#include <gtest/gtest.h>

#include "msim/world.hpp"
#include "msim/rules.hpp"
#include "msim/agents/noise_trader.hpp"
#include "msim/agents/market_maker.hpp"

TEST(Agents, NoiseTraderWorldRunsDeterministically) {
  msim::RulesConfig cfg{};
  cfg.tick_size_ticks = 1;
  cfg.lot_size = 1;
  cfg.min_qty = 1;

  {
    msim::MatchingEngine eng{ msim::RuleSet(cfg) };
    msim::World w(std::move(eng));
    msim::NoiseTraderParams np{};
    msim::MarketMakerParams mp{};

    w.add_agent(std::make_unique<msim::NoiseTrader>(msim::OwnerId{1}, cfg, np));
    w.add_agent(std::make_unique<msim::MarketMaker>(msim::OwnerId{2}, cfg, mp));

    msim::WorldConfig wc{};
    wc.dt_ns = 1'000'000;

    auto r1 = w.run(123, 1.0, wc);

    msim::MatchingEngine eng2{ msim::RuleSet(cfg) };
    msim::World w2(std::move(eng2));
    w2.add_agent(std::make_unique<msim::NoiseTrader>(msim::OwnerId{1}, cfg, np));
    w2.add_agent(std::make_unique<msim::MarketMaker>(msim::OwnerId{2}, cfg, mp));
    auto r2 = w2.run(123, 1.0, wc);

    EXPECT_EQ(r1.trades.size(), r2.trades.size());
    ASSERT_EQ(r1.accounts.size(), r2.accounts.size());

    for (std::size_t i = 0; i < r1.accounts.size(); ++i) {
      EXPECT_EQ(r1.accounts[i].owner, r2.accounts[i].owner);
      EXPECT_EQ(r1.accounts[i].cash_ticks, r2.accounts[i].cash_ticks);
      EXPECT_EQ(r1.accounts[i].position, r2.accounts[i].position);
      EXPECT_EQ(r1.accounts[i].mtm_ticks, r2.accounts[i].mtm_ticks);
    }
  }
}
