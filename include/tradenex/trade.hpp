#pragma once

#include "tradenex/types.hpp"

namespace tradenex {

// Represents an execution between a resting (maker) order and an incoming (taker) order.
// Crucial exchange invariant: Every execution strictly occurs at the resting order's price.
struct Trade {
    TradeId trade_id{kNoTrade};
    OrderId maker_order_id{kNoOrder};
    OrderId taker_order_id{kNoOrder};
    TraderId maker_trader_id{kNoTrader};
    TraderId taker_trader_id{kNoTrader};
    Side aggressor_side{Side::Buy};
    Price price{0};
    Quantity quantity{0};
    Timestamp timestamp{0};
    SequenceNumber sequence{0};

    friend bool operator==(const Trade&, const Trade&) = default;
};

// Summary of an order operation result returned to the caller.
struct OrderResult {
    OrderStatus status{OrderStatus::Rejected};
    RejectReason reject_reason{RejectReason::None};
    Quantity filled_quantity{0};
    Quantity resting_quantity{0};
    Quantity cancelled_quantity{0};

    [[nodiscard]] bool ok() const noexcept {
        return status != OrderStatus::Rejected;
    }
};

}  // namespace tradenex
