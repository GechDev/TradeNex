# TradeNex

**Engineering the mechanics of electronic markets.**

TradeNex is a C++20 market simulation and limit order matching engine designed to model how buy and sell orders interact in an electronic exchange.

The project explores the core engineering behind trading infrastructure, with an emphasis on efficient data structures, deterministic execution, matching correctness, and performance measurement.

## Features

- **Limit Order Book** — Organizes active buy and sell orders by price level.
- **Matching Engine** — Implements price-time priority for order execution.
- **Order Management** — Supports order submission, cancellation, and lifecycle tracking.
- **Trade Execution** — Handles full fills, partial fills, and unmatched quantities.
- **Market Simulation** — Processes ordered streams of market events.
- **Deterministic Replay** — Reproduces event sequences for debugging and validation.
- **Automated Testing** — Tests matching rules, order book invariants, and edge cases.
- **Performance Benchmarks** — Evaluates throughput and order-processing latency.

## Architecture

```text
           Incoming Orders
                  |
                  v
           Order Entry API
                  |
                  v
           Matching Engine
                  |
                  v
           Limit Order Book
             /         \
        Buy Orders   Sell Orders
             \         /
                  |
                  v
            Trade Events
                  |
                  v
          Simulation / Replay
```

## Technology Stack

- **Language:** C++20
- **Build System:** CMake
- **Testing:** GoogleTest
- **Version Control:** Git

## Getting Started

### Prerequisites

- A C++20-compatible compiler
- CMake 3.20 or newer
- Git

### Clone the Repository

```bash
git clone https://github.com/GechDev/TradeNex.git
cd TradeNex
```

### Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Run Tests

```bash
ctest --test-dir build --output-on-failure
```

### Run the Simulator

```bash
./build/tradenex
```

*Update the executable name and commands to match the actual implementation.*

## Matching Rules

TradeNex is designed around price-time priority:

1. Better-priced orders have execution priority.
2. Orders at the same price are processed in arrival order.
3. Incoming orders match eligible orders on the opposite side.
4. Executions occur at the resting order's price.
5. Fully filled orders leave the book.
6. Partially filled orders retain their remaining quantity.
7. Unmatched limit orders rest on the book; market orders do not.

These rules should be validated through automated tests as the engine develops.

## Performance Evaluation

TradeNex aims to measure performance using reproducible workloads.

Key metrics include:

- Orders processed per second
- Median and tail processing latency
- Matching performance across different book sizes
- Cancellation and execution workloads
- Memory usage under sustained activity

Benchmark reports should document the hardware, compiler, build configuration, and workload so results can be reproduced.

## Testing Strategy

The test suite should cover:

- Price-time priority
- Buy and sell order matching
- Full and partial fills
- Market orders with insufficient liquidity
- Order cancellation and invalid order IDs
- Multiple orders at the same price
- Empty books and unmatched orders
- Order book consistency after each operation
- Deterministic replay of identical event sequences

## Roadmap

- [ ] Implement the core order book
- [ ] Build the matching engine
- [ ] Support limit and market orders
- [ ] Implement cancellations and partial fills
- [ ] Add deterministic event replay
- [ ] Expand automated tests and edge-case coverage
- [ ] Add reproducible performance benchmarks
- [ ] Profile and optimize critical execution paths
- [ ] Add market-data event output
- [ ] Explore an optional real-time visualization dashboard

## Engineering Principles

- **Correctness first:** Matching behavior must be deterministic and testable.
- **Measure before optimizing:** Performance improvements should be backed by benchmarks.
- **Modular design:** Keep the matching engine independent of interfaces and external services.
- **Reproducibility:** Make bugs and benchmark results easy to reproduce.

## License

Add an open-source license before distributing the project.

---

TradeNex is an educational engineering project and is not intended for production trading or financial decision-making.
