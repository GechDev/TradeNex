#pragma once

#include <variant>
#include "tradenex/types.hpp"
#include "tradenex/trade.hpp"

namespace tradenex {

struct OrderAcceptedEvent {
    OrderId order_id{kNoOrder};
    TraderId trader{kNoTrader};
    Side side{Side::Buy};
    Price price{0};
    Quantity quantity{0};
    Timestamp timestamp{0};
    SequenceNumber sequence{0};
};

struct OrderRejectedEvent {
    OrderId order_id{kNoOrder};
    TraderId trader{kNoTrader};
    RejectReason reason{RejectReason::None};
    Timestamp timestamp{0};
    SequenceNumber sequence{0};
};

struct OrderPartiallyFilledEvent {
    OrderId order_id{kNoOrder};
    TraderId trader{kNoTrader};
    Quantity fill_quantity{0};
    Quantity remaining_quantity{0};
    Price execution_price{0};
    Timestamp timestamp{0};
    SequenceNumber sequence{0};
};

struct OrderFilledEvent {
    OrderId order_id{kNoOrder};
    TraderId trader{kNoTrader};
    Quantity fill_quantity{0};
    Price execution_price{0};
    Timestamp timestamp{0};
    SequenceNumber sequence{0};
};

struct OrderCancelledEvent {
    OrderId order_id{kNoOrder};
    TraderId trader{kNoTrader};
    Quantity cancelled_quantity{0};
    Timestamp timestamp{0};
    SequenceNumber sequence{0};
};

struct TradeExecutedEvent {
    Trade trade;
};

using MarketEvent = std::variant<
    OrderAcceptedEvent,
    OrderRejectedEvent,
    OrderPartiallyFilledEvent,
    OrderFilledEvent,
    OrderCancelledEvent,
    TradeExecutedEvent
>;

// Listener interface for receiving real-time deterministic lifecycle notifications
class IEventListener {
public:
    virtual ~IEventListener() = default;
    virtual void on_order_accepted(const OrderAcceptedEvent&) {}
    virtual void on_order_rejected(const OrderRejectedEvent&) {}
    virtual void on_order_partially_filled(const OrderPartiallyFilledEvent&) {}
    virtual void on_order_filled(const OrderFilledEvent&) {}
    virtual void on_order_cancelled(const OrderCancelledEvent&) {}
    virtual void on_trade_executed(const TradeExecutedEvent&) {}
};

}  // namespace tradenex
