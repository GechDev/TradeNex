#include "test_framework.hpp"
#include "tradenex/simulator.hpp"

using namespace tradenex;

TEST(ReplayTest, DeterministicCsvReplay) {
    constexpr std::string_view csv = R"(
event,order_id,side,type,price_ticks,quantity
ADD,1,BUY,LIMIT,10000,10
ADD,2,SELL,LIMIT,9900,4
ADD,3,SELL,LIMIT,10000,3
CANCEL,1,,,
)";

    MarketSimulator sim1;
    auto summary1 = sim1.replay_csv_content(csv);

    MarketSimulator sim2;
    auto summary2 = sim2.replay_csv_content(csv);

    EXPECT_EQ(summary1.total_events, summary2.total_events);
    EXPECT_EQ(summary1.trades_generated, summary2.trades_generated);
    EXPECT_EQ(summary1.total_volume_traded, summary2.total_volume_traded);
    EXPECT_EQ(summary1.orders_fully_filled, summary2.orders_fully_filled);
    EXPECT_EQ(summary1.orders_cancelled, summary2.orders_cancelled);
    EXPECT_EQ(summary1.final_best_bid, summary2.final_best_bid);
    EXPECT_EQ(summary1.final_best_ask, summary2.final_best_ask);
    EXPECT_TRUE(summary1.invariants_hold);
    EXPECT_TRUE(summary2.invariants_hold);

    // Trade 1: Order 2 sells 4 @ 10000 to Order 1 (resting price is 10000)
    // Trade 2: Order 3 sells 3 @ 10000 to Order 1
    // Order 1 has 3 remaining
    // Cancel 1 cancels remaining 3
    EXPECT_EQ(summary1.trades_generated, 2);
    EXPECT_EQ(summary1.total_volume_traded, 7);
    EXPECT_EQ(summary1.orders_cancelled, 1);
    EXPECT_EQ(sim1.engine().book().count_orders(), 0);
}

TEST(ReplayTest, ReplayMalformedRecordsDiagnostics) {
    constexpr std::string_view malformed_csv = R"(
# Sample with errors
ADD,1,BUY,LIMIT,10000,10
ADD,invalid_id,SELL,LIMIT,10000,10
ADD,2,UNKNOWN_SIDE,LIMIT,10000,10
ADD,3,BUY,LIMIT,invalid_price,10
ADD,4,BUY,LIMIT,10000,invalid_qty
CANCEL,bad_id,,,
UNKNOWN_ACTION,1,2,3,4,5
ADD,5,BUY,LIMIT,9990,20
)";

    MarketSimulator sim;
    auto summary = sim.replay_csv_content(malformed_csv);

    // Valid records should execute safely
    EXPECT_EQ(summary.orders_accepted, 2); // order 1 and order 5
    EXPECT_EQ(sim.engine().book().count_orders(), 2);
    EXPECT_TRUE(summary.invariants_hold);
}
