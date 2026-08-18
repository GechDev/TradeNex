#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "tradenex/order.hpp"
#include "tradenex/types.hpp"

namespace tradenex {

struct LevelSnapshot {
    Price price{0};
    Quantity quantity{0};
    std::size_t order_count{0};

    friend bool operator==(const LevelSnapshot&, const LevelSnapshot&) = default;
};

struct BookSnapshot {
    std::vector<LevelSnapshot> bids;
    std::vector<LevelSnapshot> asks;
};

struct InvariantCheckResult {
    bool valid{true};
    std::string error_message;
};

// Intrusive node representing a resting order in the book.
struct OrderNode {
    OrderId id{kNoOrder};
    TraderId trader{kNoTrader};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    TimeInForce tif{TimeInForce::GTC};
    Price price{0};
    Quantity initial_quantity{0};
    Quantity remaining_quantity{0};
    Timestamp timestamp{0};
    SequenceNumber sequence{0};

    OrderNode* prev{nullptr};
    OrderNode* next{nullptr};
    void* level_ptr{nullptr};
};

// Simple chunk-based object pool to eliminate heap allocations in the hot path.
class OrderNodePool {
public:
    explicit OrderNodePool(std::size_t initial_capacity = 4096);
    ~OrderNodePool() = default;

    OrderNodePool(const OrderNodePool&) = delete;
    OrderNodePool& operator=(const OrderNodePool&) = delete;

    OrderNode* acquire();
    void release(OrderNode* node);
    [[nodiscard]] std::size_t allocated_count() const noexcept { return in_use_; }

private:
    void allocate_chunk(std::size_t size);

    std::vector<std::unique_ptr<OrderNode[]>> chunks_;
    std::vector<OrderNode*> free_list_;
    std::size_t in_use_{0};
    std::size_t chunk_size_{4096};
};

// A single price level containing an intrusive doubly-linked list of orders in arrival sequence.
struct PriceLevel {
    Price price{0};
    Quantity total_quantity{0};
    std::size_t order_count{0};
    OrderNode* head{nullptr};
    OrderNode* tail{nullptr};

    void append(OrderNode* node) noexcept;
    void remove(OrderNode* node) noexcept;
    [[nodiscard]] bool empty() const noexcept { return order_count == 0; }
};

// Limit Order Book maintaining active resting orders by price-time priority.
class OrderBook {
public:
    explicit OrderBook(std::size_t initial_pool_capacity = 4096);
    ~OrderBook();

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    // Resets the book to an empty state
    void clear();

    // Direct insertion of a validated resting limit order.
    // Callers normally invoke MatchingEngine::submit_order.
    OrderNode* insert_order(const OrderRequest& req, SequenceNumber seq);

    // Removes a resting order by identifier. Returns true if removed, false if not found.
    bool cancel_order(OrderId id);

    // Updates resting order remaining quantity in place (e.g., upon partial fill).
    void decrease_order_quantity(OrderNode* node, Quantity executed_qty);

    // Removes a fully filled resting order node.
    void remove_order_node(OrderNode* node);

    // Order lookup
    [[nodiscard]] OrderNode* find_node(OrderId id) const noexcept;
    [[nodiscard]] std::optional<Order> find_order(OrderId id) const;
    [[nodiscard]] bool contains(OrderId id) const noexcept;

    // Market state queries
    [[nodiscard]] std::optional<Price> best_bid() const noexcept;
    [[nodiscard]] std::optional<Price> best_ask() const noexcept;
    [[nodiscard]] Quantity best_bid_quantity() const noexcept;
    [[nodiscard]] Quantity best_ask_quantity() const noexcept;
    [[nodiscard]] std::optional<Price> spread() const noexcept;
    [[nodiscard]] std::optional<double> mid_price() const noexcept;

    [[nodiscard]] std::size_t count_orders() const noexcept { return order_index_.size(); }
    [[nodiscard]] std::size_t count_levels(Side side) const noexcept;
    [[nodiscard]] Quantity total_volume(Side side) const noexcept;
    [[nodiscard]] Quantity volume_at(Side side, Price price) const noexcept;

    // Read-only snapshots
    [[nodiscard]] std::vector<LevelSnapshot> depth(Side side, std::size_t max_levels = 10) const;
    [[nodiscard]] BookSnapshot snapshot(std::size_t max_levels = 10) const;

    // Price level access for matching engine
    using BidMap = std::map<Price, PriceLevel, std::greater<Price>>;
    using AskMap = std::map<Price, PriceLevel, std::less<Price>>;

    [[nodiscard]] BidMap& bids() noexcept { return bids_; }
    [[nodiscard]] const BidMap& bids() const noexcept { return bids_; }
    [[nodiscard]] AskMap& asks() noexcept { return asks_; }
    [[nodiscard]] const AskMap& asks() const noexcept { return asks_; }

    // Invariant validation
    [[nodiscard]] InvariantCheckResult validate_invariants() const;

private:
    BidMap bids_;
    AskMap asks_;
    std::unordered_map<OrderId, OrderNode*> order_index_;
    OrderNodePool pool_;
};

}  // namespace tradenex
