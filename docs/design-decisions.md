# TradeNex Design Decisions & Complexity Analysis

This document details the architectural choices, data structure trade-offs, and algorithmic complexities implemented in TradeNex.

---

## 1. Data Structure Selection for Order Book

| Component | Selected Structure | Alternatives Considered | Rationale & Trade-offs |
|---|---|---|---|
| **Price Levels** | `std::map<Price, PriceLevel, Compare>` | Flat contiguous array (Ladder), B-Tree, SkipList | `std::map` (Red-Black tree) allows arbitrary price tick dispersion without pre-allocating gigabytes of memory. While flat ladder arrays offer cache-contiguous lookup for narrow price ranges, `std::map` provides robust $O(\log P)$ price insertion/deletion and $O(1)$ amortized access to the best bid/ask (`begin()`). |
| **Queue within Level** | Intrusive Doubly-Linked List (`OrderNode`) | `std::deque`, `std::vector`, `std::list` | Standard containers (`std::list`, `std::deque`) incur per-element dynamic allocations and pointer overhead. An intrusive doubly-linked list embeds `prev` and `next` pointers directly inside `OrderNode`, allowing $O(1)$ removal from the middle of any queue upon cancellation without traversing. |
| **Order Index** | `std::unordered_map<OrderId, OrderNode*>` | Dense array, Sorted vector | A hash map gives $O(1)$ average time complexity for locating resting orders by `OrderId`. Cancellations do not require scanning price levels or linked lists. |
| **Node Allocations** | `OrderNodePool` (Pre-allocated chunk pool) | `malloc`/`free`, `std::pmr::monotonic_buffer_resource` | Pre-allocating chunks of `OrderNode` in an arena free-list completely eliminates memory allocation overhead from the matching engine's hot path, avoiding memory fragmentation and system call latency spikes. |

---

## 2. Operation Time & Space Complexity

Let:
- $N$ = Number of resting orders in the book.
- $P$ = Number of active price levels.
- $M$ = Number of resting orders filled by an incoming crossing order.

| Operation | Average Complexity | Worst-Case Complexity | Description |
|---|---|---|---|
| **Best Bid / Best Ask Query** | $O(1)$ | $O(1)$ | Dereferences `bids_.begin()` or `asks_.begin()`. |
| **Non-Crossing Order Insertion** | $O(\log P)$ | $O(\log P)$ | Level lookup in tree takes $O(\log P)$, followed by $O(1)$ intrusive list append and $O(1)$ hash table insertion. |
| **Crossing Order Matching** | $O(M)$ | $O(M + \log P)$ | Consumes $M$ orders from the head of the best price queues in $O(1)$ per order. Level removal takes $O(\log P)$ when depleted. |
| **Order Cancellation** | $O(1)$ | $O(1)^*$ | Hash map lookup is $O(1)$ average. Intrusive doubly-linked list removal is strictly $O(1)$. Level cleanup is $O(\log P)$ if empty. |
| **Order Book Depth Snapshot** | $O(K)$ | $O(K)$ | Traverses top $K$ price levels directly from `begin()`. |
| **Invariant Audit** | $O(N + P)$ | $O(N + P)$ | Full structural consistency scan across all price levels, orders, and hash index. |

$^*$Worst-case hash collision is $O(N)$, but mitigated by standard 64-bit integer identity distribution.

---

## 3. Concurrency Strategy: Single-Threaded Core

TradeNex intentionally adopts a deterministic, single-threaded matching core.
- **Why?** Electronic exchanges typically shard processing by financial instrument (one matching engine per ticker/symbol). Multi-threaded synchronisation (mutex locks, atomics) on a single limit order book introduces lock contention, cache invalidation, and non-deterministic event orderings that degrade latency and make reproducible testing impossible.
- Keeping the core single-threaded ensures predictable cache residency, absolute determinism, zero race conditions, and sub-microsecond latency.
