#include "tradenex/matching_engine.hpp"

#include <algorithm>

namespace tradenex {

MatchingEngine::MatchingEngine(std::size_t initial_pool_capacity)
    : book_(initial_pool_capacity) {}

void MatchingEngine::add_listener(IEventListener* listener) {
    if (listener != nullptr) {
        listeners_.push_back(listener);
    }
}

void MatchingEngine::remove_listener(IEventListener* listener) {
    std::erase(listeners_, listener);
}

void MatchingEngine::reset() {
    book_.clear();
    sequence_ = 0;
    trade_id_ = 0;
    trade_history_.clear();
}

OrderResult MatchingEngine::submit_order(const OrderRequest& req) {
    std::vector<Trade> dummy;
    return submit_order(req, dummy);
}

OrderResult MatchingEngine::submit_order(const OrderRequest& req, std::vector<Trade>& out_trades) {
    auto [valid, reason] = validate_order_request(req);
    if (!valid) {
        notify_order_rejected(OrderRejectedEvent{
            .order_id = req.id,
            .trader = req.trader,
            .reason = reason,
            .timestamp = req.timestamp,
            .sequence = ++sequence_
        });
        return OrderResult{OrderStatus::Rejected, reason, 0, 0, 0};
    }

    if (book_.contains(req.id)) {
        notify_order_rejected(OrderRejectedEvent{
            .order_id = req.id,
            .trader = req.trader,
            .reason = RejectReason::DuplicateOrderId,
            .timestamp = req.timestamp,
            .sequence = ++sequence_
        });
        return OrderResult{OrderStatus::Rejected, RejectReason::DuplicateOrderId, 0, 0, 0};
    }

    if (req.type == OrderType::Market) {
        return req.side == Side::Buy ? match_buy_market(req, out_trades) : match_sell_market(req, out_trades);
    }

    return req.side == Side::Buy ? match_buy_limit(req, out_trades) : match_sell_limit(req, out_trades);
}

bool MatchingEngine::check_fok_liquidity(Side side, Price limit_price, Quantity required_qty) const {
    Quantity available = 0;
    if (side == Side::Buy) {
        for (const auto& [price, level] : book_.asks()) {
            if (price > limit_price) break;
            available += level.total_quantity;
            if (available >= required_qty) return true;
        }
    } else {
        for (const auto& [price, level] : book_.bids()) {
            if (price < limit_price) break;
            available += level.total_quantity;
            if (available >= required_qty) return true;
        }
    }
    return false;
}

OrderResult MatchingEngine::match_buy_limit(const OrderRequest& req, std::vector<Trade>& out_trades) {
    if (req.tif == TimeInForce::FOK && !check_fok_liquidity(Side::Buy, req.price, req.quantity)) {
        notify_order_cancelled(OrderCancelledEvent{
            .order_id = req.id,
            .trader = req.trader,
            .cancelled_quantity = req.quantity,
            .timestamp = req.timestamp,
            .sequence = ++sequence_
        });
        return OrderResult{OrderStatus::Cancelled, RejectReason::None, 0, 0, req.quantity};
    }

    Quantity remaining = req.quantity;
    Quantity filled = 0;

    while (remaining > 0 && !book_.asks().empty()) {
        auto ask_it = book_.asks().begin();
        if (ask_it->first > req.price) {
            break;  // Best ask is above limit price
        }

        PriceLevel& level = ask_it->second;
        OrderNode* maker_node = level.head;
        Quantity match_qty = std::min(remaining, maker_node->remaining_quantity);
        Price exec_price = maker_node->price;

        Trade trade{
            .trade_id = ++trade_id_,
            .maker_order_id = maker_node->id,
            .taker_order_id = req.id,
            .maker_trader_id = maker_node->trader,
            .taker_trader_id = req.trader,
            .aggressor_side = Side::Buy,
            .price = exec_price,
            .quantity = match_qty,
            .timestamp = req.timestamp,
            .sequence = ++sequence_
        };

        trade_history_.push_back(trade);
        out_trades.push_back(trade);
        notify_trade_executed(TradeExecutedEvent{.trade = trade});

        remaining -= match_qty;
        filled += match_qty;

        if (match_qty == maker_node->remaining_quantity) {
            notify_order_filled(OrderFilledEvent{
                .order_id = maker_node->id,
                .trader = maker_node->trader,
                .fill_quantity = match_qty,
                .execution_price = exec_price,
                .timestamp = req.timestamp,
                .sequence = ++sequence_
            });
            book_.remove_order_node(maker_node);
        } else {
            book_.decrease_order_quantity(maker_node, match_qty);
            notify_order_partially_filled(OrderPartiallyFilledEvent{
                .order_id = maker_node->id,
                .trader = maker_node->trader,
                .fill_quantity = match_qty,
                .remaining_quantity = maker_node->remaining_quantity,
                .execution_price = exec_price,
                .timestamp = req.timestamp,
                .sequence = ++sequence_
            });
        }
    }

    if (filled > 0) {
        if (remaining > 0) {
            notify_order_partially_filled(OrderPartiallyFilledEvent{
                .order_id = req.id,
                .trader = req.trader,
                .fill_quantity = filled,
                .remaining_quantity = remaining,
                .execution_price = out_trades.back().price,
                .timestamp = req.timestamp,
                .sequence = ++sequence_
            });
        } else {
            notify_order_filled(OrderFilledEvent{
                .order_id = req.id,
                .trader = req.trader,
                .fill_quantity = filled,
                .execution_price = out_trades.back().price,
                .timestamp = req.timestamp,
                .sequence = ++sequence_
            });
        }
    }

    if (remaining > 0) {
        if (req.tif == TimeInForce::GTC) {
            OrderRequest resting_req = req;
            resting_req.quantity = remaining;
            book_.insert_order(resting_req, ++sequence_);

            if (filled == 0) {
                notify_order_accepted(OrderAcceptedEvent{
                    .order_id = req.id,
                    .trader = req.trader,
                    .side = req.side,
                    .price = req.price,
                    .quantity = remaining,
                    .timestamp = req.timestamp,
                    .sequence = sequence_
                });
                return OrderResult{OrderStatus::Accepted, RejectReason::None, 0, remaining, 0};
            }
            return OrderResult{OrderStatus::PartiallyFilled, RejectReason::None, filled, remaining, 0};
        } else {
            // IOC: cancel unfilled remainder
            notify_order_cancelled(OrderCancelledEvent{
                .order_id = req.id,
                .trader = req.trader,
                .cancelled_quantity = remaining,
                .timestamp = req.timestamp,
                .sequence = ++sequence_
            });
            return OrderResult{
                filled > 0 ? OrderStatus::PartiallyFilled : OrderStatus::Cancelled,
                RejectReason::None,
                filled,
                0,
                remaining
            };
        }
    }

    return OrderResult{OrderStatus::Filled, RejectReason::None, filled, 0, 0};
}

OrderResult MatchingEngine::match_sell_limit(const OrderRequest& req, std::vector<Trade>& out_trades) {
    if (req.tif == TimeInForce::FOK && !check_fok_liquidity(Side::Sell, req.price, req.quantity)) {
        notify_order_cancelled(OrderCancelledEvent{
            .order_id = req.id,
            .trader = req.trader,
            .cancelled_quantity = req.quantity,
            .timestamp = req.timestamp,
            .sequence = ++sequence_
        });
        return OrderResult{OrderStatus::Cancelled, RejectReason::None, 0, 0, req.quantity};
    }

    Quantity remaining = req.quantity;
    Quantity filled = 0;

    while (remaining > 0 && !book_.bids().empty()) {
        auto bid_it = book_.bids().begin();
        if (bid_it->first < req.price) {
            break;  // Best bid is below limit price
        }

        PriceLevel& level = bid_it->second;
        OrderNode* maker_node = level.head;
        Quantity match_qty = std::min(remaining, maker_node->remaining_quantity);
        Price exec_price = maker_node->price;

        Trade trade{
            .trade_id = ++trade_id_,
            .maker_order_id = maker_node->id,
            .taker_order_id = req.id,
            .maker_trader_id = maker_node->trader,
            .taker_trader_id = req.trader,
            .aggressor_side = Side::Sell,
            .price = exec_price,
            .quantity = match_qty,
            .timestamp = req.timestamp,
            .sequence = ++sequence_
        };

        trade_history_.push_back(trade);
        out_trades.push_back(trade);
        notify_trade_executed(TradeExecutedEvent{.trade = trade});

        remaining -= match_qty;
        filled += match_qty;

        if (match_qty == maker_node->remaining_quantity) {
            notify_order_filled(OrderFilledEvent{
                .order_id = maker_node->id,
                .trader = maker_node->trader,
                .fill_quantity = match_qty,
                .execution_price = exec_price,
                .timestamp = req.timestamp,
                .sequence = ++sequence_
            });
            book_.remove_order_node(maker_node);
        } else {
            book_.decrease_order_quantity(maker_node, match_qty);
            notify_order_partially_filled(OrderPartiallyFilledEvent{
                .order_id = maker_node->id,
                .trader = maker_node->trader,
                .fill_quantity = match_qty,
                .remaining_quantity = maker_node->remaining_quantity,
                .execution_price = exec_price,
                .timestamp = req.timestamp,
                .sequence = ++sequence_
            });
        }
    }

    if (filled > 0) {
        if (remaining > 0) {
            notify_order_partially_filled(OrderPartiallyFilledEvent{
                .order_id = req.id,
                .trader = req.trader,
                .fill_quantity = filled,
                .remaining_quantity = remaining,
                .execution_price = out_trades.back().price,
                .timestamp = req.timestamp,
                .sequence = ++sequence_
            });
        } else {
            notify_order_filled(OrderFilledEvent{
                .order_id = req.id,
                .trader = req.trader,
                .fill_quantity = filled,
                .execution_price = out_trades.back().price,
                .timestamp = req.timestamp,
                .sequence = ++sequence_
            });
        }
    }

    if (remaining > 0) {
        if (req.tif == TimeInForce::GTC) {
            OrderRequest resting_req = req;
            resting_req.quantity = remaining;
            book_.insert_order(resting_req, ++sequence_);

            if (filled == 0) {
                notify_order_accepted(OrderAcceptedEvent{
                    .order_id = req.id,
                    .trader = req.trader,
                    .side = req.side,
                    .price = req.price,
                    .quantity = remaining,
                    .timestamp = req.timestamp,
                    .sequence = sequence_
                });
                return OrderResult{OrderStatus::Accepted, RejectReason::None, 0, remaining, 0};
            }
            return OrderResult{OrderStatus::PartiallyFilled, RejectReason::None, filled, remaining, 0};
        } else {
            // IOC: cancel remainder
            notify_order_cancelled(OrderCancelledEvent{
                .order_id = req.id,
                .trader = req.trader,
                .cancelled_quantity = remaining,
                .timestamp = req.timestamp,
                .sequence = ++sequence_
            });
            return OrderResult{
                filled > 0 ? OrderStatus::PartiallyFilled : OrderStatus::Cancelled,
                RejectReason::None,
                filled,
                0,
                remaining
            };
        }
    }

    return OrderResult{OrderStatus::Filled, RejectReason::None, filled, 0, 0};
}

OrderResult MatchingEngine::match_buy_market(const OrderRequest& req, std::vector<Trade>& out_trades) {
    Quantity remaining = req.quantity;
    Quantity filled = 0;

    while (remaining > 0 && !book_.asks().empty()) {
        auto ask_it = book_.asks().begin();
        PriceLevel& level = ask_it->second;
        OrderNode* maker_node = level.head;
        Quantity match_qty = std::min(remaining, maker_node->remaining_quantity);
        Price exec_price = maker_node->price;

        Trade trade{
            .trade_id = ++trade_id_,
            .maker_order_id = maker_node->id,
            .taker_order_id = req.id,
            .maker_trader_id = maker_node->trader,
            .taker_trader_id = req.trader,
            .aggressor_side = Side::Buy,
            .price = exec_price,
            .quantity = match_qty,
            .timestamp = req.timestamp,
            .sequence = ++sequence_
        };

        trade_history_.push_back(trade);
        out_trades.push_back(trade);
        notify_trade_executed(TradeExecutedEvent{.trade = trade});

        remaining -= match_qty;
        filled += match_qty;

        if (match_qty == maker_node->remaining_quantity) {
            notify_order_filled(OrderFilledEvent{
                .order_id = maker_node->id,
                .trader = maker_node->trader,
                .fill_quantity = match_qty,
                .execution_price = exec_price,
                .timestamp = req.timestamp,
                .sequence = ++sequence_
            });
            book_.remove_order_node(maker_node);
        } else {
            book_.decrease_order_quantity(maker_node, match_qty);
            notify_order_partially_filled(OrderPartiallyFilledEvent{
                .order_id = maker_node->id,
                .trader = maker_node->trader,
                .fill_quantity = match_qty,
                .remaining_quantity = maker_node->remaining_quantity,
                .execution_price = exec_price,
                .timestamp = req.timestamp,
                .sequence = ++sequence_
            });
        }
    }

    if (filled > 0) {
        if (remaining > 0) {
            notify_order_partially_filled(OrderPartiallyFilledEvent{
                .order_id = req.id,
                .trader = req.trader,
                .fill_quantity = filled,
                .remaining_quantity = remaining,
                .execution_price = out_trades.back().price,
                .timestamp = req.timestamp,
                .sequence = ++sequence_
            });
        } else {
            notify_order_filled(OrderFilledEvent{
                .order_id = req.id,
                .trader = req.trader,
                .fill_quantity = filled,
                .execution_price = out_trades.back().price,
                .timestamp = req.timestamp,
                .sequence = ++sequence_
            });
        }
    }

    // Market order unfilled remainder is NEVER rested on the book!
    if (remaining > 0) {
        notify_order_cancelled(OrderCancelledEvent{
            .order_id = req.id,
            .trader = req.trader,
            .cancelled_quantity = remaining,
            .timestamp = req.timestamp,
            .sequence = ++sequence_
        });
        return OrderResult{
            filled > 0 ? OrderStatus::PartiallyFilled : OrderStatus::Cancelled,
            RejectReason::None,
            filled,
            0,
            remaining
        };
    }

    return OrderResult{OrderStatus::Filled, RejectReason::None, filled, 0, 0};
}

OrderResult MatchingEngine::match_sell_market(const OrderRequest& req, std::vector<Trade>& out_trades) {
    Quantity remaining = req.quantity;
    Quantity filled = 0;

    while (remaining > 0 && !book_.bids().empty()) {
        auto bid_it = book_.bids().begin();
        PriceLevel& level = bid_it->second;
        OrderNode* maker_node = level.head;
        Quantity match_qty = std::min(remaining, maker_node->remaining_quantity);
        Price exec_price = maker_node->price;

        Trade trade{
            .trade_id = ++trade_id_,
            .maker_order_id = maker_node->id,
            .taker_order_id = req.id,
            .maker_trader_id = maker_node->trader,
            .taker_trader_id = req.trader,
            .aggressor_side = Side::Sell,
            .price = exec_price,
            .quantity = match_qty,
            .timestamp = req.timestamp,
            .sequence = ++sequence_
        };

        trade_history_.push_back(trade);
        out_trades.push_back(trade);
        notify_trade_executed(TradeExecutedEvent{.trade = trade});

        remaining -= match_qty;
        filled += match_qty;

        if (match_qty == maker_node->remaining_quantity) {
            notify_order_filled(OrderFilledEvent{
                .order_id = maker_node->id,
                .trader = maker_node->trader,
                .fill_quantity = match_qty,
                .execution_price = exec_price,
                .timestamp = req.timestamp,
                .sequence = ++sequence_
            });
            book_.remove_order_node(maker_node);
        } else {
            book_.decrease_order_quantity(maker_node, match_qty);
            notify_order_partially_filled(OrderPartiallyFilledEvent{
                .order_id = maker_node->id,
                .trader = maker_node->trader,
                .fill_quantity = match_qty,
                .remaining_quantity = maker_node->remaining_quantity,
                .execution_price = exec_price,
                .timestamp = req.timestamp,
                .sequence = ++sequence_
            });
        }
    }

    if (filled > 0) {
        if (remaining > 0) {
            notify_order_partially_filled(OrderPartiallyFilledEvent{
                .order_id = req.id,
                .trader = req.trader,
                .fill_quantity = filled,
                .remaining_quantity = remaining,
                .execution_price = out_trades.back().price,
                .timestamp = req.timestamp,
                .sequence = ++sequence_
            });
        } else {
            notify_order_filled(OrderFilledEvent{
                .order_id = req.id,
                .trader = req.trader,
                .fill_quantity = filled,
                .execution_price = out_trades.back().price,
                .timestamp = req.timestamp,
                .sequence = ++sequence_
            });
        }
    }

    // Market order unfilled remainder is cancelled
    if (remaining > 0) {
        notify_order_cancelled(OrderCancelledEvent{
            .order_id = req.id,
            .trader = req.trader,
            .cancelled_quantity = remaining,
            .timestamp = req.timestamp,
            .sequence = ++sequence_
        });
        return OrderResult{
            filled > 0 ? OrderStatus::PartiallyFilled : OrderStatus::Cancelled,
            RejectReason::None,
            filled,
            0,
            remaining
        };
    }

    return OrderResult{OrderStatus::Filled, RejectReason::None, filled, 0, 0};
}

OrderResult MatchingEngine::cancel_order(OrderId id) {
    OrderNode* node = book_.find_node(id);
    if (node == nullptr) {
        notify_order_rejected(OrderRejectedEvent{
            .order_id = id,
            .trader = kNoTrader,
            .reason = RejectReason::UnknownOrder,
            .timestamp = 0,
            .sequence = ++sequence_
        });
        return OrderResult{OrderStatus::Rejected, RejectReason::UnknownOrder, 0, 0, 0};
    }

    Quantity cancelled_qty = node->remaining_quantity;
    TraderId trader = node->trader;

    book_.cancel_order(id);

    notify_order_cancelled(OrderCancelledEvent{
        .order_id = id,
        .trader = trader,
        .cancelled_quantity = cancelled_qty,
        .timestamp = 0,
        .sequence = ++sequence_
    });

    return OrderResult{OrderStatus::Cancelled, RejectReason::None, 0, 0, cancelled_qty};
}

void MatchingEngine::notify_order_accepted(const OrderAcceptedEvent& ev) {
    for (auto* l : listeners_) l->on_order_accepted(ev);
}

void MatchingEngine::notify_order_rejected(const OrderRejectedEvent& ev) {
    for (auto* l : listeners_) l->on_order_rejected(ev);
}

void MatchingEngine::notify_order_partially_filled(const OrderPartiallyFilledEvent& ev) {
    for (auto* l : listeners_) l->on_order_partially_filled(ev);
}

void MatchingEngine::notify_order_filled(const OrderFilledEvent& ev) {
    for (auto* l : listeners_) l->on_order_filled(ev);
}

void MatchingEngine::notify_order_cancelled(const OrderCancelledEvent& ev) {
    for (auto* l : listeners_) l->on_order_cancelled(ev);
}

void MatchingEngine::notify_trade_executed(const TradeExecutedEvent& ev) {
    for (auto* l : listeners_) l->on_trade_executed(ev);
}

}  // namespace tradenex
