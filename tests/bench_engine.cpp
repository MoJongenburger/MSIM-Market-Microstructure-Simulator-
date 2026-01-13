#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "msim/matching_engine.hpp"
#include "msim/order.hpp"
#include "msim/rules.hpp"
#include "msim/types.hpp"

namespace {

msim::MatchingEngine make_engine_prefilled(std::int32_t n_resting) {
  msim::RulesConfig cfg{};
  // Keep rules “normal”; we want realistic processing path
  msim::MatchingEngine eng{msim::RuleSet(cfg)};

  constexpr msim::Price mid = 10000; // ticks
  constexpr int levels = 10;

  // Pre-fill book with resting liquidity on both sides, spread around mid
  // Use large qty so we don’t deplete quickly
  constexpr msim::Qty maker_qty = 1'000'000;

  std::uint64_t oid = 1;
  for (int i = 0; i < n_resting; ++i) {
    const bool is_bid = ((i % 2) == 0);
    const int lvl = (i / 2) % levels + 1;

    msim::Order o{};
    o.id = static_cast<msim::OrderId>(oid++);
    o.owner = static_cast<msim::OwnerId>(1); // maker owner
    o.ts = 0;
    o.type = msim::OrderType::Limit;
    o.tif = msim::TimeInForce::GTC;
    o.qty = maker_qty;

    if (is_bid) {
      o.side = msim::Side::Buy;
      o.price = static_cast<msim::Price>(mid - lvl);
    } else {
      o.side = msim::Side::Sell;
      o.price = static_cast<msim::Price>(mid + lvl);
    }

    // Bypass matching for setup: add as resting
    (void)eng.book_mut().add_resting_limit(o);
  }

  return eng;
}

static void BM_ProcessMarketOrder(benchmark::State& state) {
  const int N = static_cast<int>(state.range(0));

  // Rebuild the book periodically outside timing so state doesn’t drift forever.
  // This makes measurements stable & comparable.
  constexpr std::uint64_t reset_every = 200'000;

  auto eng = make_engine_prefilled(N);

  std::uint64_t taker_id = 10'000'000;
  std::uint64_t iter = 0;

  for (auto _ : state) {
    if ((iter % reset_every) == 0 && iter != 0) {
      state.PauseTiming();
      eng = make_engine_prefilled(N);
      state.ResumeTiming();
    }

    msim::Order o{};
    o.id = static_cast<msim::OrderId>(taker_id++);
    o.owner = static_cast<msim::OwnerId>(999); // taker owner
    o.ts = static_cast<msim::Ts>(iter);        // deterministic monotone ts
    o.type = msim::OrderType::Market;
    o.tif = msim::TimeInForce::IOC;
    o.price = 0;
    o.qty = 1;

    // Alternate buy/sell so we hit both sides
    o.side = ((iter & 1ull) == 0ull) ? msim::Side::Buy : msim::Side::Sell;

    auto res = eng.process(o);
    benchmark::DoNotOptimize(res.filled_qty);
    benchmark::ClobberMemory();

    ++iter;
  }

  state.SetComplexityN(N);
}

BENCHMARK(BM_ProcessMarketOrder)
  ->Unit(benchmark::kMicrosecond)
  ->RangeMultiplier(10)
  ->Range(100, 10000)
  ->Repetitions(50)
  ->ReportAggregatesOnly(false)
  ->Complexity();

} // namespace
