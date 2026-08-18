#include "tradenex/order_book.hpp"

#include <algorithm>
#include <sstream>

namespace tradenex {

void PriceLevel::append(OrderNode* node) noexcept {
    node->prev = tail;
    node->next = nullptr;
    node->level_ptr = this;

    if (tail != nullptr) {
        tail->next = node;
    } else {
        head = node;
    }
    tail = node;

    total_quantity += node->remaining_quantity;
    ++order_count;
}

void PriceLevel::remove(OrderNode* node) noexcept {
    if (node->prev != nullptr) {
        node->prev->next = node->next;
    } else {
        head = node->next;
    }

    if (node->next != nullptr) {
        node->next->prev = node->prev;
    } else {
        tail = node->prev;
    }

    total_quantity -= node->remaining_quantity;
    --order_count;

    node->prev = nullptr;
    node->next = nullptr;
    node->level_ptr = nullptr;
}

OrderNodePool::OrderNodePool(std::size_t initial_capacity)
    : chunk_size_(std::max<std::size_t>(initial_capacity, 256)) {
    allocate_chunk(chunk_size_);
}

void OrderNodePool::allocate_chunk(std::size_t size) {
    auto chunk = std::make_unique<OrderNode[]>(size);
    free_list_.reserve(free_list_.size() + size);
    for (std::size_t i = 0; i < size; ++i) {
        free_list_.push_back(&chunk[i]);
    }
    chunks_.push_back(std::move(chunk));
}

OrderNode* OrderNodePool::acquire() {
    if (free_list_.empty()) {
        allocate_chunk(chunk_size_);
    }
    OrderNode* node = free_list_.back();
    free_list_.pop_back();
    ++in_use_;
    *node = OrderNode{};
    return node;
}

void OrderNodePool::release(OrderNode* node) {
    if (node == nullptr) return;
    free_list_.push_back(node);
    if (in_use_ > 0) {
        --in_use_;
    }
}

OrderBook::OrderBook(std::size_t initial_pool_capacity)
    : pool_(initial_pool_capacity) {}

OrderBook::~OrderBook() = default;

void OrderBook::clear() {
    bids_.clear();
    asks_.clear();
    order_index_.clear();
    // pool_ can continue serving without reallocating
}

OrderNode* OrderBook::insert_order(const OrderRequest& req, SequenceNumber seq) {
    OrderNode* node = pool_.acquire();
    node->id = req.id;
    node->trader = req.trader;
    node->side = req.side;
    node->type = req.type;
    node->tif = req.tif;
    node->price = req.price;
    node->initial_quantity = req.quantity;
    node->remaining_quantity = req.quantity;
    node->timestamp = req.timestamp;
    node->sequence = seq;

    order_index_[node->id] = node;

    if (node->side == Side::Buy) {
        auto [it, inserted] = bids_.try_emplace(node->price, PriceLevel{node->price, 0, 0, nullptr, nullptr});
        it->second.append(node);
    } else {
        auto [it, inserted] = asks_.try_emplace(node->price, PriceLevel{node->price, 0, 0, nullptr, nullptr});
        it->second.append(node);
    }

    return node;
}

bool OrderBook::cancel_order(OrderId id) {
    auto it = order_index_.find(id);
    if (it == order_index_.end()) {
        return false;
    }

    OrderNode* node = it->second;
    order_index_.erase(it);

    auto* level = static_cast<PriceLevel*>(node->level_ptr);
    if (level != nullptr) {
        level->remove(node);
        if (level->empty()) {
            if (node->side == Side::Buy) {
                bids_.erase(level->price);
            } else {
                asks_.erase(level->price);
            }
        }
    }

    pool_.release(node);
    return true;
}

void OrderBook::decrease_order_quantity(OrderNode* node, Quantity executed_qty) {
    if (node == nullptr) return;
    node->remaining_quantity -= executed_qty;
    auto* level = static_cast<PriceLevel*>(node->level_ptr);
    if (level != nullptr) {
        level->total_quantity -= executed_qty;
    }
}

void OrderBook::remove_order_node(OrderNode* node) {
    if (node == nullptr) return;

    order_index_.erase(node->id);

    auto* level = static_cast<PriceLevel*>(node->level_ptr);
    if (level != nullptr) {
        level->remove(node);
        if (level->empty()) {
            if (node->side == Side::Buy) {
                bids_.erase(level->price);
            } else {
                asks_.erase(level->price);
            }
        }
    }

    pool_.release(node);
}

OrderNode* OrderBook::find_node(OrderId id) const noexcept {
    auto it = order_index_.find(id);
    if (it != order_index_.end()) {
        return it->second;
    }
    return nullptr;
}

std::optional<Order> OrderBook::find_order(OrderId id) const {
    OrderNode* node = find_node(id);
    if (node == nullptr) {
        return std::nullopt;
    }
    return Order{
        node->id,
        node->trader,
        node->side,
        node->type,
        node->tif,
        node->price,
        node->initial_quantity,
        node->remaining_quantity,
        node->remaining_quantity == node->initial_quantity ? OrderStatus::Accepted : OrderStatus::PartiallyFilled,
        node->timestamp,
        node->sequence
    };
}

bool OrderBook::contains(OrderId id) const noexcept {
    return order_index_.contains(id);
}

std::optional<Price> OrderBook::best_bid() const noexcept {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const noexcept {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

Quantity OrderBook::best_bid_quantity() const noexcept {
    if (bids_.empty()) return 0;
    return bids_.begin()->second.total_quantity;
}

Quantity OrderBook::best_ask_quantity() const noexcept {
    if (asks_.empty()) return 0;
    return asks_.begin()->second.total_quantity;
}

std::optional<Price> OrderBook::spread() const noexcept {
    auto bb = best_bid();
    auto ba = best_ask();
    if (bb && ba) {
        return *ba - *bb;
    }
    return std::nullopt;
}

std::optional<double> OrderBook::mid_price() const noexcept {
    auto bb = best_bid();
    auto ba = best_ask();
    if (bb && ba) {
        return static_cast<double>(*bb + *ba) / 2.0;
    }
    return std::nullopt;
}

std::size_t OrderBook::count_levels(Side side) const noexcept {
    return side == Side::Buy ? bids_.size() : asks_.size();
}

Quantity OrderBook::total_volume(Side side) const noexcept {
    Quantity total = 0;
    if (side == Side::Buy) {
        for (const auto& [price, level] : bids_) {
            total += level.total_quantity;
        }
    } else {
        for (const auto& [price, level] : asks_) {
            total += level.total_quantity;
        }
    }
    return total;
}

Quantity OrderBook::volume_at(Side side, Price price) const noexcept {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        return it != bids_.end() ? it->second.total_quantity : 0;
    } else {
        auto it = asks_.find(price);
        return it != asks_.end() ? it->second.total_quantity : 0;
    }
}

std::vector<LevelSnapshot> OrderBook::depth(Side side, std::size_t max_levels) const {
    std::vector<LevelSnapshot> result;
    result.reserve(max_levels);

    if (side == Side::Buy) {
        for (const auto& [price, level] : bids_) {
            if (result.size() >= max_levels) break;
            result.push_back({level.price, level.total_quantity, level.order_count});
        }
    } else {
        for (const auto& [price, level] : asks_) {
            if (result.size() >= max_levels) break;
            result.push_back({level.price, level.total_quantity, level.order_count});
        }
    }

    return result;
}

BookSnapshot OrderBook::snapshot(std::size_t max_levels) const {
    return BookSnapshot{
        .bids = depth(Side::Buy, max_levels),
        .asks = depth(Side::Sell, max_levels)
    };
}

InvariantCheckResult OrderBook::validate_invariants() const {
    // 1. Cross book check
    auto bb = best_bid();
    auto ba = best_ask();
    if (bb && ba && *bb >= *ba) {
        std::ostringstream os;
        os << "Book crossed: Best Bid (" << *bb << ") >= Best Ask (" << *ba << ")";
        return {false, os.str()};
    }

    std::size_t total_orders_found = 0;

    // 2. Validate Bid Levels
    Price prev_bid = 0;
    bool first_bid = true;
    for (const auto& [price, level] : bids_) {
        if (!first_bid && price >= prev_bid) {
            return {false, "Bid levels out of descending order"};
        }
        prev_bid = price;
        first_bid = false;

        if (level.empty()) {
            return {false, "Empty bid price level retained in book"};
        }
        if (level.price != price) {
            return {false, "Bid price level key does not match level.price"};
        }

        Quantity sum_qty = 0;
        std::size_t order_count = 0;
        for (const OrderNode* curr = level.head; curr != nullptr; curr = curr->next) {
            if (curr->side != Side::Buy) {
                return {false, "Non-buy order found in bid price level"};
            }
            if (curr->price != price) {
                return {false, "Order price does not match its bid level price"};
            }
            if (curr->remaining_quantity <= 0) {
                return {false, "Resting order has non-positive remaining quantity"};
            }
            auto it = order_index_.find(curr->id);
            if (it == order_index_.end() || it->second != curr) {
                return {false, "Order node in bid level is not indexed or mismatched in index"};
            }
            sum_qty += curr->remaining_quantity;
            ++order_count;
        }

        if (sum_qty != level.total_quantity) {
            return {false, "Bid level total_quantity does not match sum of order quantities"};
        }
        if (order_count != level.order_count) {
            return {false, "Bid level order_count does not match linked list count"};
        }
        total_orders_found += order_count;
    }

    // 3. Validate Ask Levels
    Price prev_ask = 0;
    bool first_ask = true;
    for (const auto& [price, level] : asks_) {
        if (!first_ask && price <= prev_ask) {
            return {false, "Ask levels out of ascending order"};
        }
        prev_ask = price;
        first_ask = false;

        if (level.empty()) {
            return {false, "Empty ask price level retained in book"};
        }
        if (level.price != price) {
            return {false, "Ask price level key does not match level.price"};
        }

        Quantity sum_qty = 0;
        std::size_t order_count = 0;
        for (const OrderNode* curr = level.head; curr != nullptr; curr = curr->next) {
            if (curr->side != Side::Sell) {
                return {false, "Non-sell order found in ask price level"};
            }
            if (curr->price != price) {
                return {false, "Order price does not match its ask level price"};
            }
            if (curr->remaining_quantity <= 0) {
                return {false, "Resting order has non-positive remaining quantity"};
            }
            auto it = order_index_.find(curr->id);
            if (it == order_index_.end() || it->second != curr) {
                return {false, "Order node in ask level is not indexed or mismatched in index"};
            }
            sum_qty += curr->remaining_quantity;
            ++order_count;
        }

        if (sum_qty != level.total_quantity) {
            return {false, "Ask level total_quantity does not match sum of order quantities"};
        }
        if (order_count != level.order_count) {
            return {false, "Ask level order_count does not match linked list count"};
        }
        total_orders_found += order_count;
    }

    // 4. Validate order index size
    if (order_index_.size() != total_orders_found) {
        return {false, "Order index size does not equal total active orders in book"};
    }

    return {true, ""};
}

}  // namespace tradenex
