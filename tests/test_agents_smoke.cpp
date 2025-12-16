#include <gtest/gtest.h>

#include "msim/world.hpp"
#include "msim/agents/noise_trader.hpp"

TEST(Agents, NoiseTraderWorldRunsDeterministically) {
  msim::RulesConfig cfg;
  cfg.tick_size = 1;
  cfg.lot_size = 1;
  cfg.min_qty = 1;

  msim::MatchingEngine eng(msim::RuleSet(cfg));
  msim::World w(std::move(eng));

  msim::agents::NoiseTraderConfig nt{};
  nt.intensity_per_step = 0.5;
  nt.prob_market = 0.2;
  nt.max_offset_ticks = 3;
  nt.min_qty = 1;
  nt.max_qty = 5;

  w.add_agent(std::make_unique<msim::agents::NoiseTrader>(1, nt, cfg));
  w.add_agent(std::make_unique<msim::agents::NoiseTrader>(2, nt, cfg));

  auto res = w.run(/*start_ts*/0, /*horizon_ns*/1'000'000, /*step_ns*/10'000, /*depth*/0, /*seed*/42);

  EXPECT_GT(res.stats.steps, 0u);
  EXPECT_GT(res.stats.actions_sent, 0u);
}
