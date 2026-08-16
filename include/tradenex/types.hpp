#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace tradenex {

// Prices are integer ticks. Converting to currency is a presentation concern
// (price * tick_size) and never happens inside the exchange core, ensuring
// deterministic matching free of floating-point inaccuracies.
using Price = std::int64_t;
using Quantity = std::int64_t;
using OrderId = std::uint64_t;
using TraderId = std::uint32_t;
using TradeId = std::uint64_t;
using SequenceNumber = std::uint64_t;
using Timestamp = std::uint64_t;

inline constexpr OrderId kNoOrder = 0;
inline constexpr TraderId kNoTrader = 0;
inline constexpr TradeId kNoTrade = 0;
inline constexpr Price kMinPrice = 1;
inline constexpr Price kMaxPrice = 1'000'000'000;

enum class Side : std::uint8_t {
    Buy = 0,
    Sell = 1
};

enum class OrderType : std::uint8_t {
    Limit = 0,
    Market = 1
};

enum class TimeInForce : std::uint8_t {
    GTC = 0,  // Good 'Til Cancelled: remainder rests on the book
    IOC = 1,  // Immediate Or Cancel: fill available immediately, cancel unfilled remainder
    FOK = 2   // Fill Or Kill: fill entirely immediately or cancel entire order with 0 fills
};

enum class OrderStatus : std::uint8_t {
    Accepted = 0,         // Resting in book without fills
    PartiallyFilled = 1,  // Partially executed, remainder rests in book
    Filled = 2,           // Fully executed
    Cancelled = 3,        // Remainder cancelled (or market/IOC unfilled remainder)
    Rejected = 4          // Validation failed, book state completely unmodified
};

enum class RejectReason : std::uint8_t {
    None = 0,
    InvalidOrderId,
    DuplicateOrderId,
    InvalidQuantity,
    InvalidPrice,
    UnknownOrder,
    OrderInactive,
    InsufficientLiquidity,
    BookCrossed,
    MalformedInput
};

[[nodiscard]] constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

[[nodiscard]] constexpr int sign(Side s) noexcept {
    return s == Side::Buy ? 1 : -1;
}

[[nodiscard]] constexpr std::string_view to_string(Side s) noexcept {
    return s == Side::Buy ? "BUY" : "SELL";
}

[[nodiscard]] constexpr std::string_view to_string(OrderType t) noexcept {
    return t == OrderType::Limit ? "LIMIT" : "MARKET";
}

[[nodiscard]] constexpr std::string_view to_string(TimeInForce tif) noexcept {
    switch (tif) {
        case TimeInForce::GTC: return "GTC";
        case TimeInForce::IOC: return "IOC";
        case TimeInForce::FOK: return "FOK";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view to_string(OrderStatus status) noexcept {
    switch (status) {
        case OrderStatus::Accepted: return "ACCEPTED";
        case OrderStatus::PartiallyFilled: return "PARTIALLY_FILLED";
        case OrderStatus::Filled: return "FILLED";
        case OrderStatus::Cancelled: return "CANCELLED";
        case OrderStatus::Rejected: return "REJECTED";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view to_string(RejectReason reason) noexcept {
    switch (reason) {
        case RejectReason::None: return "NONE";
        case RejectReason::InvalidOrderId: return "INVALID_ORDER_ID";
        case RejectReason::DuplicateOrderId: return "DUPLICATE_ORDER_ID";
        case RejectReason::InvalidQuantity: return "INVALID_QUANTITY";
        case RejectReason::InvalidPrice: return "INVALID_PRICE";
        case RejectReason::UnknownOrder: return "UNKNOWN_ORDER";
        case RejectReason::OrderInactive: return "ORDER_INACTIVE";
        case RejectReason::InsufficientLiquidity: return "INSUFFICIENT_LIQUIDITY";
        case RejectReason::BookCrossed: return "BOOK_CROSSED";
        case RejectReason::MalformedInput: return "MALFORMED_INPUT";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::optional<Side> parse_side(std::string_view s) noexcept {
    if (s == "BUY" || s == "Buy" || s == "buy" || s == "B" || s == "b") return Side::Buy;
    if (s == "SELL" || s == "Sell" || s == "sell" || s == "S" || s == "s") return Side::Sell;
    return std::nullopt;
}

[[nodiscard]] constexpr std::optional<OrderType> parse_order_type(std::string_view s) noexcept {
    if (s == "LIMIT" || s == "Limit" || s == "limit" || s == "L" || s == "l") return OrderType::Limit;
    if (s == "MARKET" || s == "Market" || s == "market" || s == "M" || s == "m") return OrderType::Market;
    return std::nullopt;
}

}  // namespace tradenex
