#include <gtest/gtest.h>
#include "msim/book.hpp"

TEST(OrderBook, EmptyInitially) {
  msim::OrderBook ob;
  EXPECT_TRUE(ob.empty(msim::Side::Buy));
  EXPECT_TRUE(ob.empty(msim::Side::Sell));
  EXPECT_FALSE(ob.best_bid().has_value());
  EXPECT_FALSE(ob.best_ask().has_value());
  EXPECT_FALSE(ob.is_crossed());
}

TEST(OrderBook, BestBidAskAndDepth) {
  msim::OrderBook ob;

  // Add some bids (resting, no asks yet so never crossing)
  EXPECT_TRUE(ob.add_resting_limit(msim::Order{1, 10, msim::Side::Buy, msim::OrderType::Limit, 100, 5, 1}));
  EXPECT_TRUE(ob.add_resting_limit(msim::Order{2, 11, msim::Side::Buy, msim::OrderType::Limit, 101, 7, 1}));
  EXPECT_TRUE(ob.add_resting_limit(msim::Order{3, 12, msim::Side::Buy, msim::OrderType::Limit, 101, 3, 1}));

  ASSERT_TRUE(ob.best_bid().has_value());
  EXPECT_EQ(*ob.best_bid(), 101);

  auto bid_depth = ob.depth(msim::Side::Buy, 2);
  ASSERT_EQ(bid_depth.size(), 2u);
  EXPECT_EQ(bid_depth[0].price, 101);
  EXPECT_EQ(bid_depth[0].total_qty, 10); // 7 + 3
  EXPECT_EQ(bid_depth[0].order_count, 2);

  EXPECT_EQ(bid_depth[1].price, 100);
  EXPECT_EQ(bid_depth[1].total_qty, 5);
  EXPECT_EQ(bid_depth[1].order_count, 1);

  // Add an ask that does not cross
  EXPECT_TRUE(ob.add_resting_limit(msim::Order{4, 13, msim::Side::Sell, msim::OrderType::Limit, 105, 4, 2}));
  ASSERT_TRUE(ob.best_ask().has_value());
  EXPECT_EQ(*ob.best_ask(), 105);

  EXPECT_FALSE(ob.is_crossed());
}

TEST(OrderBook, RejectCrossingRestingOrders) {
  msim::OrderBook ob;

  // Establish a spread: best bid 100, best ask 105
  EXPECT_TRUE(ob.add_resting_limit(msim::Order{1, 10, msim::Side::Buy,  msim::OrderType::Limit, 100, 5, 1}));
  EXPECT_TRUE(ob.add_resting_limit(msim::Order{2, 11, msim::Side::Sell, msim::OrderType::Limit, 105, 5, 2}));

  // This BUY would cross (>= best ask)
  EXPECT_FALSE(ob.add_resting_limit(msim::Order{3, 12, msim::Side::Buy,  msim::OrderType::Limit, 105, 1, 1}));
  EXPECT_FALSE(ob.add_resting_limit(msim::Order{4, 13, msim::Side::Buy,  msim::OrderType::Limit, 106, 1, 1}));

  // This SELL would cross (<= best bid)
  EXPECT_FALSE(ob.add_resting_limit(msim::Order{5, 14, msim::Side::Sell, msim::OrderType::Limit, 100, 1, 2}));
  EXPECT_FALSE(ob.add_resting_limit(msim::Order{6, 15, msim::Side::Sell, msim::OrderType::Limit,  99, 1, 2}));

  EXPECT_FALSE(ob.is_crossed());
  EXPECT_EQ(*ob.best_bid(), 100);
  EXPECT_EQ(*ob.best_ask(), 105);
}

