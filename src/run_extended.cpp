// ============================================================
// examples/run_extended.cpp
//
// Complete working example showing all four additions:
//   1. FundamentalValueAgent (Glosten-Milgrom informed trader)
//   2. MomentumAgent          (MACD trend-following)
//   3. LatencyModel           (two-tier HFT vs retail)
//   4. StylizedFacts          (automatic, printed at the end)
//
// Build (add to CMakeLists.txt — see cmake_additions.txt):
//   cmake --build build --target run_extended
//   ./build/run_extended
//
// The output is:
//   console: full stylized facts report
//   trades_extended.csv / tops_extended.csv / fv_signals.csv
// ============================================================

#include <fstream>
#include <iostream>
#include <memory>

// Our existing MSIM headers
#include "msim/matching_engine.hpp"
#include "msim/world.hpp"
#include "msim/rules.hpp"

// Our existing agents (adapt include paths if different)
// #include "msim/agents/noise_trader.hpp"
// #include "msim/agents/market_maker.hpp"

// The four new additions
#include "msim/agents/fundamental_value_agent.hpp"
#include "msim/agents/momentum_agent.hpp"
// latency_model.hpp and stylized_facts.hpp are included via world.hpp

int main() {
  using namespace msim;
  using namespace msim::agents;

  // ── 1. Build the matching engine ──────────────────────────────────────────
  // Use our existing RuleSet config (defaults are fine for a first test)
  RuleSet rules{};
  MatchingEngine engine(std::move(rules));

  // ── 2. Build the World ────────────────────────────────────────────────────
  World world(std::move(engine));

  // ── 3. Register agents ────────────────────────────────────────────────────
  //
  // IMPORTANT: the order we call add_agent() here MUST match the order of
  // entries in WorldConfig::latency_configs below.
  // Agent index 0 = first add_agent call, index 1 = second, etc.

  // Our existing agents — uncomment and adapt:
  //   world.add_agent(std::make_unique<NoiseTrader>(/*owner=*/1, ...));
  //   world.add_agent(std::make_unique<NoiseTrader>(/*owner=*/2, ...));
  //   world.add_agent(std::make_unique<NoiseTrader>(/*owner=*/3, ...));
  //   world.add_agent(std::make_unique<MarketMaker>(/*owner=*/10, ...));

  // NEW: Fundamental Value agents (owners 100, 101, 102)
  FundamentalValueConfig fv_cfg;
  fv_cfg.kappa     = 0.005;   // slow mean-reversion
  fv_cfg.sigma_v   = 1.5;     // 1.5 ticks of noise per step
  fv_cfg.threshold = 1.0;     // trade when ≥ 1 tick mispriced
  fv_cfg.lot_size  = 5;

  world.add_agent(std::make_unique<FundamentalValueAgent>(100, fv_cfg));
  world.add_agent(std::make_unique<FundamentalValueAgent>(101, fv_cfg));
  world.add_agent(std::make_unique<FundamentalValueAgent>(102, fv_cfg));

  // NEW: Momentum agents (owners 200, 201)
  MomentumConfig mom_cfg;
  mom_cfg.alpha_fast   = 2.0 / 6.0;    // 5-step EMA
  mom_cfg.alpha_slow   = 2.0 / 21.0;   // 20-step EMA
  mom_cfg.entry_band   = 0.30;
  mom_cfg.exit_band    = 0.05;
  mom_cfg.lot_size     = 3;
  mom_cfg.max_position = 15;
  mom_cfg.warmup_steps = 20;

  world.add_agent(std::make_unique<MomentumAgent>(200, mom_cfg));
  world.add_agent(std::make_unique<MomentumAgent>(201, mom_cfg));

  // ── 4. Configure the run ──────────────────────────────────────────────────
  WorldConfig cfg;
  cfg.dt_ns = 1'000'000;               // 1 ms steps

  // ── Latency model (two-tier) ──────────────────────────────────────────────
  // If we had 4 existing agents + 3 FV + 2 Momentum = 9 total,
  // provide 9 entries in latency_configs in the same registration order.
  //
  // Example below assumes ONLY the 5 new agents are registered
  // (3 FV + 2 Momentum).  Adjust n_fast / n_slow to match your total.

  cfg.latency_enabled = true;

  // Existing agents (if any): treat as "fast" HFT tier (500 ns fixed)
  // const int n_existing = 4;
  // for (int i = 0; i < n_existing; ++i)
  //   cfg.latency_configs.push_back({LatencyDistType::FIXED, 500.0});

  // FV agents: retail tier (lognormal ~5 µs, σ=1 µs)
  for (int i = 0; i < 3; ++i)
    cfg.latency_configs.push_back(
        {LatencyDistType::LOG_NORMAL, 5'000.0, 1'000.0});

  // Momentum agents: slightly faster retail (~2 µs)
  for (int i = 0; i < 2; ++i)
    cfg.latency_configs.push_back(
        {LatencyDistType::LOG_NORMAL, 2'000.0, 500.0});

  // ── Stylized facts ────────────────────────────────────────────────────────
  cfg.compute_stylized_facts = true;

  // ── FV signal log ─────────────────────────────────────────────────────────
  cfg.record_fv_signals = true;

  // ── 5. Pre-fill the book with resting liquidity ───────────────────────────
  // Without this, the first FV / momentum market orders will find an empty
  // book and be rejected.  Adapt to our existing pre-fill pattern.
  {
    // Example: seed 20 bids and 20 asks around mid-price 10000 ticks
    const Price mid = 10'000;
    for (int i = 1; i <= 20; ++i) {
      Order bid{};
      bid.id    = static_cast<OrderId>(900'000 + i);
      bid.owner = 999;  // a "seed liquidity" owner
      bid.side  = Side::Buy;
      bid.type  = OrderType::Limit;
      bid.price = mid - i;
      bid.qty   = 50;
      bid.ts    = 0;
      bid.tif   = TimeInForce::GTC;
      bid.mkt_style = MarketStyle::PureMarket;
      world.engine_mut().process(bid);

      Order ask = bid;
      ask.id    = static_cast<OrderId>(910'000 + i);
      ask.side  = Side::Sell;
      ask.price = mid + i;
      world.engine_mut().process(ask);
    }
  }

  // ── 6. Run ────────────────────────────────────────────────────────────────
  std::cout << "Running MSIM with all four additions...\n";
  const uint64_t seed      = 42;
  const double   horizon_s = 2.0;   // 2 second simulation = 2000 steps at 1ms

  auto result = world.run(seed, horizon_s, cfg);

  // ── 7. Print stylized facts report ────────────────────────────────────────
  std::cout << "\n==================================================\n";
  if (result.sf) {
    std::cout << StylizedFactsMeasurer::summary(*result.sf) << "\n";
  } else {
    std::cout << "Not enough trades to compute stylized facts.\n";
  }
  std::cout << "==================================================\n";
  std::cout << "Total trades:  " << result.trades.size() << "\n";
  std::cout << "Top snapshots: " << result.tops.size()   << "\n";
  std::cout << "Cancel fails:  " << result.cancel_failures << "\n";

  // ── 8. Write CSV output ───────────────────────────────────────────────────

  // trades_extended.csv
  {
    std::ofstream f("trades_extended.csv");
    f << "id,ts,price,qty,maker_order_id,taker_order_id\n";
    for (const auto& tr : result.trades)
      f << tr.id      << ","
        << tr.ts      << ","
        << tr.price   << ","
        << tr.qty     << ","
        << tr.maker_order_id << ","
        << tr.taker_order_id << "\n";
    std::cout << "Written trades_extended.csv\n";
  }

  // tops_extended.csv
  {
    std::ofstream f("tops_extended.csv");
    f << "ts,best_bid,best_ask,mid\n";
    for (const auto& top : result.tops) {
      f << top.ts << ","
        << (top.best_bid ? std::to_string(*top.best_bid) : "") << ","
        << (top.best_ask ? std::to_string(*top.best_ask) : "") << ","
        << (top.mid      ? std::to_string(*top.mid)      : "") << "\n";
    }
    std::cout << "Written tops_extended.csv\n";
  }

  // fv_signals.csv
  if (!result.fv_log.empty()) {
    std::ofstream f("fv_signals.csv");
    f << "ts,owner,V\n";
    for (const auto& e : result.fv_log)
      f << e.ts << "," << e.owner << "," << e.V << "\n";
    std::cout << "Written fv_signals.csv\n";
  }

  // stylized_facts.csv (one-row summary for batch experiments)
  if (result.sf) {
    std::ofstream f("stylized_facts.csv");
    f << StylizedFactsMeasurer::to_csv_header();
    f << StylizedFactsMeasurer::to_csv_row(*result.sf);
    std::cout << "Written stylized_facts.csv\n";
  }

  return 0;
}
