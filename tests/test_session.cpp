#include <gtest/gtest.h>
#include "msim/session.hpp"

TEST(Session, TALThenClosingAuctionThenClosed) {
  msim::MatchingEngine eng;

  // Seed last trade at 10000 so TAL has a reference price
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{1, 1, msim::Side::Sell, msim::OrderType::Limit, 10000, 1, 2}));
  (void)eng.process(msim::Order{2, 2, msim::Side::Buy, msim::OrderType::Market, 0, 1, 3});

  msim::SessionSchedule sched;
  sched.tal_start_ts = 10;
  sched.tal_end_ts = 20;
  sched.closing_auction_start_ts = 20;
  sched.closing_auction_end_ts = 30;

  msim::SessionController session(sched);

  // Enter TAL
  session.on_time(eng, 10);
  EXPECT_EQ(eng.rules().phase(), msim::MarketPhase::TradingAtLast);

  // Off-last limit should reject during TAL
  auto r0 = eng.process(msim::Order{3, 12, msim::Side::Buy, msim::OrderType::Limit, 9990, 1, 7});
  EXPECT_EQ(r0.status, msim::OrderStatus::Rejected);
  EXPECT_EQ(r0.reject_reason, msim::RejectReason::PriceNotAtLast);

  // Transition to closing auction at 20
  session.on_time(eng, 20);
  EXPECT_EQ(eng.rules().phase(), msim::MarketPhase::ClosingAuction);

  // Queue crossing interest
  (void)eng.process(msim::Order{10, 21, msim::Side::Buy, msim::OrderType::Limit, 10100, 5, 1});
  (void)eng.process(msim::Order{11, 22, msim::Side::Sell, msim::OrderType::Limit, 10050, 5, 2});

  // End at 30: uncross + closed (via flush, even without a new order)
  session.on_time(eng, 30);
  EXPECT_EQ(eng.rules().phase(), msim::MarketPhase::Closed);
}
