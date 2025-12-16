#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "msim/order_flow.hpp"
#include "msim/simulator.hpp"

#include "msim/world.hpp"
#include "msim/agents/noise_trader.hpp"
#include "msim/rules.hpp"
#include "msim/matching_engine.hpp"

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

static void usage() {
  std::cout
    << "Usage:\n"
    << "  msim_cli [seed] [horizon_seconds]\n"
    << "  msim_cli --agents <seed> <horizon_seconds>\n";
}

int main(int argc, char** argv) {
  // ---------------- Agent mode ----------------
  if (argc >= 2 && std::string(argv[1]) == "--agents") {
    if (argc < 4) { usage(); return 1; }

    const uint64_t seed = static_cast<uint64_t>(std::stoull(argv[2]));
    const double horizon_s = std::stod(argv[3]);
    const msim::Ts horizon_ns = static_cast<msim::Ts>(horizon_s * 1'000'000'000.0);

    // Basic rules for now (you can enable more in cfg later)
    msim::RulesConfig cfg;
    cfg.tick_size = 1;
    cfg.lot_size = 1;
    cfg.min_qty = 1;

    msim::MatchingEngine eng(msim::RuleSet(cfg));
    msim::World w(std::move(eng));

    msim::agents::NoiseTraderConfig nt{};
    nt.intensity_per_step = 0.30;
    nt.prob_market = 0.15;
    nt.max_offset_ticks = 5;
    nt.min_qty = 1;
    nt.max_qty = 10;

    // Multiple independent agents
    w.add_agent(std::make_unique<msim::agents::NoiseTrader>(1, nt, cfg));
    w.add_agent(std::make_unique<msim::agents::NoiseTrader>(2, nt, cfg));
    w.add_agent(std::make_unique<msim::agents::NoiseTrader>(3, nt, cfg));

    const msim::Ts step_ns = 100'000; // 0.1ms deterministic timestep

    auto res = w.run(/*start_ts*/0, horizon_ns, step_ns, /*depth*/0, /*seed*/seed);

    write_trades_csv("trades.csv", res.trades);
    write_top_csv("top.csv", res.tops);

    std::cout << "AGENTS RUN COMPLETE "
              << "steps=" << res.stats.steps
              << " actions=" << res.stats.actions_sent
              << " orders=" << res.stats.orders_sent
              << " rejects=" << res.stats.rejects
              << " trades=" << res.stats.trades
              << "\n";
    return 0;
  }

  // ---------------- Existing simulator mode (UNCHANGED) ----------------
  uint64_t seed = 1;
  double horizon = 2.0; // seconds

  if (argc >= 2) seed = static_cast<uint64_t>(std::stoull(argv[1]));
  if (argc >= 3) horizon = std::stod(argv[2]);

  msim::FlowParams p{};
  msim::OrderFlowGenerator gen(seed, p);

  const msim::Ts t0 = 0;
  auto events = gen.generate(t0, horizon);

  msim::Simulator sim;
  auto sim_res = sim.run(events);

  write_trades_csv("trades.csv", sim_res.trades);
  write_top_csv("top.csv", sim_res.tops);

  std::cout << "events=" << events.size()
            << " trades=" << sim_res.trades.size()
            << " cancel_failures=" << sim_res.cancel_failures
            << " modify_failures=" << sim_res.modify_failures
            << "\n";
  return 0;
}
