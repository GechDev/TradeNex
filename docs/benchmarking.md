# TradeNex Benchmarking Methodology & Performance Analysis

This document describes the benchmarking methodology, workloads, hardware environment, and empirical results recorded for TradeNex.

---

## 1. Benchmarking Environment

- **Operating System:** Windows 10 / 11 (x86_64)
- **Compiler:** GCC 15.2.0 (MinGW-w64 runtime)
- **Build Type:** CMake Release (`-O3 -DNDEBUG -std=c++20`)
- **Timer Mechanism:** `std::chrono::high_resolution_clock` with zero logging in the measured hot path
- **Target Architecture:** x86_64

---

## 2. Workload Definitions

1. **Order Insertion (No Match):** Submits 50,000 non-crossing buy limit orders across 500 distinct price levels. Measures pure level insertion, tree rebalancing, node acquisition, and index registration.
2. **1-to-1 Match Execution:** Submits pairs of matching limit orders. Measures the complete cycle of order placement, crossing price comparison, trade generation, fill event emission, and book removal.
3. **1-to-Many Multi-Level Sweep:** Populates 5 resting sell levels and executes a single aggressive buy order that sweeps across all 5 price levels, generating 5 distinct trades.
4. **Order Cancellation O(1):** Populates 50,000 orders, then cancels all orders in randomized order. Evaluates hash map lookup and intrusive list removal.
5. **Deep Queue Priority (10,000 Orders @ 1 Price):** Inserts 10,000 orders into a single price level, followed by consecutive taker executions. Evaluates FIFO queue head removal under extreme depth.
6. **Mixed Realistic Workload (50A/30C/20M):** Simulates active market conditions consisting of 50% limit order insertions, 30% cancellations, and 20% aggressive crossing/market orders.

---

## 3. Empirical Results

*Measurements conducted with 50,000 operations per benchmark suite on the host CPU in Release mode:*

| Workload Description | Operations | Throughput (Mops/sec) | Median p50 (ns) | Tail p95 (ns) | Tail p99 (ns) |
|---|---:|---:|---:|---:|---:|
| **Order Insertion (No Match)** | 50,000 | **1.8 Mops/s** | < 100 ns | 1,000 ns | 1,000 ns |
| **1-to-1 Match Execution** | 50,000 | **2.2 Mops/s** | < 100 ns | 1,000 ns | 1,000 ns |
| **1-to-5 Multi-Level Sweep** | 10,000 | **0.6 Mops/s** | 1,000 ns | 2,000 ns | 5,000 ns |
| **Order Cancellation O(1)** | 50,000 | **1.8 Mops/s** | < 100 ns | 1,000 ns | 2,000 ns |
| **Deep Queue (10k Orders @ 1 Price)** | 5,000 | **2.9 Mops/s** | < 100 ns | 1,000 ns | 2,000 ns |
| **Mixed Workload (50A/30C/20M)** | 50,000 | **2.2 Mops/s** | < 100 ns | 1,000 ns | 1,000 ns |

---

## 4. Analysis & Takeaways

1. **Zero-Allocation Advantage:** By utilizing the pre-allocated `OrderNodePool`, steady-state operations experience zero heap allocations, ensuring median latencies remain sub-microsecond without garbage collection pauses or allocator locks.
2. **Queue Depth Invariance:** The Deep Queue test confirms that queuing 10,000 orders at a single price level does not degrade matching performance—FIFO executions maintain 2.9 Mops/sec because removing the head node is strictly $O(1)$.
3. **Tail Latency Drivers:** p99 spikes are predominantly attributed to CPU thread preemption, operating system interrupts, and occasional cold cache misses when traversing newly created price levels in the red-black tree.
