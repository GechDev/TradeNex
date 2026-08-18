#include "test_framework.hpp"
#include "tradenex/order_book.hpp"

#ifndef TRADENEX_USE_GTEST
int main(int argc, char** argv) {
    return tradenex::test::run_all_tests(argc, argv);
}
#endif

using namespace tradenex;

TEST(OrderBookTest, NonCrossingOrders) {
    OrderBook book;
    book.insert_order(OrderRequest::limit(1, Side::Buy, 9990, 100), 1);
    book.insert_order(OrderRequest::limit(2, Side::Sell, 10010, 50), 2);

    EXPECT_EQ(book.count_orders(), 2);
    EXPECT_EQ(book.best_bid(), 9990);
    EXPECT_EQ(book.best_ask(), 10010);
    EXPECT_EQ(book.best_bid_quantity(), 100);
    EXPECT_EQ(book.best_ask_quantity(), 50);
    EXPECT_EQ(book.spread(), 20);
    EXPECT_NEAR(book.mid_price().value(), 10000.0, 0.001);

    auto inv = book.validate_invariants();
    EXPECT_TRUE(inv.valid);
}

TEST(OrderBookTest, BestPricePriority) {
    OrderBook book;
    book.insert_order(OrderRequest::limit(1, Side::Buy, 9980, 10), 1);
    book.insert_order(OrderRequest::limit(2, Side::Buy, 9990, 20), 2);
    book.insert_order(OrderRequest::limit(3, Side::Buy, 9970, 30), 3);

    EXPECT_EQ(book.best_bid(), 9990);
    EXPECT_EQ(book.best_bid_quantity(), 20);

    book.insert_order(OrderRequest::limit(4, Side::Sell, 10020, 15), 4);
    book.insert_order(OrderRequest::limit(5, Side::Sell, 10010, 25), 5);
    book.insert_order(OrderRequest::limit(6, Side::Sell, 10030, 35), 6);

    EXPECT_EQ(book.best_ask(), 10010);
    EXPECT_EQ(book.best_ask_quantity(), 25);
    EXPECT_TRUE(book.validate_invariants().valid);
}

TEST(OrderBookTest, FifoPriorityAtEqualPrice) {
    OrderBook book;
    book.insert_order(OrderRequest::limit(1, Side::Buy, 10000, 10), 1);
    book.insert_order(OrderRequest::limit(2, Side::Buy, 10000, 20), 2);
    book.insert_order(OrderRequest::limit(3, Side::Buy, 10000, 30), 3);

    EXPECT_EQ(book.best_bid(), 10000);
    EXPECT_EQ(book.best_bid_quantity(), 60);

    const auto& bids = book.bids();
    auto it = bids.find(10000);
    ASSERT_TRUE(it != bids.end());
    EXPECT_EQ(it->second.order_count, 3);
    EXPECT_EQ(it->second.head->id, 1);
    EXPECT_EQ(it->second.head->next->id, 2);
    EXPECT_EQ(it->second.tail->id, 3);
    EXPECT_TRUE(book.validate_invariants().valid);
}

TEST(OrderBookTest, EmptyPriceLevelCleanup) {
    OrderBook book;
    book.insert_order(OrderRequest::limit(1, Side::Buy, 10000, 10), 1);
    book.insert_order(OrderRequest::limit(2, Side::Buy, 10000, 20), 2);
    book.insert_order(OrderRequest::limit(3, Side::Buy, 9990, 50), 3);

    EXPECT_EQ(book.count_levels(Side::Buy), 2);

    book.cancel_order(1);
    EXPECT_EQ(book.count_levels(Side::Buy), 2);
    EXPECT_EQ(book.best_bid_quantity(), 20);

    book.cancel_order(2);
    EXPECT_EQ(book.count_levels(Side::Buy), 1);
    EXPECT_EQ(book.best_bid(), 9990);
    EXPECT_EQ(book.best_bid_quantity(), 50);

    book.cancel_order(3);
    EXPECT_EQ(book.count_levels(Side::Buy), 0);
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_TRUE(book.validate_invariants().valid);
}

TEST(OrderBookTest, DepthSnapshot) {
    OrderBook book;
    book.insert_order(OrderRequest::limit(1, Side::Buy, 9990, 10), 1);
    book.insert_order(OrderRequest::limit(2, Side::Buy, 9980, 20), 2);
    book.insert_order(OrderRequest::limit(3, Side::Buy, 9970, 30), 3);

    book.insert_order(OrderRequest::limit(4, Side::Sell, 10010, 40), 4);
    book.insert_order(OrderRequest::limit(5, Side::Sell, 10020, 50), 5);

    auto snap = book.snapshot(2);
    ASSERT_EQ(snap.bids.size(), 2);
    EXPECT_EQ(snap.bids[0].price, 9990);
    EXPECT_EQ(snap.bids[0].quantity, 10);
    EXPECT_EQ(snap.bids[1].price, 9980);
    EXPECT_EQ(snap.bids[1].quantity, 20);

    ASSERT_EQ(snap.asks.size(), 2);
    EXPECT_EQ(snap.asks[0].price, 10010);
    EXPECT_EQ(snap.asks[0].quantity, 40);
    EXPECT_EQ(snap.asks[1].price, 10020);
    EXPECT_EQ(snap.asks[1].quantity, 50);
}
