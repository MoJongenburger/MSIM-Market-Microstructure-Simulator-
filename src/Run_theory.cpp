// ============================================================
// src/run_theory.cpp
//
// Demonstration of all four market microstructure theory
// improvements:
//
//   1. Hawkes process order arrivals    (HawkesNoiseTrader)
//   2. LOB imbalance-sensitive MM       (MarketView::imbalance)
//   3. Avellaneda-Stoikov inventory     (MarketMakerAS)
//   4. Multi-asset correlated FV        (SharedFundamental +
//                                        MultiAssetFVAgent)
//
// Simulation layout:
//   Asset A (mid = 10000)  |  Asset B (mid = 20000)
//   -----------------------------------------------
//   3 HawkesNoiseTrader    |  3 HawkesNoiseTrader
//   1 MarketMakerAS        |  1 MarketMakerAS
//   2 MultiAssetFVAgent    |  2 MultiAssetFVAgent
//   (asset_idx = 0)        |  (asset_idx = 1)
//
// SharedFundamental drives both FV agent populations with
// correlation rho = 0.70.  Both worlds run for 5 seconds at
// 1ms steps (5000 steps).
//
// Outputs:
//   trades_A.csv, tops_A.csv
//   trades_B.csv, tops_B.csv
//   stylized_facts_A.csv, stylized_facts_B.csv
//   fv_signals.csv     (both assets, interleaved by ts)
//   cross_asset.csv    (step, V_A, V_B, mid_A, mid_B)
//
// Build (add to CMakeLists.txt):
//   add_executable(run_theory src/run_theory.cpp)
//   target_link_libraries(run_theory PRIVATE msim)
// ============================================================

#include <cassert>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// Core
#include "msim/world.hpp"
#include "msim/matching_engine.hpp"
#include "msim/order.hpp"
#include "msim/types.hpp"

// Theory additions
#include "msim/hawkes_process.hpp"
#include "msim/shared_fundamental.hpp"
#include "msim/agents/noise_trader_hawkes.hpp"
#include "msim/agents/market_maker_as.hpp"
#include "msim/agents/multi_asset_fv_agent.hpp"
#include "msim/stylized_facts.hpp"

using namespace msim;
using namespace msim::agents;

// ---------------------------------------------------------------------------
// Helper: pre-fill the book with symmetric resting limit orders around mid
// ---------------------------------------------------------------------------
static void prefill_book(MatchingEngine& engine,
                         Price mid, int levels, Qty qty_per_level)
{
  for (int i = 1; i <= levels; ++i) {
    Order bid{};
    bid.id        = static_cast<uint64_t>(1000 + i);
    bid.owner     = 0;
    bid.side      = Side::Buy;
    bid.type      = OrderType::Limit;
    bid.price     = mid - static_cast<Price>(i);
    bid.qty       = qty_per_level;
    bid.ts        = 0;
    bid.tif       = TimeInForce::GTC;
    bid.mkt_style = MarketStyle::PureMarket;
    engine.process(bid);

    Order ask{};
    ask.id        = static_cast<uint64_t>(2000 + i);
    ask.owner     = 0;
    ask.side      = Side::Sell;
    ask.type      = OrderType::Limit;
    ask.price     = mid + static_cast<Price>(i);
    ask.qty       = qty_per_level;
    ask.ts        = 0;
    ask.tif       = TimeInForce::GTC;
    ask.mkt_style = MarketStyle::PureMarket;
    engine.process(ask);
  }
}

// ---------------------------------------------------------------------------
// Helper: write trades CSV
// ---------------------------------------------------------------------------
static void write_trades(const std::string& path,
                         const std::vector<Trade>& trades)
{
  std::ofstream f(path);
  f << "id,ts,price,qty,maker_order_id,taker_order_id\n";
  for (const auto& t : trades)
    f << t.id << ',' << t.ts << ',' << t.price << ','
      << t.qty << ',' << t.maker_order_id << ',' << t.taker_order_id << '\n';
  std::cout << "Written " << path << " (" << trades.size() << " trades)\n";
}

// ---------------------------------------------------------------------------
// Helper: write top-of-book CSV
// ---------------------------------------------------------------------------
static void write_tops(const std::string& path,
                       const std::vector<BookTop>& tops)
{
  std::ofstream f(path);
  f << "ts,best_bid,best_ask,mid\n";
  for (const auto& t : tops) {
    auto px = [](std::optional<Price> p) -> std::string {
      return p ? std::to_string(*p) : "";
    };
    f << t.ts << ',' << px(t.best_bid) << ','
      << px(t.best_ask) << ',' << px(t.mid) << '\n';
  }
  std::cout << "Written " << path << " (" << tops.size() << " snapshots)\n";
}

// ---------------------------------------------------------------------------
// Helper: write FV log CSV
// ---------------------------------------------------------------------------
static void write_fv_log(const std::string& path,
                         const std::vector<FVLogEntry>& log,
                         const std::string& asset_label)
{
  std::ofstream f(path, std::ios::app);   // append so both assets go to one file
  for (const auto& e : log)
    f << e.ts << ',' << e.owner << ',' << asset_label << ',' << e.V << '\n';
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
  constexpr uint64_t SEED          = 42;
  constexpr double   HORIZON_S     = 5.0;   // 5 seconds
  constexpr int      N_STEPS       = 5000;  // 5000 ms steps
  constexpr Price    MID_A         = 10000;
  constexpr Price    MID_B         = 20000;
  constexpr double   CORR_AB       = 0.70;  // fundamental correlation

  std::cout << "\n=== MSIM Theory Run ===\n\n";

  // -------------------------------------------------------------------------
  // 1. Shared fundamental value signal  (two correlated assets)
  // -------------------------------------------------------------------------
  AssetFundamentalConfig fv_cfg_A;
  fv_cfg_A.kappa = 0.005;
  fv_cfg_A.sigma = 2.0;
  fv_cfg_A.mu    = static_cast<double>(MID_A);
  fv_cfg_A.V0    = static_cast<double>(MID_A);

  AssetFundamentalConfig fv_cfg_B;
  fv_cfg_B.kappa = 0.005;
  fv_cfg_B.sigma = 2.0;
  fv_cfg_B.mu    = static_cast<double>(MID_B);
  fv_cfg_B.V0    = static_cast<double>(MID_B);

  auto shared_fv = std::make_shared<SharedFundamental>(
      SharedFundamentalConfig::two_asset(fv_cfg_A, fv_cfg_B, CORR_AB));
  shared_fv->generate(SEED, N_STEPS + 10);  // +10 for safety margin

  std::cout << "SharedFundamental: 2 assets, rho=" << CORR_AB
            << ", " << N_STEPS << " steps pre-computed.\n\n";

  // -------------------------------------------------------------------------
  // Helper: build a World for one asset
  // -------------------------------------------------------------------------
  auto build_world = [&](Price mid, std::size_t asset_idx,
                         OwnerId owner_offset) -> World
  {
    MatchingEngine engine;  // default config: pure market, no phases
    prefill_book(engine, mid, 20, 10);

    World world(std::move(engine));

    // -- Hawkes noise traders (3 per asset) --
    HawkesNoiseConfig hawkes_cfg;
    hawkes_cfg.hawkes       = {.mu = 8.0, .alpha = 4.0, .beta = 16.0};
    hawkes_cfg.p_market     = 0.35;
    hawkes_cfg.lot_size     = 1;
    hawkes_cfg.imbalance_bias = 0.3;
    hawkes_cfg.dt_ns        = 1'000'000;  // 1ms

    for (int i = 0; i < 3; ++i)
      world.add_agent(std::make_unique<HawkesNoiseTrader>(
          owner_offset + static_cast<OwnerId>(i), hawkes_cfg));

    // -- Avellaneda-Stoikov market maker (1 per asset) --
    MarketMakerASConfig as_cfg;
    as_cfg.gamma      = 0.008;
    as_cfg.kappa      = 1.5;
    as_cfg.T_steps    = 500;
    as_cfg.sigma_init = 2.0;
    as_cfg.sigma_ewma = 0.02;
    as_cfg.alpha_imb  = 0.40;
    as_cfg.lot_size   = 2;
    as_cfg.max_inv    = 30;

    world.add_agent(std::make_unique<MarketMakerAS>(
        owner_offset + 10, as_cfg));

    // -- Multi-asset FV agents (2 per asset) --
    MultiAssetFVConfig ma_cfg;
    ma_cfg.asset_idx  = asset_idx;
    ma_cfg.threshold  = 1.5;  // slightly wider threshold than single-asset
    ma_cfg.lot_size   = 4;

    world.add_agent(std::make_unique<MultiAssetFVAgent>(
        owner_offset + 20, shared_fv, ma_cfg));
    world.add_agent(std::make_unique<MultiAssetFVAgent>(
        owner_offset + 21, shared_fv, ma_cfg));

    return world;
  };

  World world_A = build_world(MID_A, 0, 1000);
  World world_B = build_world(MID_B, 1, 2000);

  // -------------------------------------------------------------------------
  // 2. WorldConfig
  // -------------------------------------------------------------------------
  WorldConfig cfg;
  cfg.dt_ns                  = 1'000'000;  // 1ms
  cfg.compute_stylized_facts = true;
  cfg.record_fv_signals      = true;
  // Latency disabled for this run to isolate theoretical effects

  // -------------------------------------------------------------------------
  // 3. Run both worlds
  // -------------------------------------------------------------------------
  std::cout << "Running Asset A (" << HORIZON_S << "s, " << N_STEPS << " steps)...\n";
  WorldResult res_A = world_A.run(SEED, HORIZON_S, cfg);

  std::cout << "Running Asset B (" << HORIZON_S << "s, " << N_STEPS << " steps)...\n";
  WorldResult res_B = world_B.run(SEED ^ 0xCAFE'BABEull, HORIZON_S, cfg);

  // -------------------------------------------------------------------------
  // 4. Write outputs
  // -------------------------------------------------------------------------
  write_trades("trades_A.csv", res_A.trades);
  write_tops  ("tops_A.csv",   res_A.tops  );
  write_trades("trades_B.csv", res_B.trades);
  write_tops  ("tops_B.csv",   res_B.tops  );

  // FV signals (append both assets to one file)
  {
    std::ofstream hdr("fv_signals.csv");
    hdr << "ts,owner,asset,V\n";
  }
  write_fv_log("fv_signals.csv", res_A.fv_log, "A");
  write_fv_log("fv_signals.csv", res_B.fv_log, "B");
  std::cout << "Written fv_signals.csv\n";

  // Cross-asset correlation snapshot: V_A(t) vs V_B(t)
  {
    std::ofstream f("cross_asset.csv");
    f << "step,V_A,V_B\n";
    const int n = N_STEPS;
    for (int t = 0; t < n; ++t)
      f << t << ','
        << shared_fv->get(0, t) << ','
        << shared_fv->get(1, t) << '\n';
    std::cout << "Written cross_asset.csv\n";
  }

  // -------------------------------------------------------------------------
  // 5. Stylized facts reports
  // -------------------------------------------------------------------------
  auto write_sf = [](const std::string& path,
                     const std::optional<StyleFacts>& sf,
                     const std::string& label)
  {
    if (!sf) { std::cout << label << ": not enough trades for SF.\n"; return; }
    std::cout << "\n[" << label << "]\n"
              << StylizedFactsMeasurer::summary(*sf) << '\n';
    std::ofstream f(path);
    f << StylizedFactsMeasurer::to_csv_header();
    f << StylizedFactsMeasurer::to_csv_row(*sf);
    std::cout << "Written " << path << '\n';
  };

  write_sf("stylized_facts_A.csv", res_A.sf, "Asset A");
  write_sf("stylized_facts_B.csv", res_B.sf, "Asset B");

  // -------------------------------------------------------------------------
  // 6. Cross-asset summary stats
  // -------------------------------------------------------------------------
  std::cout << "\n=== Cross-Asset Summary ===\n";
  std::cout << "Asset A trades: " << res_A.trades.size() << '\n';
  std::cout << "Asset B trades: " << res_B.trades.size() << '\n';
  std::cout << "Fundamental correlation (target): " << CORR_AB << '\n';

  // Compute realised correlation of shared FV signal
  if (N_STEPS > 1) {
    double mu_a = 0, mu_b = 0;
    for (int t = 0; t < N_STEPS; ++t) {
      mu_a += shared_fv->get(0, t);
      mu_b += shared_fv->get(1, t);
    }
    mu_a /= N_STEPS; mu_b /= N_STEPS;
    double cov = 0, va = 0, vb = 0;
    for (int t = 0; t < N_STEPS; ++t) {
      const double da = shared_fv->get(0, t) - mu_a;
      const double db = shared_fv->get(1, t) - mu_b;
      cov += da * db; va += da * da; vb += db * db;
    }
    const double realised_corr = (va > 0 && vb > 0)
        ? cov / std::sqrt(va * vb) : 0.0;
    std::cout << "Realised FV correlation:        " << realised_corr << '\n';
  }

  std::cout << "\nDone.\n";
  return 0;
}
