#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "tradenex/events.hpp"
#include "tradenex/order.hpp"
#include "tradenex/order_book.hpp"
#include "tradenex/trade.hpp"
#include "tradenex/types.hpp"

namespace tradenex {

class MatchingEngine {
public:
    explicit MatchingEngine(std::size_t initial_pool_capacity = 4096);
    ~MatchingEngine() = default;

    MatchingEngine(const MatchingEngine&) = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;

    // Submits a new order request (limit or market).
    // Trades generated during execution are appended to `out_trades`.
    OrderResult submit_order(const OrderRequest& req, std::vector<Trade>& out_trades);

    // Convenience overload if caller does not need separate trade list.
    OrderResult submit_order(const OrderRequest& req);

    // Cancels an active resting order by ID.
    OrderResult cancel_order(OrderId id);

    // Event listener registration
    void add_listener(IEventListener* listener);
    void remove_listener(IEventListener* listener);

    // Accessors
    [[nodiscard]] const OrderBook& book() const noexcept { return book_; }
    [[nodiscard]] OrderBook& book() noexcept { return book_; }
    [[nodiscard]] SequenceNumber current_sequence() const noexcept { return sequence_; }
    [[nodiscard]] TradeId current_trade_id() const noexcept { return trade_id_; }
    [[nodiscard]] const std::vector<Trade>& trade_history() const noexcept { return trade_history_; }

    // Resets state and counters to initial deterministic point
    void reset();

private:
    OrderResult match_buy_limit(const OrderRequest& req, std::vector<Trade>& out_trades);
    OrderResult match_sell_limit(const OrderRequest& req, std::vector<Trade>& out_trades);
    OrderResult match_buy_market(const OrderRequest& req, std::vector<Trade>& out_trades);
    OrderResult match_sell_market(const OrderRequest& req, std::vector<Trade>& out_trades);

    bool check_fok_liquidity(Side side, Price limit_price, Quantity required_qty) const;

    void notify_order_accepted(const OrderAcceptedEvent& ev);
    void notify_order_rejected(const OrderRejectedEvent& ev);
    void notify_order_partially_filled(const OrderPartiallyFilledEvent& ev);
    void notify_order_filled(const OrderFilledEvent& ev);
    void notify_order_cancelled(const OrderCancelledEvent& ev);
    void notify_trade_executed(const TradeExecutedEvent& ev);

    OrderBook book_;
    SequenceNumber sequence_{0};
    TradeId trade_id_{0};
    std::vector<Trade> trade_history_;
    std::vector<IEventListener*> listeners_;
};

}  // namespace tradenex
