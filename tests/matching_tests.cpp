#include "test_framework.hpp"
#include "tradenex/matching_engine.hpp"

using namespace tradenex;

TEST(MatchingTest, ExactPriceMatching) {
    MatchingEngine engine;
    engine.submit_order(OrderRequest::limit(1, Side::Sell, 10000, 50));

    std::vector<Trade> trades;
    auto res = engine.submit_order(OrderRequest::limit(2, Side::Buy, 10000, 50), trades);

    EXPECT_EQ(res.status, OrderStatus::Filled);
    EXPECT_EQ(res.filled_quantity, 50);
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].price, 10000);
    EXPECT_EQ(trades[0].quantity, 50);
    EXPECT_EQ(trades[0].maker_order_id, 1);
    EXPECT_EQ(trades[0].taker_order_id, 2);

    EXPECT_EQ(engine.book().count_orders(), 0);
    EXPECT_TRUE(engine.book().validate_invariants().valid);
}

TEST(MatchingTest, ExecutionPriceAtRestingPrice) {
    MatchingEngine engine;
    // Maker order rests at 9950
    engine.submit_order(OrderRequest::limit(1, Side::Buy, 9950, 100));

    // Aggressive taker Sell with limit 9900 matches resting buy at maker's price 9950!
    std::vector<Trade> trades;
    auto res = engine.submit_order(OrderRequest::limit(2, Side::Sell, 9900, 40), trades);

    EXPECT_EQ(res.status, OrderStatus::Filled);
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].price, 9950);
    EXPECT_EQ(trades[0].quantity, 40);

    // Resting order should still have 60 remaining
    auto resting = engine.book().find_order(1);
    ASSERT_TRUE(resting.has_value());
    EXPECT_EQ(resting->remaining_quantity, 60);
    EXPECT_TRUE(engine.book().validate_invariants().valid);
}

TEST(MatchingTest, PartialRestingOrderFill) {
    MatchingEngine engine;
    engine.submit_order(OrderRequest::limit(1, Side::Sell, 10000, 100));

    std::vector<Trade> trades;
    auto res = engine.submit_order(OrderRequest::limit(2, Side::Buy, 10000, 30), trades);

    EXPECT_EQ(res.status, OrderStatus::Filled);
    EXPECT_EQ(res.filled_quantity, 30);
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 30);

    EXPECT_EQ(engine.book().best_ask_quantity(), 70);
    auto resting = engine.book().find_order(1);
    ASSERT_TRUE(resting.has_value());
    EXPECT_EQ(resting->remaining_quantity, 70);
    EXPECT_TRUE(engine.book().validate_invariants().valid);
}

TEST(MatchingTest, PartialIncomingOrderFillWithRemainder) {
    MatchingEngine engine;
    engine.submit_order(OrderRequest::limit(1, Side::Sell, 10000, 40));

    std::vector<Trade> trades;
    // Buy 100 @ 10000: fills 40, rests 60 on the book
    auto res = engine.submit_order(OrderRequest::limit(2, Side::Buy, 10000, 100), trades);

    EXPECT_EQ(res.status, OrderStatus::PartiallyFilled);
    EXPECT_EQ(res.filled_quantity, 40);
    EXPECT_EQ(res.resting_quantity, 60);

    EXPECT_FALSE(engine.book().best_ask().has_value());
    EXPECT_EQ(engine.book().best_bid(), 10000);
    EXPECT_EQ(engine.book().best_bid_quantity(), 60);
    EXPECT_TRUE(engine.book().validate_invariants().valid);
}

TEST(MatchingTest, OneIncomingOrderMatchingMultipleRestingOrders) {
    MatchingEngine engine;
    engine.submit_order(OrderRequest::limit(1, Side::Sell, 10000, 20));
    engine.submit_order(OrderRequest::limit(2, Side::Sell, 10000, 30));
    engine.submit_order(OrderRequest::limit(3, Side::Sell, 10010, 50));

    std::vector<Trade> trades;
    // Buy 80 @ 10010: sweeps order 1 (20), order 2 (30), and partial order 3 (30)
    auto res = engine.submit_order(OrderRequest::limit(4, Side::Buy, 10010, 80), trades);

    EXPECT_EQ(res.status, OrderStatus::Filled);
    EXPECT_EQ(res.filled_quantity, 80);
    ASSERT_EQ(trades.size(), 3);

    EXPECT_EQ(trades[0].maker_order_id, 1);
    EXPECT_EQ(trades[0].price, 10000);
    EXPECT_EQ(trades[0].quantity, 20);

    EXPECT_EQ(trades[1].maker_order_id, 2);
    EXPECT_EQ(trades[1].price, 10000);
    EXPECT_EQ(trades[1].quantity, 30);

    EXPECT_EQ(trades[2].maker_order_id, 3);
    EXPECT_EQ(trades[2].price, 10010);
    EXPECT_EQ(trades[2].quantity, 30);

    // Order 3 should have 20 remaining
    EXPECT_EQ(engine.book().best_ask(), 10010);
    EXPECT_EQ(engine.book().best_ask_quantity(), 20);
    EXPECT_TRUE(engine.book().validate_invariants().valid);
}

TEST(MatchingTest, MarketOrderAgainstEmptyBook) {
    MatchingEngine engine;
    std::vector<Trade> trades;
    auto res = engine.submit_order(OrderRequest::market(1, Side::Buy, 50), trades);

    EXPECT_EQ(res.status, OrderStatus::Cancelled);
    EXPECT_EQ(res.filled_quantity, 0);
    EXPECT_EQ(res.cancelled_quantity, 50);
    EXPECT_EQ(trades.size(), 0);
    EXPECT_EQ(engine.book().count_orders(), 0);
}

TEST(MatchingTest, MarketOrderWithInsufficientLiquidity) {
    MatchingEngine engine;
    engine.submit_order(OrderRequest::limit(1, Side::Sell, 10000, 30));

    std::vector<Trade> trades;
    // Market buy for 50: consumes 30, cancels remaining 20
    auto res = engine.submit_order(OrderRequest::market(2, Side::Buy, 50), trades);

    EXPECT_EQ(res.status, OrderStatus::PartiallyFilled);
    EXPECT_EQ(res.filled_quantity, 30);
    EXPECT_EQ(res.cancelled_quantity, 20);
    EXPECT_EQ(res.resting_quantity, 0);
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 30);

    // Market order unfilled remainder must NEVER be on book
    EXPECT_EQ(engine.book().count_orders(), 0);
    EXPECT_TRUE(engine.book().validate_invariants().valid);
}

TEST(MatchingTest, FillOrKillOrder) {
    MatchingEngine engine;
    engine.submit_order(OrderRequest::limit(1, Side::Sell, 10000, 30));

    std::vector<Trade> trades;
    // FOK for 50: only 30 available -> must kill entire order with 0 fills
    auto res = engine.submit_order(OrderRequest::limit(2, Side::Buy, 10000, 50, TimeInForce::FOK), trades);

    EXPECT_EQ(res.status, OrderStatus::Cancelled);
    EXPECT_EQ(res.filled_quantity, 0);
    EXPECT_EQ(trades.size(), 0);
    EXPECT_EQ(engine.book().best_ask_quantity(), 30);

    // FOK for 30: fully fills
    trades.clear();
    auto res2 = engine.submit_order(OrderRequest::limit(3, Side::Buy, 10000, 30, TimeInForce::FOK), trades);
    EXPECT_EQ(res2.status, OrderStatus::Filled);
    EXPECT_EQ(res2.filled_quantity, 30);
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(engine.book().count_orders(), 0);
}
