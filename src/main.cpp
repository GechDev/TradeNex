#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "tradenex/matching_engine.hpp"
#include "tradenex/simulator.hpp"

namespace {

void print_header() {
    std::cout << "======================================================================\n";
    std::cout << " TradeNex — High-Performance C++20 Limit Order Matching Engine\n";
    std::cout << " Engineering the Mechanics of Electronic Markets\n";
    std::cout << "======================================================================\n\n";
}

void print_usage(const char* prog_name) {
    std::cout << "Usage:\n";
    std::cout << "  " << prog_name << " demo\n";
    std::cout << "  " << prog_name << " replay <path_to_csv>\n";
    std::cout << "  " << prog_name << " benchmark\n";
    std::cout << "  " << prog_name << " --help\n\n";
    std::cout << "Commands:\n";
    std::cout << "  demo       Runs an interactive exchange simulation walkthrough.\n";
    std::cout << "  replay     Replays a deterministic sequence of market orders from CSV.\n";
    std::cout << "  benchmark  Executes rapid throughput and latency diagnostics.\n";
}

void print_depth(const tradenex::OrderBook& book) {
    auto asks = book.depth(tradenex::Side::Sell, 5);
    auto bids = book.depth(tradenex::Side::Buy, 5);

    std::cout << "\n  --- Current Order Book Depth (Top 5) ---\n";
    std::cout << "  ASKS (Sell Side):\n";
    if (asks.empty()) {
        std::cout << "    [No resting asks]\n";
    } else {
        // Print asks descending so lowest ask is closest to spread
        for (auto it = asks.rbegin(); it != asks.rend(); ++it) {
            std::cout << "    Price: " << std::setw(6) << it->price
                      << " | Qty: " << std::setw(6) << it->quantity
                      << " | Orders: " << it->order_count << "\n";
        }
    }

    auto spread = book.spread();
    auto mid = book.mid_price();
    std::cout << "  ----------------------------------------\n";
    if (spread && mid) {
        std::cout << "  Spread: " << *spread << " ticks | Mid Price: " << std::fixed << std::setprecision(2) << *mid << "\n";
    } else {
        std::cout << "  Spread: [N/A] | Mid: [N/A]\n";
    }
    std::cout << "  ----------------------------------------\n";

    std::cout << "  BIDS (Buy Side):\n";
    if (bids.empty()) {
        std::cout << "    [No resting bids]\n";
    } else {
        for (const auto& b : bids) {
            std::cout << "    Price: " << std::setw(6) << b.price
                      << " | Qty: " << std::setw(6) << b.quantity
                      << " | Orders: " << b.order_count << "\n";
        }
    }
    std::cout << "\n";
}

int run_demo() {
    print_header();
    std::cout << ">> Running interactive exchange simulation demo...\n\n";

    tradenex::MatchingEngine engine;

    std::cout << "[Step 1] Seeding initial two-sided resting liquidity...\n";
    engine.submit_order(tradenex::OrderRequest::limit(101, tradenex::Side::Buy, 9990, 50, tradenex::TimeInForce::GTC, 1));
    engine.submit_order(tradenex::OrderRequest::limit(102, tradenex::Side::Buy, 9995, 30, tradenex::TimeInForce::GTC, 1));
    engine.submit_order(tradenex::OrderRequest::limit(103, tradenex::Side::Buy, 9995, 20, tradenex::TimeInForce::GTC, 2));

    engine.submit_order(tradenex::OrderRequest::limit(201, tradenex::Side::Sell, 10005, 40, tradenex::TimeInForce::GTC, 3));
    engine.submit_order(tradenex::OrderRequest::limit(202, tradenex::Side::Sell, 10010, 60, tradenex::TimeInForce::GTC, 4));

    print_depth(engine.book());

    std::cout << "[Step 2] Incoming crossing limit Buy order #301: BUY 25 @ 10005 (matches resting ask #201)...\n";
    std::vector<tradenex::Trade> trades;
    auto res1 = engine.submit_order(tradenex::OrderRequest::limit(301, tradenex::Side::Buy, 10005, 25, tradenex::TimeInForce::GTC, 5), trades);
    std::cout << "  -> Result: " << tradenex::to_string(res1.status) << ", filled: " << res1.filled_quantity << "\n";
    for (const auto& t : trades) {
        std::cout << "  -> Trade #" << t.trade_id << ": Executed " << t.quantity << " @ " << t.price
                  << " (Maker #" << t.maker_order_id << ", Taker #" << t.taker_order_id << ")\n";
    }
    print_depth(engine.book());

    std::cout << "[Step 3] Incoming multi-level sweep Buy order #302: BUY 50 @ 10010...\n";
    trades.clear();
    auto res2 = engine.submit_order(tradenex::OrderRequest::limit(302, tradenex::Side::Buy, 10010, 50, tradenex::TimeInForce::GTC, 6), trades);
    std::cout << "  -> Result: " << tradenex::to_string(res2.status) << ", filled: " << res2.filled_quantity << "\n";
    for (const auto& t : trades) {
        std::cout << "  -> Trade #" << t.trade_id << ": Executed " << t.quantity << " @ " << t.price
                  << " (Maker #" << t.maker_order_id << ", Taker #" << t.taker_order_id << ")\n";
    }
    print_depth(engine.book());

    std::cout << "[Step 4] Incoming Market Sell order #401: SELL 35 (consumes top bids)...\n";
    trades.clear();
    auto res3 = engine.submit_order(tradenex::OrderRequest::market(401, tradenex::Side::Sell, 35, 7), trades);
    std::cout << "  -> Result: " << tradenex::to_string(res3.status) << ", filled: " << res3.filled_quantity << "\n";
    for (const auto& t : trades) {
        std::cout << "  -> Trade #" << t.trade_id << ": Executed " << t.quantity << " @ " << t.price
                  << " (Maker #" << t.maker_order_id << ", Taker #" << t.taker_order_id << ")\n";
    }
    print_depth(engine.book());

    std::cout << "[Step 5] Cancelling resting order #101...\n";
    auto res4 = engine.cancel_order(101);
    std::cout << "  -> Cancel Result: " << tradenex::to_string(res4.status) << ", cancelled qty: " << res4.cancelled_quantity << "\n";
    print_depth(engine.book());

    std::cout << "[Step 6] Validating Order Book Invariants...\n";
    auto inv = engine.book().validate_invariants();
    if (inv.valid) {
        std::cout << "  [PASS] All order book and priority invariants hold successfully.\n";
    } else {
        std::cout << "  [FAIL] Invariant violation: " << inv.error_message << "\n";
        return 1;
    }

    std::cout << "\n>> Demo walkthrough completed successfully.\n";
    return 0;
}

int run_replay(const std::string& filepath) {
    print_header();
    std::cout << ">> Replaying market events from: " << filepath << "\n\n";

    tradenex::MarketSimulator simulator;
    auto summary = simulator.replay_file(filepath);

    std::cout << "----------------------------------------------------------------------\n";
    std::cout << " Replay Execution Summary\n";
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "  Total Events Processed:        " << summary.total_events << "\n";
    std::cout << "  Orders Accepted (Rested):      " << summary.orders_accepted << "\n";
    std::cout << "  Orders Partially Filled:       " << summary.orders_partially_filled << "\n";
    std::cout << "  Orders Fully Filled:           " << summary.orders_fully_filled << "\n";
    std::cout << "  Orders Cancelled:              " << summary.orders_cancelled << "\n";
    std::cout << "  Orders Rejected:               " << summary.orders_rejected << "\n";
    std::cout << "  Trades Generated:              " << summary.trades_generated << "\n";
    std::cout << "  Total Volume Executed:         " << summary.total_volume_traded << "\n";
    std::cout << "  Final Best Bid:                " << (summary.final_best_bid ? std::to_string(*summary.final_best_bid) : "None") << "\n";
    std::cout << "  Final Best Ask:                " << (summary.final_best_ask ? std::to_string(*summary.final_best_ask) : "None") << "\n";
    std::cout << "  Book Invariants Verified:      " << (summary.invariants_hold ? "PASSED" : "FAILED (" + summary.invariant_error + ")") << "\n";
    std::cout << "----------------------------------------------------------------------\n\n";

    print_depth(simulator.engine().book());

    return summary.invariants_hold ? 0 : 1;
}

int run_quick_benchmark() {
    print_header();
    std::cout << ">> Running quick benchmark simulation (100,000 synthetic events)...\n";

    tradenex::MarketSimulator simulator;
    auto start = std::chrono::high_resolution_clock::now();
    auto summary = simulator.run_simulation(100'000, 42);
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    double elapsed_sec = static_cast<double>(elapsed_us) / 1'000'000.0;
    double throughput = static_cast<double>(summary.total_events) / elapsed_sec;

    std::cout << "----------------------------------------------------------------------\n";
    std::cout << " Benchmark Results\n";
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "  Operations Processed:          " << summary.total_events << "\n";
    std::cout << "  Total Elapsed Time:            " << std::fixed << std::setprecision(3) << elapsed_sec * 1000.0 << " ms\n";
    std::cout << "  Throughput:                    " << std::fixed << std::setprecision(0) << throughput << " ops/sec ("
              << std::setprecision(2) << throughput / 1'000'000.0 << " Mops/sec)\n";
    std::cout << "  Average Latency:               " << std::fixed << std::setprecision(2) << (elapsed_sec * 1e9 / summary.total_events) << " ns/op\n";
    std::cout << "  Trades Generated:              " << summary.trades_generated << "\n";
    std::cout << "  Volume Executed:               " << summary.total_volume_traded << "\n";
    std::cout << "  Invariants Check:              " << (summary.invariants_hold ? "PASSED" : "FAILED") << "\n";
    std::cout << "----------------------------------------------------------------------\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_header();
        print_usage(argv[0]);
        return 1;
    }

    std::string command = argv[1];
    if (command == "demo") {
        return run_demo();
    } else if (command == "replay") {
        if (argc < 3) {
            std::cerr << "Error: 'replay' command requires path to CSV file.\n";
            std::cerr << "Usage: " << argv[0] << " replay <path_to_csv>\n";
            return 1;
        }
        return run_replay(argv[2]);
    } else if (command == "benchmark") {
        return run_quick_benchmark();
    } else if (command == "--help" || command == "-h") {
        print_header();
        print_usage(argv[0]);
        return 0;
    } else {
        std::cerr << "Unknown command: '" << command << "'\n\n";
        print_usage(argv[0]);
        return 1;
    }
}
