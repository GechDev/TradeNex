#include "tradenex/simulator.hpp"

#include <charconv>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

namespace tradenex {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

std::vector<std::string_view> split_csv_line(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (start < line.size()) {
        std::size_t comma = line.find(',', start);
        if (comma == std::string_view::npos) {
            fields.push_back(trim(line.substr(start)));
            break;
        }
        fields.push_back(trim(line.substr(start, comma - start)));
        start = comma + 1;
    }
    if (!line.empty() && line.back() == ',') {
        fields.push_back("");
    }
    return fields;
}

}  // namespace

MarketSimulator::MarketSimulator()
    : owned_engine_(), engine_ptr_(&owned_engine_) {}

MarketSimulator::MarketSimulator(MatchingEngine& engine)
    : owned_engine_(), engine_ptr_(&engine) {}

std::vector<ReplayRecord> MarketSimulator::parse_csv(std::string_view content, std::vector<std::string>& out_errors) {
    std::vector<ReplayRecord> records;
    std::size_t line_number = 0;
    std::size_t pos = 0;

    while (pos < content.size()) {
        std::size_t end = content.find('\n', pos);
        std::string_view line = (end == std::string_view::npos) ? content.substr(pos) : content.substr(pos, end - pos);
        pos = (end == std::string_view::npos) ? content.size() : end + 1;
        ++line_number;

        line = trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }

        // Check if header line
        if (line.starts_with("event") || line.starts_with("EVENT") || line.starts_with("Action")) {
            continue;
        }

        auto fields = split_csv_line(line);
        if (fields.empty()) {
            continue;
        }

        std::string_view action_str = fields[0];
        ReplayRecord record;
        record.line_number = line_number;

        if (action_str == "ADD" || action_str == "add") {
            record.action = ReplayRecord::Action::Add;
            if (fields.size() < 6) {
                out_errors.push_back("Line " + std::to_string(line_number) + ": ADD event requires 6 columns (event,order_id,side,type,price_ticks,quantity)");
                continue;
            }

            // order_id
            auto res_id = std::from_chars(fields[1].data(), fields[1].data() + fields[1].size(), record.order_id);
            if (res_id.ec != std::errc()) {
                out_errors.push_back("Line " + std::to_string(line_number) + ": Invalid order_id '" + std::string(fields[1]) + "'");
                continue;
            }

            // side
            auto opt_side = parse_side(fields[2]);
            if (!opt_side) {
                out_errors.push_back("Line " + std::to_string(line_number) + ": Invalid side '" + std::string(fields[2]) + "'");
                continue;
            }
            record.side = *opt_side;

            // type
            auto opt_type = parse_order_type(fields[3]);
            if (!opt_type) {
                out_errors.push_back("Line " + std::to_string(line_number) + ": Invalid order type '" + std::string(fields[3]) + "'");
                continue;
            }
            record.type = *opt_type;

            // price
            if (record.type == OrderType::Limit) {
                auto res_price = std::from_chars(fields[4].data(), fields[4].data() + fields[4].size(), record.price);
                if (res_price.ec != std::errc()) {
                    out_errors.push_back("Line " + std::to_string(line_number) + ": Invalid price '" + std::string(fields[4]) + "'");
                    continue;
                }
            } else {
                record.price = 0;
            }

            // quantity
            auto res_qty = std::from_chars(fields[5].data(), fields[5].data() + fields[5].size(), record.quantity);
            if (res_qty.ec != std::errc()) {
                out_errors.push_back("Line " + std::to_string(line_number) + ": Invalid quantity '" + std::string(fields[5]) + "'");
                continue;
            }

            records.push_back(record);
        } else if (action_str == "CANCEL" || action_str == "cancel") {
            record.action = ReplayRecord::Action::Cancel;
            if (fields.size() < 2) {
                out_errors.push_back("Line " + std::to_string(line_number) + ": CANCEL event requires order_id");
                continue;
            }

            auto res_id = std::from_chars(fields[1].data(), fields[1].data() + fields[1].size(), record.order_id);
            if (res_id.ec != std::errc()) {
                out_errors.push_back("Line " + std::to_string(line_number) + ": Invalid order_id for CANCEL: '" + std::string(fields[1]) + "'");
                continue;
            }

            records.push_back(record);
        } else {
            out_errors.push_back("Line " + std::to_string(line_number) + ": Unknown event action '" + std::string(action_str) + "'");
        }
    }

    return records;
}

ReplaySummary MarketSimulator::replay_csv_content(std::string_view csv_content) {
    ReplaySummary summary;
    std::vector<std::string> parse_errors;
    auto records = parse_csv(csv_content, parse_errors);

    for (const auto& err : parse_errors) {
        std::cerr << "  [PARSER WARNING] " << err << "\n";
    }

    for (const auto& rec : records) {
        ++summary.total_events;

        if (rec.action == ReplayRecord::Action::Add) {
            OrderRequest req;
            req.id = rec.order_id;
            req.side = rec.side;
            req.type = rec.type;
            req.price = rec.price;
            req.quantity = rec.quantity;
            req.tif = (rec.type == OrderType::Market) ? TimeInForce::IOC : TimeInForce::GTC;
            req.timestamp = summary.total_events;

            std::vector<Trade> trades;
            OrderResult res = engine_ptr_->submit_order(req, trades);

            for (const auto& t : trades) {
                ++summary.trades_generated;
                summary.total_volume_traded += t.quantity;
            }

            switch (res.status) {
                case OrderStatus::Accepted:
                    ++summary.orders_accepted;
                    break;
                case OrderStatus::PartiallyFilled:
                    ++summary.orders_partially_filled;
                    break;
                case OrderStatus::Filled:
                    ++summary.orders_fully_filled;
                    break;
                case OrderStatus::Cancelled:
                    ++summary.orders_cancelled;
                    break;
                case OrderStatus::Rejected:
                    ++summary.orders_rejected;
                    break;
            }
        } else if (rec.action == ReplayRecord::Action::Cancel) {
            OrderResult res = engine_ptr_->cancel_order(rec.order_id);
            if (res.status == OrderStatus::Cancelled) {
                ++summary.orders_cancelled;
            } else {
                ++summary.orders_rejected;
            }
        }
    }

    summary.final_best_bid = engine_ptr_->book().best_bid();
    summary.final_best_ask = engine_ptr_->book().best_ask();

    auto inv = engine_ptr_->book().validate_invariants();
    summary.invariants_hold = inv.valid;
    summary.invariant_error = inv.error_message;

    return summary;
}

ReplaySummary MarketSimulator::replay_file(const std::string& csv_filepath) {
    std::ifstream file(csv_filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open CSV replay file: " << csv_filepath << "\n";
        ReplaySummary failed_summary;
        failed_summary.invariants_hold = false;
        failed_summary.invariant_error = "File could not be opened: " + csv_filepath;
        return failed_summary;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return replay_csv_content(buffer.str());
}

ReplaySummary MarketSimulator::run_simulation(std::size_t num_events, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> action_dist(1, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<Price> spread_dist(1, 10);
    std::uniform_int_distribution<Quantity> qty_dist(1, 50);

    Price mid = 10000;
    std::vector<OrderId> active_ids;
    OrderId next_id = 1;

    ReplaySummary summary;

    for (std::size_t i = 0; i < num_events; ++i) {
        ++summary.total_events;
        int action = action_dist(rng);

        if (action <= 60 || active_ids.empty()) {
            // Limit order submission
            Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
            Price price = (side == Side::Buy) ? mid - spread_dist(rng) : mid + spread_dist(rng);
            Quantity qty = qty_dist(rng);
            OrderId id = next_id++;

            OrderRequest req = OrderRequest::limit(id, side, price, qty, TimeInForce::GTC, 1, i + 1);
            std::vector<Trade> trades;
            OrderResult res = engine_ptr_->submit_order(req, trades);

            for (const auto& t : trades) {
                ++summary.trades_generated;
                summary.total_volume_traded += t.quantity;
            }

            if (res.status == OrderStatus::Accepted) {
                ++summary.orders_accepted;
                active_ids.push_back(id);
            } else if (res.status == OrderStatus::PartiallyFilled) {
                ++summary.orders_partially_filled;
                active_ids.push_back(id);
            } else if (res.status == OrderStatus::Filled) {
                ++summary.orders_fully_filled;
            } else if (res.status == OrderStatus::Cancelled) {
                ++summary.orders_cancelled;
            } else {
                ++summary.orders_rejected;
            }
        } else if (action <= 80) {
            // Market order
            Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
            Quantity qty = qty_dist(rng) / 2 + 1;
            OrderId id = next_id++;

            OrderRequest req = OrderRequest::market(id, side, qty, 2, i + 1);
            std::vector<Trade> trades;
            OrderResult res = engine_ptr_->submit_order(req, trades);

            for (const auto& t : trades) {
                ++summary.trades_generated;
                summary.total_volume_traded += t.quantity;
            }

            if (res.status == OrderStatus::Filled) {
                ++summary.orders_fully_filled;
            } else if (res.status == OrderStatus::PartiallyFilled) {
                ++summary.orders_partially_filled;
            } else {
                ++summary.orders_cancelled;
            }
        } else {
            // Cancel order
            std::uniform_int_distribution<std::size_t> idx_dist(0, active_ids.size() - 1);
            std::size_t idx = idx_dist(rng);
            OrderId target_id = active_ids[idx];
            active_ids.erase(active_ids.begin() + idx);

            OrderResult res = engine_ptr_->cancel_order(target_id);
            if (res.status == OrderStatus::Cancelled) {
                ++summary.orders_cancelled;
            } else {
                ++summary.orders_rejected;
            }
        }

        // Slight mid-price random walk
        if (action_dist(rng) > 90) {
            mid += (side_dist(rng) == 0 ? 1 : -1);
            if (mid < 1000) mid = 1000;
        }
    }

    summary.final_best_bid = engine_ptr_->book().best_bid();
    summary.final_best_ask = engine_ptr_->book().best_ask();

    auto inv = engine_ptr_->book().validate_invariants();
    summary.invariants_hold = inv.valid;
    summary.invariant_error = inv.error_message;

    return summary;
}

}  // namespace tradenex
