# TradeNex Matching Rules & Mechanics

This document formalizes the matching semantics, price-time priority rules, and lifecycle contracts enforced by the TradeNex matching engine.

---

## 1. Price-Time Priority Principle

1. **Price Priority:** Orders offering a more favorable price execute before orders offering a less favorable price.
   - For Buy orders: higher prices have priority.
   - For Sell orders: lower prices have priority.
2. **Time Priority:** Orders entered at the same price execute in strict chronological arrival sequence (First-In, First-Out).

---

## 2. Order Types & Time-in-Force

### 2.1 Limit Orders (GTC — Good 'Til Cancelled)
- May execute immediately against crossing opposite resting orders.
- Any unfilled remainder is added to the book at its specified limit price.
- If the price level does not exist, a new price level is instantiated.
- If the price level already exists, the remainder is appended to the tail of the FIFO queue.

### 2.2 Immediate-Or-Cancel (IOC)
- Executes against eligible resting liquidity up to its specified limit price.
- Any unfilled remainder is immediately cancelled and never placed on the book.

### 2.3 Fill-Or-Kill (FOK)
- Requires the entire order quantity to execute immediately at or within its limit price.
- Pre-scans available opposite liquidity.
- If total eligible liquidity is strictly less than the required quantity, the order is cancelled with zero fills and the book is not modified.

### 2.4 Market Orders
- Does not specify a limit price.
- Aggressively consumes resting liquidity on the opposite side.
- Executes sequentially across price levels in order of best price.
- Any unfilled remainder (due to insufficient liquidity) is cancelled. Market orders never enter the resting order book.

---

## 3. Execution Pricing Rules

- **Resting Order Price Rule:** Every trade executes at the resting maker order's limit price.
- The aggressive incoming taker order receives price improvement if the resting order's limit price is better than the incoming order's limit price.
- When an incoming order matches across multiple price levels, a discrete `Trade` event is generated for each level and maker order matched.

---

## 4. Order Cancellation Semantics

- Only active resting orders can be cancelled.
- Cancellation requests specify `OrderId`.
- If the order ID is not resting in the book (e.g. already filled, previously cancelled, or invalid), the cancellation is rejected with `RejectReason::UnknownOrder`.
- Successful cancellation:
  - Removes the node from its price level in $O(1)$.
  - Erases the entry from the order index in $O(1)$.
  - If the price level becomes empty, it is removed from the book.
  - Remaining orders at the same price maintain their relative queue position.
  - Partially filled orders can be cancelled without altering or invalidating previous trade executions.

---

## 5. Invariants & Conservation Laws

At all times:
1. **Uncrossed Market:** $\text{BestBid} < \text{BestAsk}$ whenever both sides have active orders.
2. **Conservation of Volume:**
   $$\sum \text{Initial Quantities} = 2 \times \sum \text{Trade Quantities} + \sum \text{Resting Quantities} + \sum \text{Cancelled Quantities}$$
3. **No Phantom Orders:** $\sum_{level} \text{total\_quantity} = \sum_{orders} \text{remaining\_quantity}$.
4. **Idempotence & State Preservation:** Any rejected order leaves the order book completely unmodified.
