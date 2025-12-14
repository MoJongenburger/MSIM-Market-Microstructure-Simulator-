#include <gtest/gtest.h>
#include "msim/matching_engine.hpp"

TEST(MatchingEngine, FIFOAtSamePrice) {
  msim::MatchingEngine eng;

  // Two asks at same price: FIFO by insertion time
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{1, 10, msim::Side::Sell, msim::OrderType::Limit, 105, 5, 1}));
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{2, 11, msim::Side::Sell, msim::OrderType::Limit, 105, 7, 2}));

  auto res = eng.process(msim::Order{100, 20, msim::Side::Buy, msim::OrderType::Market, 0, 8, 9});

  ASSERT_EQ(res.trades.size(), 2u);
  EXPECT_EQ(res.trades[0].maker_order_id, 1u);
  EXPECT_EQ(res.trades[0].qty, 5);
  EXPECT_EQ(res.trades[1].maker_order_id, 2u);
  EXPECT_EQ(res.trades[1].qty, 3);
  EXPECT_EQ(res.filled_qty, 8);
}

TEST(MatchingEngine, LimitBuyPartialFillThenRest) {
  msim::MatchingEngine eng;

  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{1, 10, msim::Side::Sell, msim::OrderType::Limit, 105, 4, 1}));
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{2, 11, msim::Side::Sell, msim::OrderType::Limit, 106, 4, 1}));

  // Buy limit at 105 for qty 10: fills 4 @105, then rests 6 as bid @105
  auto res = eng.process(msim::Order{100, 20, msim::Side::Buy, msim::OrderType::Limit, 105, 10, 9});

  ASSERT_EQ(res.trades.size(), 1u);
  EXPECT_EQ(res.trades[0].price, 105);
  EXPECT_EQ(res.trades[0].qty, 4);
  EXPECT_EQ(res.filled_qty, 4);

  ASSERT_TRUE(res.resting.has_value());
  EXPECT_EQ(res.resting->price, 105);
  EXPECT_EQ(res.resting->qty, 6);

  EXPECT_EQ(*eng.book().best_bid(), 105);
  EXPECT_EQ(*eng.book().best_ask(), 106);
  EXPECT_FALSE(eng.book().is_crossed());
}
