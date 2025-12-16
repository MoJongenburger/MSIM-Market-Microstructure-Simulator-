#include <gtest/gtest.h>

#include "msim/world.hpp"
#include "msim/matching_engine.hpp"
#include "msim/rules.hpp"
#include "msim/agents/noise_trader.hpp"

TEST(Agents, NoiseTraderWorldRunsDeterministically) {
  msim::RulesConfig cfg;

  // IMPORTANT: braces avoid "most vexing parse"
  msim::MatchingEngine eng{ msim::RuleSet(cfg) };

  msim::World w(std::move(eng));

  msim::agents::NoiseTraderConfig nt{};
  nt.intensity_per_step = 0.30;
  nt.prob_market = 0.15;
  nt.max_offset_ticks = 5;
  nt.min_qty = 1;
  nt.max_qty = 10;
  nt.tick_size = 1;
  nt.lot_size = 1;
  nt.default_mid = 100;

  w.add_agent(std::make_unique<msim::agents::NoiseTrader>(1, nt));
  w.add_agent(std::make_unique<msim::agents::NoiseTrader>(2, nt));
  w.add_agent(std::make_unique<msim::agents::NoiseTrader>(3, nt));

  const msim::Ts start_ts = 0;
  const msim::Ts horizon_ns = 1'000'000'000; // 1 second
  const msim::Ts step_ns = 100'000;          // 0.1ms

  // Run twice with same seed => identical output size (smoke determinism)
  auto r1 = w.run(start_ts, horizon_ns, step_ns, /*depth*/0, /*seed*/123);
  auto r2 = w.run(start_ts, horizon_ns, step_ns, /*depth*/0, /*seed*/123);

  EXPECT_EQ(r1.trades.size(), r2.trades.size());
  EXPECT_EQ(r1.tops.size(), r2.tops.size());
}
