#include <gtest/gtest.h>
#include "msim/matching_engine.hpp"

TEST(OrderTypes, IOC_LimitDoesNotRestRemainder) {
  msim::MatchingEngine eng;

  // Book: asks 105x4, 106x4
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{1, 10, msim::Side::Sell, msim::OrderType::Limit, 105, 4, 1}));
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{2, 11, msim::Side::Sell, msim::OrderType::Limit, 106, 4, 1}));

  // IOC buy limit 105 qty 10 -> fills 4@105, remainder canceled
  msim::Order o{100, 20, msim::Side::Buy, msim::OrderType::Limit, 105, 10, 9,
                msim::TimeInForce::IOC, msim::MarketStyle::PureMarket};

  auto res = eng.process(o);

  ASSERT_EQ(res.trades.size(), 1u);
  EXPECT_EQ(res.trades[0].price, 105);
  EXPECT_EQ(res.trades[0].qty, 4);
  EXPECT_EQ(res.filled_qty, 4);
  EXPECT_FALSE(res.resting.has_value());

  // No bid should have been added at 105
  EXPECT_FALSE(eng.book().best_bid().has_value());
  EXPECT_EQ(*eng.book().best_ask(), 106);
}

TEST(OrderTypes, FOK_FailsAtomically_NoTradesNoBookChange) {
  msim::MatchingEngine eng;

  // Book: asks 105x4
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{1, 10, msim::Side::Sell, msim::OrderType::Limit, 105, 4, 1}));

  // FOK buy limit 105 qty 5 -> not enough liquidity at <=105, so no trades and book unchanged
  msim::Order fok{100, 20, msim::Side::Buy, msim::OrderType::Limit, 105, 5, 9,
                  msim::TimeInForce::FOK, msim::MarketStyle::PureMarket};

  auto res = eng.process(fok);

  EXPECT_TRUE(res.trades.empty());
  EXPECT_EQ(res.filled_qty, 0);
  EXPECT_FALSE(res.resting.has_value());

  // Book should remain unchanged
  auto d = eng.book().depth(msim::Side::Sell, 1);
  ASSERT_EQ(d.size(), 1u);
  EXPECT_EQ(d[0].price, 105);
  EXPECT_EQ(d[0].total_qty, 4);
}

TEST(OrderTypes, FOK_Succeeds_FillsCompletely) {
  msim::MatchingEngine eng;

  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{1, 10, msim::Side::Sell, msim::OrderType::Limit, 105, 4, 1}));
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{2, 11, msim::Side::Sell, msim::OrderType::Limit, 106, 4, 1}));

  msim::Order fok{100, 20, msim::Side::Buy, msim::OrderType::Limit, 106, 8, 9,
                  msim::TimeInForce::FOK, msim::MarketStyle::PureMarket};

  auto res = eng.process(fok);

  ASSERT_EQ(res.trades.size(), 2u);
  EXPECT_EQ(res.filled_qty, 8);
  EXPECT_FALSE(eng.book().best_ask().has_value());
}

TEST(OrderTypes, MarketToLimit_RestsRemainderAtLastFillPrice) {
  msim::MatchingEngine eng;

  // Only 4 shares available at 105
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{1, 10, msim::Side::Sell, msim::OrderType::Limit, 105, 4, 1}));

  msim::Order mtl{100, 20, msim::Side::Buy, msim::OrderType::Market, 0, 10, 9,
                  msim::TimeInForce::GTC, msim::MarketStyle::MarketToLimit};

  auto res = eng.process(mtl);

  ASSERT_EQ(res.trades.size(), 1u);
  EXPECT_EQ(res.trades[0].price, 105);
  EXPECT_EQ(res.trades[0].qty, 4);
  EXPECT_EQ(res.filled_qty, 4);

  // remainder should rest as limit at 105 for qty 6
  ASSERT_TRUE(res.resting.has_value());
  EXPECT_EQ(res.resting->type, msim::OrderType::Limit);
  EXPECT_EQ(res.resting->price, 105);
  EXPECT_EQ(res.resting->qty, 6);

  ASSERT_TRUE(eng.book().best_bid().has_value());
  EXPECT_EQ(*eng.book().best_bid(), 105);
}
