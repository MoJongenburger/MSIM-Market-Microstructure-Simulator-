#include <benchmark/benchmark.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <optional>
#include <random>
#include <vector>

#include "msim/book.hpp"
#include "msim/matching_engine.hpp"
#include "msim/order.hpp"
#include "msim/rules.hpp"
#include "msim/types.hpp"

// ---------------- Benchmark-only allocation counter ----------------
// This only impacts the benchmark executable, not msim lib/tests.
// Counting only happens inside alloc::Scope.
namespace alloc {
inline std::atomic<std::uint64_t> g_allocs{0};
inline thread_local bool g_counting = false;

struct Scope {
  Scope() noexcept { g_counting = true; }
  ~Scope() noexcept { g_counting = false; }
};

inline void reset() noexcept { g_allocs.store(0, std::memory_order_relaxed); }
inline std::uint64_t total() noexcept { return g_allocs.load(std::memory_order_relaxed); }
} // namespace alloc

void* operator new(std::size_t n) {
  if (alloc::g_counting) alloc::g_allocs.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }

void* operator new[](std::size_t n) {
  if (alloc::g_counting) alloc::g_allocs.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void operator delete[](void* p) noexcept { std::free(p); }

// sized delete (C++14+)
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

msim::RulesConfig bench_rules_cfg() {
  msim::RulesConfig cfg{};
  // Make benchmark results stable (avoid “defaults changed” surprises)
  cfg.tick_size_ticks = 1;
  cfg.lot_size = 1;
  cfg.min_qty = 1;
  return cfg;
}

static msim::Order make_limit(msim::OrderId id,
                             msim::OwnerId owner,
                             msim::Ts ts,
                             msim::Side side,
                             msim::Price px,
                             msim::Qty qty) {
  msim::Order o{};
  o.id = id;
  o.owner = owner;
  o.ts = ts;
  o.side = side;
  o.type = msim::OrderType::Limit;
  o.tif = msim::TimeInForce::GTC;
  o.price = px;
  o.qty = qty;

  // If your Order has mkt_style, keep it deterministic (compile-time detected).
  if constexpr (requires(msim::Order x) { x.mkt_style = msim::MarketStyle::PureMarket; }) {
    o.mkt_style = msim::MarketStyle::PureMarket;
  }
  return o;
}

static msim::Order make_market(msim::OrderId id,
                              msim::OwnerId owner,
                              msim::Ts ts,
                              msim::Side side,
                              msim::Qty qty) {
  msim::Order o{};
  o.id = id;
  o.owner = owner;
  o.ts = ts;
  o.side = side;
  o.type = msim::OrderType::Market;
  o.tif = msim::TimeInForce::IOC;
  o.price = 0;
  o.qty = qty;

  if constexpr (requires(msim::Order x) { x.mkt_style = msim::MarketStyle::PureMarket; }) {
    o.mkt_style = msim::MarketStyle::PureMarket;
  }
  return o;
}

msim::MatchingEngine make_engine_prefilled(std::int32_t n_resting) {
  auto cfg = bench_rules_cfg();
  msim::MatchingEngine eng{msim::RuleSet(cfg)};

  constexpr msim::Price mid = 10000; // ticks
  constexpr int levels = 10;
  constexpr msim::Qty maker_qty = 1'000'000;

  std::uint64_t oid = 1;
  for (int i = 0; i < n_resting; ++i) {
    const bool is_bid = ((i % 2) == 0);
    const int lvl = (i / 2) % levels + 1;

    const auto id = static_cast<msim::OrderId>(oid++);
    const auto owner = static_cast<msim::OwnerId>(1);
    const msim::Price px =
        is_bid ? static_cast<msim::Price>(mid - lvl)
               : static_cast<msim::Price>(mid + lvl);

    auto o = make_limit(id, owner, 0, is_bid ? msim::Side::Buy : msim::Side::Sell, px, maker_qty);
    (void)eng.book_mut().add_resting_limit(o);
  }

  return eng;
}

static void prefill_asks_levels(msim::MatchingEngine& eng,
                                msim::Price start_px,
                                msim::Price tick,
                                int levels,
                                msim::Qty qty_per_level,
                                msim::OwnerId owner,
                                msim::OrderId& next_id,
                                msim::Ts ts) {
  for (int i = 0; i < levels; ++i) {
    const msim::Price px = static_cast<msim::Price>(start_px + static_cast<msim::Price>(i) * tick);
    auto o = make_limit(next_id++, owner, ts, msim::Side::Sell, px, qty_per_level);
    (void)eng.book_mut().add_resting_limit(o);
  }
}

static void prefill_bids_levels(msim::MatchingEngine& eng,
                                msim::Price start_px,
                                msim::Price tick,
                                int levels,
                                msim::Qty qty_per_level,
                                msim::OwnerId owner,
                                msim::OrderId& next_id,
                                msim::Ts ts) {
  for (int i = 0; i < levels; ++i) {
    const msim::Price px = static_cast<msim::Price>(start_px - static_cast<msim::Price>(i) * tick);
    auto o = make_limit(next_id++, owner, ts, msim::Side::Buy, px, qty_per_level);
    (void)eng.book_mut().add_resting_limit(o);
  }
}

// ---------------- Existing benchmark (kept) ----------------
static void BM_ProcessMarketOrder(benchmark::State& state) {
  const int N = static_cast<int>(state.range(0));
  constexpr std::uint64_t reset_every = 200'000;

  auto eng = make_engine_prefilled(N);

  std::uint64_t taker_id = 10'000'000;
  std::uint64_t iter = 0;

  alloc::reset();

  for (auto _ : state) {
    if ((iter % reset_every) == 0 && iter != 0) {
      state.PauseTiming();
      eng = make_engine_prefilled(N);
      state.ResumeTiming();
    }

    const auto oid = static_cast<msim::OrderId>(taker_id++);
    const auto owner = static_cast<msim::OwnerId>(999);
    const msim::Side side = ((iter & 1ull) == 0ull) ? msim::Side::Buy : msim::Side::Sell;

    auto o = make_market(oid, owner, static_cast<msim::Ts>(iter), side, 1);

    alloc::Scope s;
    auto res = eng.process(o);

    benchmark::DoNotOptimize(res.filled_qty);
    benchmark::ClobberMemory();

    ++iter;
  }

  const double iters = static_cast<double>(state.iterations());
  state.counters["allocs/op"] = alloc::total() / iters;
  state.SetComplexityN(N);
}

BENCHMARK(BM_ProcessMarketOrder)
  ->Unit(benchmark::kMicrosecond)
  ->RangeMultiplier(10)
  ->Range(100, 10000)
  ->Repetitions(50)
  ->ReportAggregatesOnly(false)
  ->Complexity();

// ---------------- New benchmarks ----------------

// 1) Reject path: invalid qty (rules + validation overhead)
static void BM_ProcessReject_InvalidQty(benchmark::State& state) {
  auto eng = msim::MatchingEngine(msim::RuleSet(bench_rules_cfg()));
  std::uint64_t oid = 1;

  alloc::reset();

  for (auto _ : state) {
    msim::Order o{};
    o.id = static_cast<msim::OrderId>(oid++);
    o.owner = static_cast<msim::OwnerId>(123);
    o.ts = 1;
    o.side = msim::Side::Buy;
    o.type = msim::OrderType::Limit;
    o.tif = msim::TimeInForce::GTC;
    o.price = 100;
    o.qty = 0; // invalid by cfg.min_qty = 1

    alloc::Scope s;
    auto res = eng.process(o);
    benchmark::DoNotOptimize(res.status);
  }

  const double iters = static_cast<double>(state.iterations());
  state.counters["allocs/op"] = alloc::total() / iters;
}
BENCHMARK(BM_ProcessReject_InvalidQty)->Unit(benchmark::kMicrosecond);

// 2) Sweep across K levels (measures deeper matching work)
static void BM_ProcessMarket_SweepKLevels(benchmark::State& state) {
  const int K = static_cast<int>(state.range(0));
  alloc::reset();

  msim::OrderId next_id = 1;

  for (auto _ : state) {
    state.PauseTiming();
    auto eng = msim::MatchingEngine(msim::RuleSet(bench_rules_cfg()));
    msim::OrderId local = next_id;

    // K levels, qty=1 each => market buy qty=K sweeps K trades
    prefill_asks_levels(eng, 100, 1, K, 1, static_cast<msim::OwnerId>(1), local, 0);
    state.ResumeTiming();

    auto mkt = make_market(local + 1,
                           static_cast<msim::OwnerId>(2),
                           1,
                           msim::Side::Buy,
                           static_cast<msim::Qty>(K));

    alloc::Scope s;
    auto res = eng.process(mkt);
    benchmark::DoNotOptimize(res.filled_qty);

    next_id = local + 2;
  }

  const double iters = static_cast<double>(state.iterations());
  state.counters["allocs/op"] = alloc::total() / iters;
  state.SetComplexityN(K);
}
BENCHMARK(BM_ProcessMarket_SweepKLevels)
  ->Arg(2)->Arg(5)->Arg(10)->Arg(20)
  ->Complexity()
  ->Unit(benchmark::kMicrosecond);

// 3) O(1) cancel
static void BM_BookCancel_O1(benchmark::State& state) {
  auto eng = msim::MatchingEngine(msim::RuleSet(bench_rules_cfg()));
  msim::OrderId next_id = 1;

  alloc::reset();

  for (auto _ : state) {
    state.PauseTiming();
    const msim::OrderId oid = next_id++;
    (void)eng.book_mut().add_resting_limit(
        make_limit(oid, static_cast<msim::OwnerId>(1), 0, msim::Side::Buy, 100, 10));
    state.ResumeTiming();

    alloc::Scope s;
    const bool ok = eng.book_mut().cancel(oid);
    benchmark::DoNotOptimize(ok);
  }

  const double iters = static_cast<double>(state.iterations());
  state.counters["allocs/op"] = alloc::total() / iters;
}
BENCHMARK(BM_BookCancel_O1)->Unit(benchmark::kMicrosecond);

// 4) O(1) modify (reduce-only)
static void BM_BookModifyQty_O1(benchmark::State& state) {
  auto eng = msim::MatchingEngine(msim::RuleSet(bench_rules_cfg()));
  msim::OrderId next_id = 1;

  alloc::reset();

  for (auto _ : state) {
    state.PauseTiming();
    const msim::OrderId oid = next_id++;
    (void)eng.book_mut().add_resting_limit(
        make_limit(oid, static_cast<msim::OwnerId>(1), 0, msim::Side::Buy, 100, 100));
    state.ResumeTiming();

    alloc::Scope s;
    const bool ok = eng.book_mut().modify_qty(oid, 50);
    benchmark::DoNotOptimize(ok);

    // cleanup not measured
    state.PauseTiming();
    (void)eng.book_mut().cancel(oid);
    state.ResumeTiming();
  }

  const double iters = static_cast<double>(state.iterations());
  state.counters["allocs/op"] = alloc::total() / iters;
}
BENCHMARK(BM_BookModifyQty_O1)->Unit(benchmark::kMicrosecond);

// 5) Depth snapshot (cost impacts ladder/UI)
static void BM_BookDepth_TopN(benchmark::State& state) {
  const int N = static_cast<int>(state.range(0));
  auto eng = msim::MatchingEngine(msim::RuleSet(bench_rules_cfg()));

  msim::OrderId next_id = 1;
  // Build a “fat” book so depth has real work.
  prefill_bids_levels(eng, 2000, 1, 2000, 10, static_cast<msim::OwnerId>(1), next_id, 0);
  prefill_asks_levels(eng, 2001, 1, 2000, 10, static_cast<msim::OwnerId>(2), next_id, 0);

  alloc::reset();

  for (auto _ : state) {
    alloc::Scope s;
    auto bids = eng.book().depth(msim::Side::Buy, static_cast<std::size_t>(N));
    auto asks = eng.book().depth(msim::Side::Sell, static_cast<std::size_t>(N));
    benchmark::DoNotOptimize(bids);
    benchmark::DoNotOptimize(asks);
  }

  const double iters = static_cast<double>(state.iterations());
  state.counters["allocs/op"] = alloc::total() / iters;
  state.SetComplexityN(N);
}
BENCHMARK(BM_BookDepth_TopN)
  ->Arg(5)->Arg(10)->Arg(20)->Arg(50)->Arg(100)
  ->Complexity()
  ->Unit(benchmark::kMicrosecond);

} // namespace
