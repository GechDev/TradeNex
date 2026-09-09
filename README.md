# TradeNex

**Engineering the mechanics of electronic markets.**

High-performance C++20 limit order matching engine and market simulator featuring price-time priority, order book management, and deterministic trade execution.

[![CI](https://github.com/GechDev/TradeNex/actions/workflows/ci.yml/badge.svg)](https://github.com/GechDev/TradeNex/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/Standard-C%2B%2B20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)

---

## Overview

TradeNex is a deterministic, single-instrument electronic limit order matching engine and market simulator. Built from the ground up in standard C++20, it explores the core mechanical and software systems underpinning modern exchange infrastructure—prioritizing algorithmic correctness, ultra-low latency, reproducible testing, and memory efficiency.

### Key Highlights

- **Price-Time Priority (FIFO):** Guarantees strict arrival-order precedence at each price level.
- **Dual-Sided Limit Order Book:** Ordered price maps with intrusive doubly-linked queues and $O(1)$ order lookup.
- **Order Types & Lifecycle:** Supports Limit (GTC, IOC, FOK) and Market orders with full/partial fills and cancellations.
- **Resting Price Execution:** Every execution occurs strictly at the maker's price, providing natural price improvement to aggressive takers.
- **Zero-Allocation Hot Path:** Employs an internal object pool arena (`OrderNodePool`) for steady-state order operations without dynamic heap churn.
- **Deterministic Replay Engine:** Replays external CSV order streams deterministically with full audit invariant checking.
- **Automated Verification:** 25 test cases covering matching edge cases, invariants, and deterministic randomized fuzz tests with recorded seeds.
- **High-Throughput Performance:** Processes over **2.2+ million operations per second** with median latency under **100 nanoseconds**.

---

## Architecture

```text
               +-------------------------------------------+
               |             Market Participant            |
               |      (Strategy / Gateway / Replay / CLI)  |
               +---------------------+---------------------+
                                     |
                                     | OrderRequest (Limit / Market / IOC / FOK)
                                     v
               +-------------------------------------------+
               |              MatchingEngine               |
               |  - Protocol Validation & Idempotency      |
               |  - Monotonic Deterministic Sequences      |
               |  - Trade ID & Lifecycle Event Dispatcher  |
               +---------------------+---------------------+
                                     |
                                     | Price-Time Matching & Depth Updates
                                     v
               +-------------------------------------------+
               |             Limit Order Book              |
               |  +-------------------+-----------------+  |
               |  |     Bids (Buy)    |   Asks (Sell)   |  |
               |  | std::greater<P>   |  std::less<P>   |  |
               |  +-------------------+-----------------+  |
               |  |  PriceLevel (Intrusive FIFO Queue)  |  |
               |  +-------------------------------------+  |
               |  |    O(1) Hash Index (OrderId -> Node)|  |
               |  +-------------------------------------+  |
               |  |    Zero-Heap Node Object Pool Arena |  |
               |  +-------------------------------------+  |
               +---------------------+---------------------+
                                     |
                                     | Fills & Trades
                                     v
               +-------------------------------------------+
               |              Execution Logs               |
               |  - Deterministic Trade Stream             |
               |  - Audit Invariant Verification           |
               +-------------------------------------------+
```

For complete architectural details, see [docs/architecture.md](docs/architecture.md).

---

## Quick Start

### Prerequisites

- A C++20-compliant compiler (GCC 11+, Clang 14+, or MSVC 19.30+)
- CMake 3.20+
- Ninja (optional, recommended for fast builds)

### Clone & Build

```bash
git clone https://github.com/GechDev/TradeNex.git
cd TradeNex

# Configure & Build with Release optimizations
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run automated test suite
ctest --test-dir build --output-on-failure
```

### CLI Usage

```bash
# 1. Interactive Matching Walkthrough
./build/tradenex demo

# 2. Replay Deterministic CSV Event Stream
./build/tradenex replay examples/sample_orders.csv

# 3. Quick Performance Benchmark
./build/tradenex benchmark

# 4. Standalone Micro-Benchmark Suite
./build/tradenex_benchmark
```

---

## Data Structures & Complexity

| Operation | Selected Structure | Time Complexity | Space Complexity |
|---|---|---|---|
| **Best Bid / Ask Query** | Map Root (`begin()`) | $O(1)$ amortized | $O(1)$ |
| **Limit Order Insertion** | `std::map` + Intrusive List | $O(\log P)$ | $O(1)$ (pool) |
| **Order Matching** | In-place Queue Consumption | $O(M)$ | $O(1)$ |
| **Order Cancellation** | `std::unordered_map` index | $O(1)$ average | $O(1)$ |
| **Depth Snapshot (Top K)** | Forward Map Traversal | $O(K)$ | $O(K)$ |
| **Invariant Validation** | Full Depth & Index Audit | $O(N + P)$ | $O(1)$ |

*Where $N$ = total resting orders, $P$ = active price levels, and $M$ = number of matched maker orders.*

Detailed rationale: [docs/design-decisions.md](docs/design-decisions.md).

---

## Matching Semantics & Invariants

1. **Resting Order Pricing:** Incoming aggressive orders execute strictly at the maker's limit price.
2. **Partial Fills:** If an incoming order partially fills a resting order, the resting remainder retains its original FIFO priority.
3. **Market Order Rules:** Market orders consume eligible resting liquidity. Any unfilled remainder is immediately cancelled and **never rests on the book**.
4. **Conservation of Volume:**
   $$\sum \text{Submitted Quantities} = 2 \times \sum \text{Trade Quantities} + \sum \text{Resting Quantities} + \sum \text{Cancelled Quantities}$$
5. **State Preservation:** Duplicate order IDs, invalid prices ($P \le 0$), or invalid quantities ($Q \le 0$) are rejected with zero mutations to existing book state.

Detailed specification: [docs/matching-rules.md](docs/matching-rules.md).

---

## Benchmark Results

Empirical results recorded on an x86_64 CPU under Release mode (`-O3 -DNDEBUG`):

| Workload Description | Ops | Throughput (Mops/s) | p50 Latency | p95 Latency | p99 Latency |
|---|---:|---:|---:|---:|---:|
| **Order Insertion (No Match)** | 50,000 | **1.8 Mops/s** | < 100 ns | 1,000 ns | 1,000 ns |
| **1-to-1 Match Execution** | 50,000 | **2.2 Mops/s** | < 100 ns | 1,000 ns | 1,000 ns |
| **1-to-5 Multi-Level Sweep** | 10,000 | **0.6 Mops/s** | 1,000 ns | 2,000 ns | 5,000 ns |
| **Order Cancellation O(1)** | 50,000 | **1.8 Mops/s** | < 100 ns | 1,000 ns | 2,000 ns |
| **Deep Queue (10k Orders @ 1 Price)** | 5,000 | **2.9 Mops/s** | < 100 ns | 1,000 ns | 2,000 ns |
| **Mixed Workload (50A/30C/20M)** | 50,000 | **2.2 Mops/s** | < 100 ns | 1,000 ns | 1,000 ns |

Methodology and latency distribution analysis: [docs/benchmarking.md](docs/benchmarking.md).

---

## Testing & Reliability

The automated test suite runs via CTest and covers:
- Exact price matching and price improvement
- Multi-level sweeps and partial fills
- Market orders against empty and low-liquidity books
- Fill-or-kill (FOK) and Immediate-or-cancel (IOC) logic
- Cancellation idempotency and relative priority preservation
- Order book invariant verification and cross-market prevention
- Property-based deterministic randomized fuzz testing with recorded seeds (`12345ULL`, `67890ULL`, `99999ULL`)

```bash
./build/tradenex_tests
```

---

## Project Structure

```text
TradeNex/
├── CMakeLists.txt              # Top-level CMake build configuration
├── CMakePresets.json           # Presets for Release, Debug, and Sanitizer builds
├── README.md                   # Project overview and documentation
├── LICENSE                     # MIT License
├── .gitignore                  # Git ignore rules
├── .clang-format               # C++20 code formatting rules
├── .github/
│   └── workflows/
│       └── ci.yml              # Multi-platform GitHub Actions CI
├── include/
│   └── tradenex/
│       ├── types.hpp           # Core domain types, ticks, and enumerations
│       ├── order.hpp           # Order, OrderRequest, and validation contract
│       ├── trade.hpp           # Execution record and operation results
│       ├── order_book.hpp      # Dual-sided book, PriceLevel, and OrderNodePool
│       ├── matching_engine.hpp # Price-time priority matching engine
│       ├── events.hpp          # Typed market lifecycle events & listeners
│       └── simulator.hpp       # Deterministic CSV replay and simulation
├── src/
│   ├── order_book.cpp          # Order book and intrusive list implementation
│   ├── matching_engine.cpp     # Matching logic, sweeps, and cancellation
│   ├── simulator.cpp           # CSV parser and event replay driver
│   └── main.cpp                # Command-line interface (demo, replay, bench)
├── tests/
│   ├── test_framework.hpp      # Zero-dependency GoogleTest-compatible test harness
│   ├── order_book_tests.cpp    # Book depth, spread, and queue unit tests
│   ├── matching_tests.cpp      # Crossing match, FOK, IOC, and sweep tests
│   ├── cancellation_tests.cpp  # Order cancellation edge cases
│   ├── invariant_tests.cpp     # Structural invariants & randomized fuzzing
│   └── replay_tests.cpp        # CSV determinism and diagnostics tests
├── benchmarks/
│   └── matching_benchmark.cpp  # Latency percentile and throughput profiler
├── examples/
│   └── sample_orders.csv       # Documented sample event stream for replay
└── docs/
    ├── architecture.md         # Detailed component architecture
    ├── matching-rules.md       # Formal matching rules and invariants
    ├── design-decisions.md     # Data structure trade-offs and complexity
    └── benchmarking.md         # Performance methodology and metrics
```

---

## Known Limitations & Roadmap

- **Single Instrument:** Currently models a single order book per engine instance. Multi-instrument support via symbol sharding is planned.
- **Lock-Free Concurrency:** The core engine is single-threaded by design to avoid cache contention and maintain determinism. A multi-threaded gateway routing to distinct instrument engines is a natural extension.
- **Market By Order (MBO) Feed:** Planned binary protocol output (e.g. ITCH/OUCH) for ultra-low overhead network broadcasting.

---

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.
