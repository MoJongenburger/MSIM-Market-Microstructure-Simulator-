#include <gtest/gtest.h>
#include "msim/matching_engine.hpp"

TEST(SessionPhases, TradingAtLastRejectsLimitNotAtLast) {
  msim::MatchingEngine eng;

  // Seed last trade at 10000
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{1, 1, msim::Side::Sell, msim::OrderType::Limit, 10000, 1, 2}));
  (void)eng.process(msim::Order{2, 2, msim::Side::Buy, msim::OrderType::Market, 0, 1, 3});

  eng.start_trading_at_last(100);

  // Limit not at last should be rejected
  auto res = eng.process(msim::Order{3, 10, msim::Side::Buy, msim::OrderType::Limit, 9990, 1, 7});
  EXPECT_EQ(res.status, msim::OrderStatus::Rejected);
  EXPECT_EQ(res.reject_reason, msim::RejectReason::PriceNotAtLast);
}

TEST(SessionPhases, ClosingAuctionUncrossesAndCloses) {
  msim::MatchingEngine eng;

  // Seed last trade so TAL can exist (not strictly required for closing auction)
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{1, 1, msim::Side::Sell, msim::OrderType::Limit, 10000, 1, 2}));
  (void)eng.process(msim::Order{2, 2, msim::Side::Buy, msim::OrderType::Market, 0, 1, 3});

  eng.start_closing_auction(20);

  // Queue crossing interest in auction
  (void)eng.process(msim::Order{10, 10, msim::Side::Buy, msim::OrderType::Limit, 10100, 5, 1});
  (void)eng.process(msim::Order{11, 11, msim::Side::Sell, msim::OrderType::Limit, 10050, 5, 2});

  // At ts >= 20, uncross happens, phase becomes Closed
  auto r = eng.process(msim::Order{12, 25, msim::Side::Buy, msim::OrderType::Limit, 1, 1, 9});

  EXPECT_FALSE(r.trades.empty());
  EXPECT_EQ(eng.rules().phase(), msim::MarketPhase::Closed);
}
