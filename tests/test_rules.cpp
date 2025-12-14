#include <gtest/gtest.h>
#include "msim/matching_engine.hpp"

TEST(Rules, RejectInvalidOrdersWithReason) {
  msim::MatchingEngine eng;

  // qty=0 invalid (fails base is_valid_order)
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

TEST(Rules, TickSizeRejectsLimitOrdersOffGrid) {
  msim::MatchingEngine eng;

  // Set tick size to 5 ticks implies allowed prices: ..., 10000, 10005, 10010, ...
  eng.rules_mut().config_mut().tick_size_ticks = 5;
  eng.rules_mut().config_mut().lot_size = 1;
  eng.rules_mut().config_mut().min_qty = 1;

  // Price 10003 is not on tick
  auto res = eng.process(msim::Order{3, 30, msim::Side::Buy, msim::OrderType::Limit, 10003, 1, 1});
  EXPECT_EQ(res.status, msim::OrderStatus::Rejected);
  EXPECT_EQ(res.reject_reason, msim::RejectReason::PriceNotOnTick);

  // Price 10005 is on tick
  auto ok = eng.process(msim::Order{4, 31, msim::Side::Buy, msim::OrderType::Limit, 10005, 1, 1});
  EXPECT_EQ(ok.status, msim::OrderStatus::Accepted);
  EXPECT_EQ(ok.reject_reason, msim::RejectReason::None);
}

TEST(Rules, LotSizeAndMinQtyEnforced) {
  msim::MatchingEngine eng;

  eng.rules_mut().config_mut().tick_size_ticks = 1;
  eng.rules_mut().config_mut().lot_size = 10;
  eng.rules_mut().config_mut().min_qty = 10;

  // Too small
  auto too_small = eng.process(msim::Order{5, 40, msim::Side::Buy, msim::OrderType::Market, 0, 5, 1});
  EXPECT_EQ(too_small.status, msim::OrderStatus::Rejected);
  EXPECT_EQ(too_small.reject_reason, msim::RejectReason::QtyBelowMinimum);

  // Not on lot (12 not multiple of 10)
  auto off_lot = eng.process(msim::Order{6, 41, msim::Side::Buy, msim::OrderType::Market, 0, 12, 1});
  EXPECT_EQ(off_lot.status, msim::OrderStatus::Rejected);
  EXPECT_EQ(off_lot.reject_reason, msim::RejectReason::QtyNotOnLot);

  // OK (20 is multiple of 10 and >= 10)
  auto ok = eng.process(msim::Order{7, 42, msim::Side::Buy, msim::OrderType::Market, 0, 20, 1});
  EXPECT_EQ(ok.status, msim::OrderStatus::Accepted);
  EXPECT_EQ(ok.reject_reason, msim::RejectReason::None);
}
