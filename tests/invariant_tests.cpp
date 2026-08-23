#include "test_framework.hpp"
#include "tradenex/matching_engine.hpp"

#include <random>

using namespace tradenex;

TEST(InvariantTest, DuplicateOrderIdRejection) {
    MatchingEngine engine;
    auto res1 = engine.submit_order(OrderRequest::limit(1, Side::Buy, 10000, 50));
    EXPECT_EQ(res1.status, OrderStatus::Accepted);

    // Duplicate order ID #1
    auto res2 = engine.submit_order(OrderRequest::limit(1, Side::Sell, 10050, 20));
    EXPECT_EQ(res2.status, OrderStatus::Rejected);
    EXPECT_EQ(res2.reject_reason, RejectReason::DuplicateOrderId);

    // Verify book state was completely preserved
    EXPECT_EQ(engine.book().count_orders(), 1);
    EXPECT_EQ(engine.book().best_bid(), 10000);
    EXPECT_EQ(engine.book().best_bid_quantity(), 50);
    EXPECT_FALSE(engine.book().best_ask().has_value());
    EXPECT_TRUE(engine.book().validate_invariants().valid);
}

TEST(InvariantTest, InvalidPricesAndQuantities) {
    MatchingEngine engine;
    // Pre-populate with order 1
    engine.submit_order(OrderRequest::limit(1, Side::Buy, 10000, 50));

    // Zero quantity
    auto res1 = engine.submit_order(OrderRequest::limit(2, Side::Buy, 10000, 0));
    EXPECT_EQ(res1.status, OrderStatus::Rejected);
    EXPECT_EQ(res1.reject_reason, RejectReason::InvalidQuantity);

    // Negative quantity
    auto res2 = engine.submit_order(OrderRequest::limit(3, Side::Buy, 10000, -10));
    EXPECT_EQ(res2.status, OrderStatus::Rejected);
    EXPECT_EQ(res2.reject_reason, RejectReason::InvalidQuantity);

    // Zero price for limit order
    auto res3 = engine.submit_order(OrderRequest::limit(4, Side::Buy, 0, 10));
    EXPECT_EQ(res3.status, OrderStatus::Rejected);
    EXPECT_EQ(res3.reject_reason, RejectReason::InvalidPrice);

    // Zero order ID
    auto res4 = engine.submit_order(OrderRequest::limit(0, Side::Buy, 10000, 10));
    EXPECT_EQ(res4.status, OrderStatus::Rejected);
    EXPECT_EQ(res4.reject_reason, RejectReason::InvalidOrderId);

    // Book must remain identical to before invalid submissions
    EXPECT_EQ(engine.book().count_orders(), 1);
    EXPECT_EQ(engine.book().best_bid(), 10000);
    EXPECT_TRUE(engine.book().validate_invariants().valid);
}

TEST(InvariantTest, DeterministicTradeIdsAndSequence) {
    MatchingEngine engine1;
    MatchingEngine engine2;

    auto run_sequence = [](MatchingEngine& eng) {
        eng.submit_order(OrderRequest::limit(1, Side::Sell, 10000, 20));
        eng.submit_order(OrderRequest::limit(2, Side::Sell, 10000, 30));
        eng.submit_order(OrderRequest::limit(3, Side::Buy, 10000, 25));
        eng.submit_order(OrderRequest::limit(4, Side::Buy, 10000, 30));
    };

    run_sequence(engine1);
    run_sequence(engine2);

    ASSERT_EQ(engine1.trade_history().size(), engine2.trade_history().size());
    for (std::size_t i = 0; i < engine1.trade_history().size(); ++i) {
        EXPECT_EQ(engine1.trade_history()[i], engine2.trade_history()[i]);
    }
    EXPECT_EQ(engine1.current_sequence(), engine2.current_sequence());
    EXPECT_EQ(engine1.current_trade_id(), engine2.current_trade_id());
}

TEST(InvariantTest, DeterministicRandomizedFuzzStress) {
    // Recorded seeds for exact reproducibility
    constexpr std::uint64_t kSeeds[] = {12345ULL, 67890ULL, 99999ULL};

    for (std::uint64_t seed : kSeeds) {
        MatchingEngine engine;
        std::mt19937_64 rng(seed);
        std::uniform_int_distribution<int> action_dist(0, 10);
        std::uniform_int_distribution<int> side_dist(0, 1);
        std::uniform_int_distribution<Price> price_dist(9900, 10100);
        std::uniform_int_distribution<Quantity> qty_dist(1, 50);

        std::vector<OrderId> active_ids;
        OrderId next_id = 1;
        Quantity total_submitted = 0;
        Quantity total_traded = 0;
        Quantity total_cancelled = 0;

        for (int step = 0; step < 500; ++step) {
            int action = action_dist(rng);
            if (action <= 6 || active_ids.empty()) {
                // Submit limit order
                OrderId id = next_id++;
                Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
                Price price = price_dist(rng);
                Quantity qty = qty_dist(rng);
                total_submitted += qty;

                std::vector<Trade> trades;
                auto res = engine.submit_order(OrderRequest::limit(id, side, price, qty), trades);
                for (const auto& t : trades) {
                    total_traded += t.quantity;
                }

                if (res.status == OrderStatus::Accepted || res.status == OrderStatus::PartiallyFilled) {
                    active_ids.push_back(id);
                }
            } else if (action <= 8) {
                // Submit market order
                OrderId id = next_id++;
                Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
                Quantity qty = qty_dist(rng);
                total_submitted += qty;

                std::vector<Trade> trades;
                auto res = engine.submit_order(OrderRequest::market(id, side, qty), trades);
                for (const auto& t : trades) {
                    total_traded += t.quantity;
                }
                total_cancelled += res.cancelled_quantity;
            } else {
                // Cancel random resting order
                std::uniform_int_distribution<std::size_t> idx_dist(0, active_ids.size() - 1);
                std::size_t idx = idx_dist(rng);
                OrderId target = active_ids[idx];
                active_ids.erase(active_ids.begin() + idx);

                auto res = engine.cancel_order(target);
                if (res.status == OrderStatus::Cancelled) {
                    total_cancelled += res.cancelled_quantity;
                }
            }

            // Invariant must hold at every single step of execution!
            auto inv = engine.book().validate_invariants();
            ASSERT_TRUE(inv.valid);
        }

        // Final volume conservation:
        // Total submitted quantity must equal (2 * total_traded) + remaining_resting + total_cancelled!
        // Note: each trade accounts for 1 maker unit and 1 taker unit.
        Quantity resting_vol = engine.book().total_volume(Side::Buy) + engine.book().total_volume(Side::Sell);
        EXPECT_EQ(total_submitted, (2 * total_traded) + resting_vol + total_cancelled);
    }
}
