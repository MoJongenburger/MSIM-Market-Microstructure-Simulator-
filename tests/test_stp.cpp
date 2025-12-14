#include <gtest/gtest.h>
#include "msim/matching_engine.hpp"

TEST(STP, CancelTakerPreventsSelfTrade_NoMakerRemoved) {
  msim::MatchingEngine eng;
  eng.rules_mut().config_mut().stp = msim::StpMode::CancelTaker;

  // Maker ask owned by owner=7
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{1, 10, msim::Side::Sell, msim::OrderType::Limit, 105, 5, 7}));

  // Taker buy is also owner=7 => should be killed, no trades
  msim::Order taker{2, 11, msim::Side::Buy, msim::OrderType::Market, 0, 3, 7};
  auto res = eng.process(taker);

  EXPECT_TRUE(res.trades.empty());

  // Maker should still be there
  auto d = eng.book().depth(msim::Side::Sell, 1);
  ASSERT_EQ(d.size(), 1u);
  EXPECT_EQ(d[0].total_qty, 5);
}

TEST(STP, CancelMakerPreventsSelfTrade_RemovesMakerThenAllowsNext) {
  msim::MatchingEngine eng;
  eng.rules_mut().config_mut().stp = msim::StpMode::CancelMaker;

  // Best ask is self-owned, second ask is different owner
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{1, 10, msim::Side::Sell, msim::OrderType::Limit, 105, 5, 7}));
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{2, 11, msim::Side::Sell, msim::OrderType::Limit, 106, 5, 8}));

  // Taker buy owner=7 should cancel maker id=1, then trade against owner=8 at 106
  msim::Order taker{3, 12, msim::Side::Buy, msim::OrderType::Market, 0, 3, 7};
  auto res = eng.process(taker);

  ASSERT_EQ(res.trades.size(), 1u);
  EXPECT_EQ(res.trades[0].price, 106);
  EXPECT_EQ(res.trades[0].qty, 3);

  // Verify self-owned level removed
  auto d = eng.book().depth(msim::Side::Sell, 2);
  ASSERT_EQ(d.size(), 1u);
  EXPECT_EQ(d[0].price, 106);
  EXPECT_EQ(d[0].total_qty, 2);
}
