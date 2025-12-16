#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "msim/rules.hpp"
#include "msim/world.hpp"

// Agents live under msim::agents
#include "msim/agents/noise_trader.hpp"
#include "msim/agents/market_maker.hpp"

static void write_trades_csv(const std::string& path, const std::vector<msim::Trade>& trades) {
  std::ofstream f(path);
  f << "trade_id,ts,price,qty,maker_id,taker_id\n";
  for (const auto& t : trades) {
    f << t.id << "," << t.ts << "," << t.price << "," << t.qty << ","
      << t.maker_order_id << "," << t.taker_order_id << "\n";
  }
}

static void write_top_csv(const std::string& path, const std::vector<msim::BookTop>& tops) {
  std::ofstream f(path);
  f << "ts,best_bid,best_ask,mid\n";
  for (const auto& x : tops) {
    f << x.ts << ",";
    if (x.best_bid) f << *x.best_bid;
    f << ",";
    if (x.best_ask) f << *x.best_ask;
    f << ",";
    if (x.mid) f << *x.mid;
    f << "\n";
  }
}

int main(int argc, char** argv) {
  uint64_t seed = 1;
  double horizon = 2.0; // seconds

  if (argc >= 2) seed = static_cast<uint64_t>(std::stoull(argv[1]));
  if (argc >= 3) horizon = std::stod(argv[2]);

  msim::RulesConfig rcfg{};

  // Avoid vexing-parse: use braces
  msim::MatchingEngine eng{msim::RuleSet(rcfg)};
  msim::World w{std::move(eng)};

  // Params + agents are in msim::agents
  msim::agents::NoiseTraderParams np{};
  w.add_agent(std::make_unique<msim::agents::NoiseTrader>(msim::OwnerId{1}, rcfg, np));

  msim::agents::MarketMakerParams mp{};
  w.add_agent(std::make_unique<msim::agents::MarketMaker>(msim::OwnerId{2}, rcfg, mp));

  auto res = w.run(seed, horizon);

  write_trades_csv("trades.csv", res.trades);
  write_top_csv("top.csv", res.tops);

  std::cout << "seed=" << seed
            << " horizon_s=" << horizon
            << " trades=" << res.trades.size()
            << "\n";

  return 0;
}
