#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "tradenex/matching_engine.hpp"

namespace tradenex {

struct ReplaySummary {
    std::size_t total_events{0};
    std::size_t orders_accepted{0};
    std::size_t orders_partially_filled{0};
    std::size_t orders_fully_filled{0};
    std::size_t orders_cancelled{0};
    std::size_t orders_rejected{0};
    std::size_t trades_generated{0};
    Quantity total_volume_traded{0};
    std::optional<Price> final_best_bid;
    std::optional<Price> final_best_ask;
    bool invariants_hold{true};
    std::string invariant_error;
};

struct ReplayRecord {
    enum class Action { Add, Cancel, Unknown };
    Action action{Action::Unknown};
    OrderId order_id{kNoOrder};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    Price price{0};
    Quantity quantity{0};
    std::size_t line_number{0};
};

class MarketSimulator {
public:
    MarketSimulator();
    explicit MarketSimulator(MatchingEngine& engine);

    // Replays a stream or file of CSV events deterministically
    ReplaySummary replay_file(const std::string& csv_filepath);
    ReplaySummary replay_csv_content(std::string_view csv_content);

    // Runs a synthetic market simulation with specified parameters and deterministic seed
    ReplaySummary run_simulation(std::size_t num_events, std::uint64_t seed = 42);

    [[nodiscard]] MatchingEngine& engine() noexcept { return *engine_ptr_; }
    [[nodiscard]] const MatchingEngine& engine() const noexcept { return *engine_ptr_; }

private:
    std::vector<ReplayRecord> parse_csv(std::string_view content, std::vector<std::string>& out_errors);

    MatchingEngine owned_engine_;
    MatchingEngine* engine_ptr_{&owned_engine_};
};

}  // namespace tradenex
