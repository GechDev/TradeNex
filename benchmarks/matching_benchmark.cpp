#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "tradenex/matching_engine.hpp"

namespace {

struct BenchMetrics {
    std::string name;
    std::size_t operations{0};
    double total_time_ms{0.0};
    double throughput_ops_sec{0.0};
    double p50_ns{0.0};
    double p95_ns{0.0};
    double p99_ns{0.0};
    double max_ns{0.0};
};

BenchMetrics compute_metrics(const std::string& name, std::vector<std::int64_t>& latencies_ns) {
    BenchMetrics m;
    m.name = name;
    m.operations = latencies_ns.size();
    if (latencies_ns.empty()) return m;

    std::sort(latencies_ns.begin(), latencies_ns.end());

    std::int64_t total_ns = std::accumulate(latencies_ns.begin(), latencies_ns.end(), std::int64_t{0});
    m.total_time_ms = static_cast<double>(total_ns) / 1'000'000.0;
    m.throughput_ops_sec = static_cast<double>(m.operations) / (static_cast<double>(total_ns) / 1e9);

    m.p50_ns = static_cast<double>(latencies_ns[m.operations * 50 / 100]);
    m.p95_ns = static_cast<double>(latencies_ns[m.operations * 95 / 100]);
    m.p99_ns = static_cast<double>(latencies_ns[m.operations * 99 / 100]);
    m.max_ns = static_cast<double>(latencies_ns.back());

    return m;
}

void print_metric_row(const BenchMetrics& m) {
    std::cout << std::left << std::setw(34) << m.name
              << std::right << std::setw(12) << m.operations
              << std::setw(14) << std::fixed << std::setprecision(1) << (m.throughput_ops_sec / 1e6)
              << std::setw(12) << std::fixed << std::setprecision(1) << m.p50_ns
              << std::setw(12) << std::fixed << std::setprecision(1) << m.p95_ns
              << std::setw(12) << std::fixed << std::setprecision(1) << m.p99_ns
              << std::setw(12) << std::fixed << std::setprecision(1) << m.max_ns
              << "\n";
}

// 1. Order Insertion Without Matching
BenchMetrics bench_insertion(std::size_t count) {
    tradenex::MatchingEngine engine(count * 2);
    std::vector<std::int64_t> latencies;
    latencies.reserve(count);

    tradenex::OrderId id = 1;
    tradenex::Price price = 10000;

    for (std::size_t i = 0; i < count; ++i) {
        tradenex::OrderRequest req = tradenex::OrderRequest::limit(id++, tradenex::Side::Buy, price - static_cast<tradenex::Price>(i % 500 + 1), 10);
        auto t0 = std::chrono::high_resolution_clock::now();
        engine.submit_order(req);
        auto t1 = std::chrono::high_resolution_clock::now();
        latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    return compute_metrics("Order Insertion (No Match)", latencies);
}

// 2. 1-to-1 Matching (Crossing Limit Orders)
BenchMetrics bench_one_to_one(std::size_t count) {
    tradenex::MatchingEngine engine(count * 2);
    std::vector<std::int64_t> latencies;
    latencies.reserve(count);

    tradenex::OrderId id = 1;
    std::vector<tradenex::Trade> trades;

    for (std::size_t i = 0; i < count; ++i) {
        // Place resting ask
        engine.submit_order(tradenex::OrderRequest::limit(id++, tradenex::Side::Sell, 10000, 10));

        // Incoming aggressive buy matches resting ask exactly
        tradenex::OrderRequest taker = tradenex::OrderRequest::limit(id++, tradenex::Side::Buy, 10000, 10);
        trades.clear();

        auto t0 = std::chrono::high_resolution_clock::now();
        engine.submit_order(taker, trades);
        auto t1 = std::chrono::high_resolution_clock::now();

        latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    return compute_metrics("1-to-1 Match Execution", latencies);
}

// 3. 1-to-Many Multi-Level Sweep Matching
BenchMetrics bench_multi_level_sweep(std::size_t count, std::size_t levels_to_sweep) {
    tradenex::MatchingEngine engine(count * levels_to_sweep * 2);
    std::vector<std::int64_t> latencies;
    latencies.reserve(count);

    tradenex::OrderId id = 1;
    std::vector<tradenex::Trade> trades;

    for (std::size_t i = 0; i < count; ++i) {
        // Build depth across multiple levels
        for (std::size_t lvl = 0; lvl < levels_to_sweep; ++lvl) {
            engine.submit_order(tradenex::OrderRequest::limit(
                id++, tradenex::Side::Sell, 10000 + static_cast<tradenex::Price>(lvl * 2), 10));
        }

        // Taker sweeps all levels
        tradenex::Quantity sweep_qty = static_cast<tradenex::Quantity>(levels_to_sweep * 10);
        tradenex::OrderRequest taker = tradenex::OrderRequest::limit(
            id++, tradenex::Side::Buy, 10000 + static_cast<tradenex::Price>(levels_to_sweep * 2), sweep_qty);
        trades.clear();

        auto t0 = std::chrono::high_resolution_clock::now();
        engine.submit_order(taker, trades);
        auto t1 = std::chrono::high_resolution_clock::now();

        latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    return compute_metrics("1-to-" + std::to_string(levels_to_sweep) + " Multi-Level Sweep", latencies);
}

// 4. Order Cancellation
BenchMetrics bench_cancellation(std::size_t count) {
    tradenex::MatchingEngine engine(count * 2);
    std::vector<std::int64_t> latencies;
    latencies.reserve(count);

    std::vector<tradenex::OrderId> ids;
    ids.reserve(count);

    tradenex::Price price = 10000;
    for (std::size_t i = 0; i < count; ++i) {
        tradenex::OrderId id = i + 1;
        engine.submit_order(tradenex::OrderRequest::limit(id, tradenex::Side::Buy, price - static_cast<tradenex::Price>(i % 500 + 1), 10));
        ids.push_back(id);
    }

    // Shuffle IDs to benchmark non-sequential hash lookup and level removal
    std::mt19937_64 rng(1337);
    std::shuffle(ids.begin(), ids.end(), rng);

    for (tradenex::OrderId id : ids) {
        auto t0 = std::chrono::high_resolution_clock::now();
        engine.cancel_order(id);
        auto t1 = std::chrono::high_resolution_clock::now();
        latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    return compute_metrics("Order Cancellation O(1)", latencies);
}

// 5. Deep Queue Scaling (Many orders at single price level)
BenchMetrics bench_deep_queue_priority(std::size_t count) {
    tradenex::MatchingEngine engine(count * 2);
    std::vector<std::int64_t> latencies;
    latencies.reserve(count);

    // Seed 10,000 resting orders at the SAME price level
    tradenex::OrderId id = 1;
    for (std::size_t i = 0; i < count; ++i) {
        engine.submit_order(tradenex::OrderRequest::limit(id++, tradenex::Side::Buy, 10000, 10));
    }

    std::vector<tradenex::Trade> trades;
    // Execute 1,000 consecutive 1-unit fills popping the FIFO head
    std::size_t test_ops = std::min<std::size_t>(count, 5000);
    latencies.reserve(test_ops);

    for (std::size_t i = 0; i < test_ops; ++i) {
        tradenex::OrderRequest taker = tradenex::OrderRequest::limit(id++, tradenex::Side::Sell, 10000, 10);
        trades.clear();

        auto t0 = std::chrono::high_resolution_clock::now();
        engine.submit_order(taker, trades);
        auto t1 = std::chrono::high_resolution_clock::now();

        latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    return compute_metrics("Deep Queue (10k Orders @ 1 Price)", latencies);
}

// 6. Realistic Mixed Workload (50% Add, 30% Cancel, 20% Crossing/Market)
BenchMetrics bench_mixed_workload(std::size_t count) {
    tradenex::MatchingEngine engine(count * 2);
    std::vector<std::int64_t> latencies;
    latencies.reserve(count);

    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int> action_dist(1, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<tradenex::Price> spread_dist(1, 20);

    tradenex::Price mid = 10000;
    tradenex::OrderId next_id = 1;
    std::vector<tradenex::OrderId> active_ids;
    active_ids.reserve(count / 2);
    std::vector<tradenex::Trade> trades;

    for (std::size_t i = 0; i < count; ++i) {
        int action = action_dist(rng);

        if (action <= 50 || active_ids.empty()) {
            // Limit Add
            tradenex::OrderId id = next_id++;
            tradenex::Side side = side_dist(rng) == 0 ? tradenex::Side::Buy : tradenex::Side::Sell;
            tradenex::Price price = (side == tradenex::Side::Buy) ? mid - spread_dist(rng) : mid + spread_dist(rng);
            tradenex::OrderRequest req = tradenex::OrderRequest::limit(id, side, price, 10);
            trades.clear();

            auto t0 = std::chrono::high_resolution_clock::now();
            auto res = engine.submit_order(req, trades);
            auto t1 = std::chrono::high_resolution_clock::now();

            latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            if (res.status == tradenex::OrderStatus::Accepted || res.status == tradenex::OrderStatus::PartiallyFilled) {
                active_ids.push_back(id);
            }
        } else if (action <= 80) {
            // Cancel
            std::uniform_int_distribution<std::size_t> idx_dist(0, active_ids.size() - 1);
            std::size_t idx = idx_dist(rng);
            tradenex::OrderId target_id = active_ids[idx];
            active_ids.erase(active_ids.begin() + idx);

            auto t0 = std::chrono::high_resolution_clock::now();
            engine.cancel_order(target_id);
            auto t1 = std::chrono::high_resolution_clock::now();

            latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        } else {
            // Crossing / Market Order
            tradenex::OrderId id = next_id++;
            tradenex::Side side = side_dist(rng) == 0 ? tradenex::Side::Buy : tradenex::Side::Sell;
            tradenex::OrderRequest req = tradenex::OrderRequest::market(id, side, 10);
            trades.clear();

            auto t0 = std::chrono::high_resolution_clock::now();
            engine.submit_order(req, trades);
            auto t1 = std::chrono::high_resolution_clock::now();

            latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        }
    }

    return compute_metrics("Mixed Workload (50A/30C/20M)", latencies);
}

}  // namespace

int main() {
    std::cout << "=========================================================================================\n";
    std::cout << " TradeNex Engine Performance Benchmarks (C++20)\n";
    std::cout << " Profiling throughput and latency percentiles across micro-architectural workloads\n";
    std::cout << "=========================================================================================\n\n";

    std::cout << std::left << std::setw(34) << "Workload Description"
              << std::right << std::setw(12) << "Ops"
              << std::setw(14) << "Mops/sec"
              << std::setw(12) << "p50 (ns)"
              << std::setw(12) << "p95 (ns)"
              << std::setw(12) << "p99 (ns)"
              << std::setw(12) << "max (ns)"
              << "\n";
    std::cout << std::string(108, '-') << "\n";

    constexpr std::size_t kOps = 50'000;

    print_metric_row(bench_insertion(kOps));
    print_metric_row(bench_one_to_one(kOps));
    print_metric_row(bench_multi_level_sweep(10'000, 5));
    print_metric_row(bench_cancellation(kOps));
    print_metric_row(bench_deep_queue_priority(10'000));
    print_metric_row(bench_mixed_workload(kOps));

    std::cout << std::string(108, '-') << "\n";
    std::cout << ">> Benchmark suite completed successfully.\n";
    return 0;
}
