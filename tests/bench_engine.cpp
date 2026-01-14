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
                       msim::Qty qty,
                       msim::TimeInForce tif = msim::TimeInForce::GTC) {
  msim::Order o{};
  o.id = id;
  o.owner = owner;
  o.ts = ts;
  o.side = side;
  o.type = msim::OrderType::Limit;
  o.price = price;
  o.qty = qty;
  o.tif = tif;
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
  if constexpr (requires(msim::Order x) { x.mkt_style = msim::MarketStyle::PureMarket; }) {
    o.mkt_style = msim::MarketStyle::PureMarket;
  }
  return o;
}

// Prefill a “warm” book around mid with both sides.
// maker_qty controls whether top-of-book depletes.
msim::MatchingEngine make_engine_prefilled(std::int32_t n_resting, msim::Qty maker_qty) {
  msim::MatchingEngine eng{msim::RuleSet(bench_rules_cfg())};

  constexpr msim::Price mid = 10'000;
  constexpr int levels = 10;

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

// Build a sweepable ASK ladder: exactly one order per price level at qty=1.
// A BUY market order of qty=K will sweep K price levels.
msim::MatchingEngine make_engine_sweepable_asks(std::int32_t levels) {
  msim::MatchingEngine eng{msim::RuleSet(bench_rules_cfg())};

  constexpr msim::Price mid = 10'000;
  std::uint64_t oid = 1;

  // Add bids (not strictly required, but keeps book realistic)
  for (std::int32_t i = 1; i <= levels; ++i) {
    (void)eng.book_mut().add_resting_limit(
        make_limit(static_cast<msim::OrderId>(oid++),
                   static_cast<msim::OwnerId>(1),
                   0,
                   msim::Side::Buy,
                   static_cast<msim::Price>(mid - i),
                   1));
  }

  // Add asks: one order each level, qty=1
  for (std::int32_t i = 1; i <= levels; ++i) {
    (void)eng.book_mut().add_resting_limit(
        make_limit(static_cast<msim::OrderId>(oid++),
                   static_cast<msim::OwnerId>(1),
                   0,
                   msim::Side::Sell,
                   static_cast<msim::Price>(mid + i),
                   1));
  }

  return eng;
}

void set_allocs_per_op(benchmark::State& state) {
  const double iters = static_cast<double>(state.iterations());
  if (iters <= 0.0) return;
  state.counters["allocs/op"] =
      static_cast<double>(alloc::scoped_total()) / iters;
}

// avoid benchmark::DoNotOptimize(const&) deprecation by using volatile sink
inline void sink_int_(int v) noexcept {
  volatile int sink = v;
  (void)sink;
}

} // namespace

// =====================
// Benchmarks
// =====================

// 1) Hot-path market order processing latency (small qty hits top of book)
static void BM_ProcessMarketOrder(benchmark::State& state) {
  const std::int32_t N = static_cast<std::int32_t>(state.range(0));
  auto eng = make_engine_prefilled(N, static_cast<msim::Qty>(1'000'000'000)); // prevent depletion

  std::uint64_t taker_id = 10'000'000;
  std::uint64_t iter = 0;

  alloc::reset();

  for (auto _ : state) {
    const msim::Side side = ((iter & 1ull) == 0ull) ? msim::Side::Buy : msim::Side::Sell;

    msim::Order o = make_market(static_cast<msim::OrderId>(taker_id++),
                                static_cast<msim::OwnerId>(999),
                                static_cast<msim::Ts>(iter),
                                side,
                                1);

    alloc::Scope s;
    auto res = eng.process(o);

    sink_int_(static_cast<int>(res.filled_qty));
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

    sink_int_(static_cast<int>(res.status));
    sink_int_(static_cast<int>(res.reject_reason));
    benchmark::ClobberMemory();
  }

  set_allocs_per_op(state);
}

BENCHMARK(BM_ProcessReject_InvalidQty)
  ->Unit(benchmark::kNanosecond)
  ->Repetitions(25);


// 3) Market order sweeping K PRICE LEVELS (true sweep across book depth)
static void BM_ProcessMarket_SweepKLevels(benchmark::State& state) {
  const std::int32_t K = static_cast<std::int32_t>(state.range(0));
  const std::int32_t max_levels = 2048;

  auto eng = make_engine_sweepable_asks(max_levels);

  std::uint64_t taker_id = 20'000'000;
  std::uint64_t iter = 0;

  alloc::reset();

  for (auto _ : state) {
    // reset book periodically so sweep remains comparable
    if ((iter % 50'000ull) == 0ull && iter != 0ull) {
      state.PauseTiming();
      eng = make_engine_sweepable_asks(max_levels);
      state.ResumeTiming();
    }

    msim::Order o = make_market(static_cast<msim::OrderId>(taker_id++),
                                static_cast<msim::OwnerId>(999),
                                static_cast<msim::Ts>(iter++),
                                msim::Side::Buy,
                                static_cast<msim::Qty>(K));

    alloc::Scope s;
    auto res = eng.process(o);

    sink_int_(static_cast<int>(res.filled_qty));
    benchmark::ClobberMemory();
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
    sink_int_(ok ? 1 : 0);
    benchmark::ClobberMemory();
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
    sink_int_(ok ? 1 : 0);

    state.PauseTiming();
    (void)eng.book_mut().cancel(oid);
    state.ResumeTiming();

    benchmark::ClobberMemory();
  }

  set_allocs_per_op(state);
}

BENCHMARK(BM_BookModifyQty_O1)
  ->Unit(benchmark::kNanosecond)
  ->Repetitions(25);


// 6) Depth snapshot top-N (L2 extraction)
static void BM_BookDepth_TopN(benchmark::State& state) {
  const std::size_t topN = static_cast<std::size_t>(state.range(0));
  auto eng = make_engine_prefilled(5000, static_cast<msim::Qty>(1'000'000'000));

  alloc::reset();

  for (auto _ : state) {
    alloc::Scope s;
    auto bids = eng.book().depth(msim::Side::Buy, topN);
    auto asks = eng.book().depth(msim::Side::Sell, topN);
    sink_int_(static_cast<int>(bids.size()));
    sink_int_(static_cast<int>(asks.size()));
    benchmark::ClobberMemory();
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


// =====================
// NEW BENCHMARKS YOU REQUESTED
// =====================

// 7) Throughput benchmark: market order processing in a stable hot book
// Reports items/sec via SetItemsProcessed.
static void BM_Throughput_ProcessMarketOrder(benchmark::State& state) {
  const std::int32_t N = static_cast<std::int32_t>(state.range(0));
  auto eng = make_engine_prefilled(N, static_cast<msim::Qty>(1'000'000'000));

  std::uint64_t taker_id = 50'000'000;
  std::uint64_t iter = 0;

  alloc::reset();

  for (auto _ : state) {
    const msim::Side side = ((iter & 1ull) == 0ull) ? msim::Side::Buy : msim::Side::Sell;

    msim::Order o = make_market(static_cast<msim::OrderId>(taker_id++),
                                static_cast<msim::OwnerId>(999),
                                static_cast<msim::Ts>(iter),
                                side,
                                1);

    alloc::Scope s;
    auto res = eng.process(o);
    sink_int_(static_cast<int>(res.filled_qty));
    ++iter;
  }

  set_allocs_per_op(state);
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
  state.SetComplexityN(N);
}

BENCHMARK(BM_Throughput_ProcessMarketOrder)
  ->Unit(benchmark::kNanosecond)
  ->UseRealTime()
  ->RangeMultiplier(10)
  ->Range(100, 10000)
  ->Repetitions(10)
  ->Complexity();


// 8) Resting limit insertion benchmark: book.add_resting_limit()
// Keeps book size stable by canceling the inserted order OUTSIDE timing.
static void BM_BookAddRestingLimit(benchmark::State& state) {
  const std::int32_t N = static_cast<std::int32_t>(state.range(0));
  auto eng = make_engine_prefilled(N, static_cast<msim::Qty>(100));

  constexpr msim::Price mid = 10'000;
  constexpr msim::Price px_bid = mid - 50; // safely non-crossing
  constexpr msim::Price px_ask = mid + 50;

  msim::OrderId next_id = 100'000'000;
  std::uint64_t iter = 0;

  alloc::reset();

  for (auto _ : state) {
    const bool is_bid = ((iter & 1ull) == 0ull);
    const msim::OrderId oid = next_id++;

    msim::Order o = make_limit(
        oid,
        static_cast<msim::OwnerId>(7),
        static_cast<msim::Ts>(iter),
        is_bid ? msim::Side::Buy : msim::Side::Sell,
        is_bid ? px_bid : px_ask,
        10,
        msim::TimeInForce::GTC);

    alloc::Scope s;
    const bool ok = eng.book_mut().add_resting_limit(o);
    sink_int_(ok ? 1 : 0);

    // remove it outside timing so the book doesn't grow
    state.PauseTiming();
    (void)eng.book_mut().cancel(oid);
    state.ResumeTiming();

    ++iter;
    benchmark::ClobberMemory();
  }

  set_allocs_per_op(state);
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
  state.SetComplexityN(N);
}

BENCHMARK(BM_BookAddRestingLimit)
  ->Unit(benchmark::kNanosecond)
  ->RangeMultiplier(10)
  ->Range(100, 10000)
  ->Repetitions(10)
  ->Complexity();


// 9) Crossing limit order benchmark: Limit IOC that crosses the spread (like a marketable limit)
static void BM_ProcessCrossingLimitIOC(benchmark::State& state) {
  const std::int32_t N = static_cast<std::int32_t>(state.range(0));
  auto eng = make_engine_prefilled(N, static_cast<msim::Qty>(1'000'000'000));

  constexpr msim::Price mid = 10'000;

  msim::OrderId next_id = 70'000'000;
  std::uint64_t iter = 0;

  alloc::reset();

  for (auto _ : state) {
    const bool buy = ((iter & 1ull) == 0ull);

    // Cross aggressively:
    // buy limit high -> takes best ask
    // sell limit low -> takes best bid
    const msim::Price px = buy ? static_cast<msim::Price>(mid + 100)
                               : static_cast<msim::Price>(mid - 100);

    msim::Order o = make_limit(static_cast<msim::OrderId>(next_id++),
                               static_cast<msim::OwnerId>(999),
                               static_cast<msim::Ts>(iter),
                               buy ? msim::Side::Buy : msim::Side::Sell,
                               px,
                               1,
                               msim::TimeInForce::IOC);

    alloc::Scope s;
    auto res = eng.process(o);

    sink_int_(static_cast<int>(res.filled_qty));
    benchmark::ClobberMemory();
    ++iter;
  }

  set_allocs_per_op(state);
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
  state.SetComplexityN(N);
}

BENCHMARK(BM_ProcessCrossingLimitIOC)
  ->Unit(benchmark::kNanosecond)
  ->RangeMultiplier(10)
  ->Range(100, 10000)
  ->Repetitions(10)
  ->Complexity();

