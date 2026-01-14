// tests/bench_engine.cpp
#include <benchmark/benchmark.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <optional>
#include <vector>

#include "msim/matching_engine.hpp"
#include "msim/order.hpp"
#include "msim/rules.hpp"
#include "msim/types.hpp"

// =====================
// Global allocation counter (benchmark-only TU)
// =====================
namespace alloc {

inline std::atomic<std::uint64_t> g_allocs{0};
inline std::atomic<std::uint64_t> g_scoped_allocs{0};

inline void reset() noexcept {
  g_allocs.store(0, std::memory_order_relaxed);
  g_scoped_allocs.store(0, std::memory_order_relaxed);
}

inline std::uint64_t total() noexcept {
  return g_allocs.load(std::memory_order_relaxed);
}

inline std::uint64_t scoped_total() noexcept {
  return g_scoped_allocs.load(std::memory_order_relaxed);
}

// Scope counts allocations that happened inside the timed critical path.
struct Scope {
  std::uint64_t start{0};
  Scope() noexcept : start(total()) {}
  ~Scope() noexcept {
    const std::uint64_t end = total();
    g_scoped_allocs.fetch_add(end - start, std::memory_order_relaxed);
  }
};

} // namespace alloc

// Override global new/delete so we can count allocations.
// (Only affects the msim_bench binary because this is a separate TU)
void* operator new(std::size_t n) {
  alloc::g_allocs.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void* operator new[](std::size_t n) {
  alloc::g_allocs.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void operator delete[](void* p) noexcept { std::free(p); }
// sized deletes (C++14+)
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

// =====================
// Helpers
// =====================
namespace {

msim::RulesConfig bench_rules_cfg() {
  msim::RulesConfig cfg{};
  // Keep defaults realistic. (Tick/lot rules still apply.)
  return cfg;
}

msim::Order make_limit(msim::OrderId id,
                       msim::OwnerId owner,
                       msim::Ts ts,
                       msim::Side side,
                       msim::Price price,
                       msim::Qty qty) {
  msim::Order o{};
  o.id = id;
  o.owner = owner;
  o.ts = ts;
  o.side = side;
  o.type = msim::OrderType::Limit;
  o.price = price;
  o.qty = qty;
  o.tif = msim::TimeInForce::GTC;
  return o;
}

msim::Order make_market(msim::OrderId id,
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
  o.price = 0;
  o.qty = qty;
  o.tif = msim::TimeInForce::IOC;
  // If MarketStyle exists in your build, keep it pure.
  if constexpr (requires(msim::Order x) { x.mkt_style = msim::MarketStyle::PureMarket; }) {
    o.mkt_style = msim::MarketStyle::PureMarket;
  }
  return o;
}

msim::MatchingEngine make_engine_prefilled(std::int32_t n_resting) {
  msim::MatchingEngine eng{msim::RuleSet(bench_rules_cfg())};

  constexpr msim::Price mid = 10'000;
  constexpr int levels = 10;
  constexpr msim::Qty maker_qty = 1'000'000;

  std::uint64_t oid = 1;
  for (std::int32_t i = 0; i < n_resting; ++i) {
    const bool is_bid = ((i % 2) == 0);
    const int lvl = (static_cast<int>(i / 2) % levels) + 1;

    msim::Order o{};
    o.id = static_cast<msim::OrderId>(oid++);
    o.owner = static_cast<msim::OwnerId>(1);
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

    (void)eng.book_mut().add_resting_limit(o);
  }

  return eng;
}

void set_allocs_per_op(benchmark::State& state) {
  const double iters = static_cast<double>(state.iterations());
  if (iters <= 0.0) return;
  state.counters["allocs/op"] =
      static_cast<double>(alloc::scoped_total()) / iters;
}

// =====================
// Benchmarks
// =====================

// 1) Hot-path market order processing (small qty, hits top of book)
static void BM_ProcessMarketOrder(benchmark::State& state) {
  const std::int32_t N = static_cast<std::int32_t>(state.range(0));
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

    const msim::Side side = ((iter & 1ull) == 0ull) ? msim::Side::Buy : msim::Side::Sell;
    msim::Order o = make_market(static_cast<msim::OrderId>(taker_id++),
                                static_cast<msim::OwnerId>(999),
                                static_cast<msim::Ts>(iter),
                                side,
                                1);

    alloc::Scope s;
    auto res = eng.process(o);

    benchmark::DoNotOptimize(res.filled_qty);
    benchmark::ClobberMemory();
    ++iter;
  }

  set_allocs_per_op(state);
  state.SetComplexityN(N);
}

BENCHMARK(BM_ProcessMarketOrder)
  ->Unit(benchmark::kNanosecond)
  ->RangeMultiplier(10)
  ->Range(100, 10000)
  ->Repetitions(25)
  ->Complexity();


// 2) Reject path (invalid qty) — should be very fast and allocation-free
static void BM_ProcessReject_InvalidQty(benchmark::State& state) {
  auto eng = msim::MatchingEngine(msim::RuleSet(bench_rules_cfg()));

  std::uint64_t oid = 1;
  std::uint64_t iter = 0;

  alloc::reset();

  for (auto _ : state) {
    msim::Order o = make_limit(static_cast<msim::OrderId>(oid++),
                              static_cast<msim::OwnerId>(999),
                              static_cast<msim::Ts>(iter++),
                              msim::Side::Buy,
                              10'000,
                              0 /* invalid */);

    alloc::Scope s;
    auto res = eng.process(o);

    benchmark::DoNotOptimize(static_cast<int>(res.status));
    benchmark::DoNotOptimize(static_cast<int>(res.reject_reason));
  }

  set_allocs_per_op(state);
}

BENCHMARK(BM_ProcessReject_InvalidQty)
  ->Unit(benchmark::kNanosecond)
  ->Repetitions(25);


// 3) Market order sweeping K levels (qty large enough to walk book depth)
static void BM_ProcessMarket_SweepKLevels(benchmark::State& state) {
  const std::int32_t N = 5000; // fixed warm book size
  const std::int32_t K = static_cast<std::int32_t>(state.range(0));

  auto eng = make_engine_prefilled(N);

  std::uint64_t taker_id = 20'000'000;
  std::uint64_t iter = 0;

  alloc::reset();

  for (auto _ : state) {
    // approximate sweep: qty grows with K
    const msim::Qty sweep_qty = static_cast<msim::Qty>(K);

    msim::Order o = make_market(static_cast<msim::OrderId>(taker_id++),
                                static_cast<msim::OwnerId>(999),
                                static_cast<msim::Ts>(iter++),
                                msim::Side::Buy,
                                sweep_qty);

    alloc::Scope s;
    auto res = eng.process(o);
    benchmark::DoNotOptimize(res.filled_qty);
  }

  set_allocs_per_op(state);
  state.SetComplexityN(K);
}

BENCHMARK(BM_ProcessMarket_SweepKLevels)
  ->Unit(benchmark::kNanosecond)
  ->RangeMultiplier(2)
  ->Range(1, 1024)
  ->Repetitions(25)
  ->Complexity();


// 4) O(1) cancel (uses locator map)
static void BM_BookCancel_O1(benchmark::State& state) {
  auto eng = msim::MatchingEngine(msim::RuleSet(bench_rules_cfg()));
  msim::OrderId next_id = 1;

  alloc::reset();

  for (auto _ : state) {
    state.PauseTiming();
    const msim::OrderId oid = next_id++;
    (void)eng.book_mut().add_resting_limit(
        make_limit(oid, static_cast<msim::OwnerId>(1), 0, msim::Side::Buy, 10'000, 10));
    state.ResumeTiming();

    alloc::Scope s;
    const bool ok = eng.book_mut().cancel(oid);

    // Avoid deprecated const-ref DoNotOptimize: use a non-const lvalue
    int ok_i = ok ? 1 : 0;
    benchmark::DoNotOptimize(ok_i);
  }

  set_allocs_per_op(state);
}

BENCHMARK(BM_BookCancel_O1)
  ->Unit(benchmark::kNanosecond)
  ->Repetitions(25);


// 5) O(1) reduce-only modify qty
static void BM_BookModifyQty_O1(benchmark::State& state) {
  auto eng = msim::MatchingEngine(msim::RuleSet(bench_rules_cfg()));
  msim::OrderId next_id = 1;

  alloc::reset();

  for (auto _ : state) {
    state.PauseTiming();
    const msim::OrderId oid = next_id++;
    (void)eng.book_mut().add_resting_limit(
        make_limit(oid, static_cast<msim::OwnerId>(1), 0, msim::Side::Buy, 10'000, 100));
    state.ResumeTiming();

    alloc::Scope s;
    const bool ok = eng.book_mut().modify_qty(oid, 50);

    int ok_i = ok ? 1 : 0;
    benchmark::DoNotOptimize(ok_i);

    state.PauseTiming();
    (void)eng.book_mut().cancel(oid);
    state.ResumeTiming();
  }

  set_allocs_per_op(state);
}

BENCHMARK(BM_BookModifyQty_O1)
  ->Unit(benchmark::kNanosecond)
  ->Repetitions(25);


// 6) Depth snapshot top-N (L2 extraction)
static void BM_BookDepth_TopN(benchmark::State& state) {
  const std::size_t topN = static_cast<std::size_t>(state.range(0));
  auto eng = make_engine_prefilled(5000);

  alloc::reset();

  for (auto _ : state) {
    alloc::Scope s;
    auto bids = eng.book().depth(msim::Side::Buy, topN);
    auto asks = eng.book().depth(msim::Side::Sell, topN);
    benchmark::DoNotOptimize(bids.size());
    benchmark::DoNotOptimize(asks.size());
  }

  set_allocs_per_op(state);
  state.SetComplexityN(static_cast<std::int64_t>(topN));
}

BENCHMARK(BM_BookDepth_TopN)
  ->Unit(benchmark::kNanosecond)
  ->RangeMultiplier(2)
  ->Range(1, 128)
  ->Repetitions(25)
  ->Complexity();

} // namespace
