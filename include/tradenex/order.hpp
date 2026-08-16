#pragma once

#include <utility>
#include "tradenex/types.hpp"

namespace tradenex {

// Represents an order submitted to the matching engine.
struct OrderRequest {
    OrderId id{kNoOrder};
    TraderId trader{kNoTrader};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    TimeInForce tif{TimeInForce::GTC};
    Price price{0};           // Explicitly ignored for market orders
    Quantity quantity{0};
    Timestamp timestamp{0};

    [[nodiscard]] static OrderRequest limit(OrderId id,
                                            Side side,
                                            Price price,
                                            Quantity quantity,
                                            TimeInForce tif = TimeInForce::GTC,
                                            TraderId trader = kNoTrader,
                                            Timestamp ts = 0) noexcept {
        return OrderRequest{id, trader, side, OrderType::Limit, tif, price, quantity, ts};
    }

    [[nodiscard]] static OrderRequest market(OrderId id,
                                             Side side,
                                             Quantity quantity,
                                             TraderId trader = kNoTrader,
                                             Timestamp ts = 0) noexcept {
        // Market orders execute immediately against existing liquidity (IOC semantics)
        return OrderRequest{id, trader, side, OrderType::Market, TimeInForce::IOC, 0, quantity, ts};
    }
};

// Represents an active or historical order in the exchange domain model.
struct Order {
    OrderId id{kNoOrder};
    TraderId trader{kNoTrader};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    TimeInForce tif{TimeInForce::GTC};
    Price price{0};
    Quantity initial_quantity{0};
    Quantity remaining_quantity{0};
    OrderStatus status{OrderStatus::Accepted};
    Timestamp timestamp{0};
    SequenceNumber sequence{0};

    [[nodiscard]] Quantity filled_quantity() const noexcept {
        return initial_quantity - remaining_quantity;
    }

    [[nodiscard]] bool is_active() const noexcept {
        return status == OrderStatus::Accepted || status == OrderStatus::PartiallyFilled;
    }

    [[nodiscard]] bool is_terminal() const noexcept {
        return status == OrderStatus::Filled || status == OrderStatus::Cancelled || status == OrderStatus::Rejected;
    }

    [[nodiscard]] bool can_cancel() const noexcept {
        return is_active() && remaining_quantity > 0;
    }
};

// Validates whether an incoming order request conforms to protocol invariants.
[[nodiscard]] constexpr std::pair<bool, RejectReason> validate_order_request(const OrderRequest& req) noexcept {
    if (req.id == kNoOrder) {
        return {false, RejectReason::InvalidOrderId};
    }
    if (req.quantity <= 0) {
        return {false, RejectReason::InvalidQuantity};
    }
    if (req.type == OrderType::Limit) {
        if (req.price < kMinPrice || req.price > kMaxPrice) {
            return {false, RejectReason::InvalidPrice};
        }
    }
    return {true, RejectReason::None};
}

}  // namespace tradenex
