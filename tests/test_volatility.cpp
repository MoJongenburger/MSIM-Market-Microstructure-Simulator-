#include <gtest/gtest.h>
#include "msim/matching_engine.hpp"

TEST(VolatilityInterruption, TriggersAuctionWhenExecutionWouldBreachBands) {
  msim::MatchingEngine eng;

  // Configure a tight band so it's easy to trigger
  eng.rules_mut().config_mut().enable_price_bands = true;
  eng.rules_mut().config_mut().enable_volatility_interruption = true;
  eng.rules_mut().config_mut().band_bps = 100;               // 1%
  eng.rules_mut().config_mut().vol_auction_duration_ns = 5;  // 5 ns for test simplicity

  // Reference price from last trade
  eng.rules_mut().on_trades(std::span<const msim::Trade>{});

  // Set last trade price by executing one trade:
  // Put ask @10000, then market buy 1 -> last_trade = 10000
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{1, 1, msim::Side::Sell, msim::OrderType::Limit, 10000, 1, 2}));
  (void)eng.process(msim::Order{2, 2, msim::Side::Buy, msim::OrderType::Market, 0, 1, 3});

  // Now put a far ask outside 1% band: 12000 (breach)
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{3, 3, msim::Side::Sell, msim::OrderType::Limit, 12000, 5, 9}));

  // Market buy would execute at 12000 -> should trigger auction and queue (no trades)
  auto res = eng.process(msim::Order{4, 10, msim::Side::Buy, msim::OrderType::Market, 0, 1, 7});

  EXPECT_TRUE(res.trades.empty());
  EXPECT_EQ(eng.rules().phase(), msim::MarketPhase::Auction);
}

TEST(VolatilityInterruption, ReopensAfterDurationAndReplaysQueuedOrders) {
  msim::MatchingEngine eng;

  eng.rules_mut().config_mut().enable_price_bands = true;
  eng.rules_mut().config_mut().enable_volatility_interruption = true;
  eng.rules_mut().config_mut().band_bps = 100;                // 1%
  eng.rules_mut().config_mut().vol_auction_duration_ns = 5;   // 5 ns

  // Seed reference trade at 10000
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{1, 1, msim::Side::Sell, msim::OrderType::Limit, 10000, 1, 2}));
  (void)eng.process(msim::Order{2, 2, msim::Side::Buy, msim::OrderType::Market, 0, 1, 3});

  // Put ask far away to trigger auction
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{3, 3, msim::Side::Sell, msim::OrderType::Limit, 12000, 5, 9}));

  // Trigger auction, queue order
  auto r0 = eng.process(msim::Order{4, 10, msim::Side::Buy, msim::OrderType::Market, 0, 2, 7});
  EXPECT_TRUE(r0.trades.empty());
  EXPECT_EQ(eng.rules().phase(), msim::MarketPhase::Auction);

  // At ts >= auction_end, engine reopens and replays queued orders.
  // Provide a harmless order to advance time.
  auto r1 = eng.process(msim::Order{5, 20, msim::Side::Buy, msim::OrderType::Limit, 1, 1, 8});
  // The replayed market buy should have executed at 12000 for qty 2
  // (MVP replay behavior, not a true auction uncross yet)
  EXPECT_FALSE(r1.trades.empty());
  EXPECT_EQ(eng.rules().phase(), msim::MarketPhase::Continuous);
}
