#include "test_framework.hpp"
#include "tradenex/matching_engine.hpp"

using namespace tradenex;

TEST(CancellationTest, SuccessfulCancellation) {
    MatchingEngine engine;
    engine.submit_order(OrderRequest::limit(1, Side::Buy, 9990, 100));
    EXPECT_EQ(engine.book().count_orders(), 1);

    auto res = engine.cancel_order(1);
    EXPECT_EQ(res.status, OrderStatus::Cancelled);
    EXPECT_EQ(res.cancelled_quantity, 100);
    EXPECT_EQ(engine.book().count_orders(), 0);
    EXPECT_FALSE(engine.book().best_bid().has_value());
    EXPECT_TRUE(engine.book().validate_invariants().valid);
}

TEST(CancellationTest, UnknownOrderCancellation) {
    MatchingEngine engine;
    auto res = engine.cancel_order(999);
    EXPECT_EQ(res.status, OrderStatus::Rejected);
    EXPECT_EQ(res.reject_reason, RejectReason::UnknownOrder);
}

TEST(CancellationTest, DoubleCancellation) {
    MatchingEngine engine;
    engine.submit_order(OrderRequest::limit(1, Side::Buy, 9990, 50));
    auto res1 = engine.cancel_order(1);
    EXPECT_EQ(res1.status, OrderStatus::Cancelled);

    auto res2 = engine.cancel_order(1);
    EXPECT_EQ(res2.status, OrderStatus::Rejected);
    EXPECT_EQ(res2.reject_reason, RejectReason::UnknownOrder);
}

TEST(CancellationTest, FilledOrderCancellation) {
    MatchingEngine engine;
    engine.submit_order(OrderRequest::limit(1, Side::Sell, 10000, 50));
    engine.submit_order(OrderRequest::limit(2, Side::Buy, 10000, 50));

    // Order 1 is now fully filled and removed from book
    auto res = engine.cancel_order(1);
    EXPECT_EQ(res.status, OrderStatus::Rejected);
    EXPECT_EQ(res.reject_reason, RejectReason::UnknownOrder);
}

TEST(CancellationTest, PartiallyFilledOrderCancellation) {
    MatchingEngine engine;
    engine.submit_order(OrderRequest::limit(1, Side::Buy, 10000, 100));

    // Fill 30
    engine.submit_order(OrderRequest::limit(2, Side::Sell, 10000, 30));

    auto resting = engine.book().find_order(1);
    ASSERT_TRUE(resting.has_value());
    EXPECT_EQ(resting->remaining_quantity, 70);
    EXPECT_EQ(resting->filled_quantity(), 30);

    // Cancel remaining 70
    auto res = engine.cancel_order(1);
    EXPECT_EQ(res.status, OrderStatus::Cancelled);
    EXPECT_EQ(res.cancelled_quantity, 70);

    EXPECT_EQ(engine.book().count_orders(), 0);
    EXPECT_TRUE(engine.book().validate_invariants().valid);
}

TEST(CancellationTest, RelativePriorityPreservedAfterCancellation) {
    MatchingEngine engine;
    engine.submit_order(OrderRequest::limit(1, Side::Buy, 10000, 10));
    engine.submit_order(OrderRequest::limit(2, Side::Buy, 10000, 20));
    engine.submit_order(OrderRequest::limit(3, Side::Buy, 10000, 30));

    // Cancel middle order #2
    engine.cancel_order(2);

    // Incoming sell of 25: should fill order 1 (10), then order 3 (15)
    std::vector<Trade> trades;
    engine.submit_order(OrderRequest::limit(4, Side::Sell, 10000, 25), trades);

    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].maker_order_id, 1);
    EXPECT_EQ(trades[0].quantity, 10);
    EXPECT_EQ(trades[1].maker_order_id, 3);
    EXPECT_EQ(trades[1].quantity, 15);

    auto resting3 = engine.book().find_order(3);
    ASSERT_TRUE(resting3.has_value());
    EXPECT_EQ(resting3->remaining_quantity, 15);
    EXPECT_TRUE(engine.book().validate_invariants().valid);
}
