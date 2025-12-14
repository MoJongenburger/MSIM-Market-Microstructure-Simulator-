#include <gtest/gtest.h>
#include "msim/matching_engine.hpp"

TEST(Rules, RejectInvalidOrdersWithReason) {
  msim::MatchingEngine eng;

  // qty=0 invalid
  auto res = eng.process(msim::Order{1, 10, msim::Side::Buy, msim::OrderType::Market, 0, 0, 1});
  EXPECT_EQ(res.status, msim::OrderStatus::Rejected);
  EXPECT_EQ(res.reject_reason, msim::RejectReason::InvalidOrder);
  EXPECT_TRUE(res.trades.empty());
}

TEST(Rules, RejectOrdersWhenMarketHalted) {
  msim::MatchingEngine eng;
  eng.rules_mut().set_phase(msim::MarketPhase::Halted);

  auto res = eng.process(msim::Order{2, 20, msim::Side::Buy, msim::OrderType::Market, 0, 5, 1});
  EXPECT_EQ(res.status, msim::OrderStatus::Rejected);
  EXPECT_EQ(res.reject_reason, msim::RejectReason::MarketHalted);
  EXPECT_TRUE(res.trades.empty());
}
