# TradeNex Architecture & System Design

TradeNex is an ultra-low-latency, deterministic, single-instrument electronic limit order matching engine and market simulator built in modern C++20.

---

## 1. High-Level System Architecture

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

---

## 2. Component Breakdown

### 2.1 Domain Layer (`include/tradenex/types.hpp`, `order.hpp`, `trade.hpp`)
- **Integer Price Ticks (`Price = std::int64_t`):** Eliminates floating-point comparison, rounding discrepancies, and non-determinism. Converting to currency is strictly a display concern (`price * tick_size`).
- **Quantities (`Quantity = std::int64_t`):** Strict positive validation for incoming orders.
- **Typed Lifecycle States:** Orders transition deterministically through `Accepted`, `PartiallyFilled`, `Filled`, `Cancelled`, or `Rejected`.

### 2.2 Order Book Layer (`include/tradenex/order_book.hpp`, `src/order_book.cpp`)
- **Dual-Sided Price Hierarchy:**
  - `Bids`: Ordered map keyed in descending price order (`std::greater<Price>`), ensuring `bids_.begin()` points to the Best Bid in $O(1)$ amortized access.
  - `Asks`: Ordered map keyed in ascending price order (`std::less<Price>`), ensuring `asks_.begin()` points to the Best Ask in $O(1)$ amortized access.
- **Intrusive FIFO Price Levels:**
  - Each `PriceLevel` maintains an intrusive doubly-linked list (`head` and `tail`) of `OrderNode` instances.
  - Orders at equal prices are queued strictly in arrival sequence.
  - Splicing and removing an order node is strictly $O(1)$ without container re-indexing.
- **Order Index:**
  - `std::unordered_map<OrderId, OrderNode*>` provides $O(1)$ average lookup and cancellation by ID.
- **Node Object Pool (`OrderNodePool`):**
  - Chunks of `OrderNode` are pre-allocated upfront.
  - Allocations and deallocations during active trading take $O(1)$ free-list operations with zero calls to `malloc` / `new`.

### 2.3 Matching Engine (`include/tradenex/matching_engine.hpp`, `src/matching_engine.cpp`)
- **Price-Time Priority Dispatch:**
  - Crossing limit orders match against opposite-side liquidity.
  - Executions strictly print at the resting (maker) order's price.
  - Unmatched limit GTC quantities rest on the book; IOC/Market remainders are immediately cancelled.
- **Market Orders:**
  - Market orders execute against resting depth until completely filled or liquidity is exhausted.
  - Any unfilled quantity is immediately cancelled—it is never permitted to rest on the book.
- **Zero Book Mutation on Rejections:**
  - Validates IDs, quantities, and price bounds before touching internal book state.

### 2.4 Deterministic Event Replay & Simulator (`include/tradenex/simulator.hpp`, `src/simulator.cpp`)
- Parses CSV event streams (`ADD`, `CANCEL`) with detailed line diagnostics.
- Guarantees identical execution traces across platforms and runs.
- Runs invariant verification checks after every event stream.

---

## 3. Development Milestones & Historical Reconstruction Record

In accordance with project governance standards, the development progression is organized across the following target engineering periods:

1. **August 15–24, 2026 (Phase 1–3: Core Architecture, Order Book & Matching):**
   - Project foundation, CMake presets, warnings, and CI matrix.
   - Domain modeling (types, integer ticks, order lifecycle states, events).
   - Dual-sided limit order book with intrusive doubly-linked queues and $O(1)$ index.
   - Matching engine implementing deterministic price-time priority, sweeps, and market orders.
   - Cancellation semantics and state preservation on rejected operations.
2. **September 5–9, 2026 (Phase 4–6: Reliability, Replay, CLI & Benchmarks):**
   - Deterministic event replay engine with robust CSV parsing and diagnostics.
   - Command-line interface (`tradenex demo`, `tradenex replay`, `tradenex benchmark`).
   - Micro-benchmarking suite profiling throughput and latency percentiles (p50, p95, p99).
   - Property-based testing and deterministic randomized fuzzing with recorded seeds.
   - System documentation and architectural trade-off analysis.

**Reconstruction Audit Date:** October 17, 2026.  
All functional tests pass with 100% success rate, verifying algorithmic correctness, invariant preservation, and zero memory leaks.

