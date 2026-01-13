#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "httplib.h"

#include "msim/live_world.hpp"
#include "msim/matching_engine.hpp"
#include "msim/order.hpp"
#include "msim/rules.hpp"
#include "msim/types.hpp"

// ---------------- Tunables (static storage => no lambda capture issues) ----------------
namespace {

// ---- Market maker ladder depth ----
constexpr std::size_t kMMLevels = 5;

// Keep top-of-book deep so aggressor market orders usually produce ~1 trade/order
constexpr msim::Qty kMMQtyL1   = 50'000;
constexpr msim::Qty kMMQtyStep = 10'000;

// How often the MM re-quotes the whole ladder
constexpr int kMMRefreshMs = 200;

// ---- Aggressor flow (drives trades/sec) ----
constexpr int kAggressorThreads = 2;

// Random sleep in [min,max] ms per order => controls orders/sec
constexpr int kAggressorSleepMinMs = 1;
constexpr int kAggressorSleepMaxMs = 3;

// Small orders are common
constexpr msim::Qty kAggrMinQ = 1;
constexpr msim::Qty kAggrMaxQ = 25;

// Big sweep orders occasionally to move last price across levels
constexpr int kBigTradeEveryN = 25;
constexpr msim::Qty kBigMinQ = 50;
constexpr msim::Qty kBigMaxQ = 250;

// ---- Fundamental process (drives “market drift”) ----
// Regime changes every ~3–6 seconds (buy vs sell pressure)
constexpr int kRegimeMinMs = 3000;
constexpr int kRegimeMaxMs = 6000;

// Fundamental random walk step every 50ms
constexpr int kFundStepMs = 50;

// Vol per step in ticks (0.3–1.2 are reasonable)
constexpr double kFundSigmaTicks = 0.60;

// Small drift per step when regime bias is active
constexpr double kFundDriftTicks = 0.15;

// Occasional discrete jump when regime changes (in ticks)
constexpr int kRegimeJumpMinTicks = 1;
constexpr int kRegimeJumpMaxTicks = 6;

} // namespace

// ---------------- Helpers ----------------
static long long get_ll(const httplib::Request& req, const char* key, long long def = 0) {
  if (!req.has_param(key)) return def;
  try { return std::stoll(req.get_param_value(key)); }
  catch (...) { return def; }
}

static std::string json_bool(bool v) { return v ? "true" : "false"; }

static std::string read_text_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static void set_no_cache(httplib::Response& res) {
  res.set_header("Cache-Control", "no-store, max-age=0");
  res.set_header("Pragma", "no-cache");
}

// ---------------- Compile-time detection for your OrderAck + LiveWorld API ----------------
template <class T>
static bool ack_accepted_(const T& a) {
  if constexpr (requires { a.accepted; }) return static_cast<bool>(a.accepted);
  else if constexpr (requires { a.ok; }) return static_cast<bool>(a.ok);
  else if constexpr (requires { a.status; }) return static_cast<int>(a.status) == 0;
  else return true;
}

template <class T>
static int ack_reason_(const T& a) {
  if constexpr (requires { a.reject_reason; }) return static_cast<int>(a.reject_reason);
  else if constexpr (requires { a.reason; }) return static_cast<int>(a.reason);
  else if constexpr (requires { a.reject; }) return static_cast<int>(a.reject);
  else return 0;
}

template <class T>
static long long ack_order_id_(const T& a) {
  if constexpr (requires { a.order_id; }) return static_cast<long long>(a.order_id);
  else if constexpr (requires { a.id; }) return static_cast<long long>(a.id);
  else if constexpr (requires { a.oid; }) return static_cast<long long>(a.oid);
  else return 0;
}

template <class LiveW>
static bool live_cancel_(LiveW& w, msim::OrderId id) {
  if constexpr (requires { w.cancel(id); }) return w.cancel(id);
  else if constexpr (requires { w.cancel_order(id); }) return w.cancel_order(id);
  else if constexpr (requires { w.cancel_order_id(id); }) return w.cancel_order_id(id);
  else return false;
}

template <class LiveW>
static bool live_modify_(LiveW& w, msim::OrderId id, msim::Qty q) {
  if constexpr (requires { w.modify_qty(id, q); }) return w.modify_qty(id, q);
  else if constexpr (requires { w.modify(id, q); }) return w.modify(id, q);
  else if constexpr (requires { w.modify_order_qty(id, q); }) return w.modify_order_qty(id, q);
  else return false;
}

// Depth level accessors (supports multiple layouts)
template <class L>
static long long level_price_(const L& x) {
  if constexpr (requires { x.price; }) return static_cast<long long>(x.price);
  else if constexpr (requires { x.px; }) return static_cast<long long>(x.px);
  else return 0;
}

template <class L>
static long long level_qty_(const L& x) {
  if constexpr (requires { x.qty; }) return static_cast<long long>(x.qty);
  else if constexpr (requires { x.total_qty; }) return static_cast<long long>(x.total_qty);
  else if constexpr (requires { x.size; }) return static_cast<long long>(x.size);
  else return 0;
}

template <class L>
static long long level_orders_(const L& x) {
  if constexpr (requires { x.order_count; }) return static_cast<long long>(x.order_count);
  else if constexpr (requires { x.orders; }) return static_cast<long long>(x.orders);
  else return 0;
}

// ---------------- Synthetic flow (MM ladder + aggressors) ----------------
static msim::OrderId make_oid_(msim::OwnerId owner, std::uint32_t seq) noexcept {
  const std::uint64_t hi = (static_cast<std::uint64_t>(owner) & 0xFFFF'FFFFull) << 32;
  const std::uint64_t lo = static_cast<std::uint64_t>(seq);
  return static_cast<msim::OrderId>(hi | lo);
}

struct FlowThreads {
  std::shared_ptr<std::atomic<bool>> running;
  std::thread regime_thread;
  std::thread fundamental_thread;
  std::thread mm_thread;
  std::vector<std::thread> aggressors;
};

static FlowThreads start_background_flow(msim::LiveWorld& world,
                                        const msim::RulesConfig& rcfg,
                                        std::uint64_t seed) {
  FlowThreads ft{};
  ft.running = std::make_shared<std::atomic<bool>>(true);
  auto run = ft.running;

  const msim::Price tick = std::max<msim::Price>(1, static_cast<msim::Price>(rcfg.tick_size_ticks));

  // fundamental in "price ticks"
  auto fundamental_px = std::make_shared<std::atomic<long long>>(static_cast<long long>(100 * tick));

  // bias: -1 sell pressure, 0 neutral, +1 buy pressure
  auto bias = std::make_shared<std::atomic<int>>(0);

  // ---- Regime thread: every 3–6s choose bias + optional jump ----
  ft.regime_thread = std::thread([run, fundamental_px, bias, seed, tick]() {
    using RT = std::mt19937_64::result_type;
    std::mt19937_64 rng(static_cast<RT>(seed) ^ static_cast<RT>(0xA5A5A5A5ull));
    std::uniform_int_distribution<int> sleep_ms(kRegimeMinMs, kRegimeMaxMs);
    std::uniform_int_distribution<int> pick(0, 2); // 0 sell, 1 neutral, 2 buy
    std::uniform_int_distribution<int> jump_ticks(kRegimeJumpMinTicks, kRegimeJumpMaxTicks);

    while (run->load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms(rng)));

      const int p = pick(rng);
      int b = 0;
      if (p == 0) b = -1;
      if (p == 2) b = +1;

      bias->store(b, std::memory_order_relaxed);

      // jump on regime change to create visible “level shifts”
      if (b != 0) {
        const long long j = static_cast<long long>(jump_ticks(rng)) * static_cast<long long>(tick);
        const long long cur = fundamental_px->load(std::memory_order_relaxed);
        long long nxt = (b > 0) ? (cur + j) : (cur - j);
        if (nxt < static_cast<long long>(tick)) nxt = static_cast<long long>(tick);
        fundamental_px->store(nxt, std::memory_order_relaxed);
      }
    }
  });

  // ---- Fundamental: random walk every 50ms with drift based on bias ----
  ft.fundamental_thread = std::thread([run, fundamental_px, bias, seed, tick]() {
    using RT = std::mt19937_64::result_type;
    std::mt19937_64 rng(static_cast<RT>(seed) ^ static_cast<RT>(0xC0FFEEull));
    std::normal_distribution<double> N01(0.0, 1.0);

    while (run->load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kFundStepMs));

      const int b = bias->load(std::memory_order_relaxed);
      const double drift = kFundDriftTicks * static_cast<double>(b);
      const double step_ticks = drift + kFundSigmaTicks * N01(rng);

      long long delta = static_cast<long long>(std::llround(step_ticks * static_cast<double>(tick)));
      // snap to tick
      if (delta != 0) delta = (delta / static_cast<long long>(tick)) * static_cast<long long>(tick);

      long long px = fundamental_px->load(std::memory_order_relaxed);
      px += delta;
      if (px < static_cast<long long>(tick)) px = static_cast<long long>(tick);

      fundamental_px->store(px, std::memory_order_relaxed);
    }
  });

  // ---- Market maker ladder ----
  ft.mm_thread = std::thread([run, &world, fundamental_px, seed, tick]() {
    constexpr msim::OwnerId MM_OWNER = static_cast<msim::OwnerId>(2);
    std::uint32_t seq = 1;

    std::array<msim::OrderId, kMMLevels> bid_ids{};
    std::array<msim::OrderId, kMMLevels> ask_ids{};
    bid_ids.fill(static_cast<msim::OrderId>(0));
    ask_ids.fill(static_cast<msim::OrderId>(0));

    using RT = std::mt19937_64::result_type;
    std::mt19937_64 rng(static_cast<RT>(seed) ^ static_cast<RT>(0xDEADBEEFull));
    std::uniform_int_distribution<int> jitter(-1, 1);

    while (run->load(std::memory_order_relaxed)) {
      // cancel old quotes (ignore failures: may be filled already)
      for (std::size_t i = 0; i < kMMLevels; ++i) {
        if (bid_ids[i] != 0) (void)live_cancel_(world, bid_ids[i]);
        if (ask_ids[i] != 0) (void)live_cancel_(world, ask_ids[i]);
        bid_ids[i] = 0;
        ask_ids[i] = 0;
      }

      const long long fpx_ll = fundamental_px->load(std::memory_order_relaxed);
      const msim::Price fpx =
          static_cast<msim::Price>(std::max<long long>(static_cast<long long>(tick), fpx_ll));

      for (std::size_t lvl = 0; lvl < kMMLevels; ++lvl) {
        const msim::Price off =
            static_cast<msim::Price>((static_cast<long long>(lvl) + 1LL) * static_cast<long long>(tick));
        const msim::Price j =
            static_cast<msim::Price>(jitter(rng) * static_cast<int>(tick));

        msim::Qty qty = static_cast<msim::Qty>(
            std::max<long long>(
                1LL,
                static_cast<long long>(kMMQtyL1) -
                    static_cast<long long>(kMMQtyStep) * static_cast<long long>(lvl)));

        // Bid
        {
          msim::Order b{};
          b.owner = MM_OWNER;
          b.id = make_oid_(MM_OWNER, seq++);
          b.side = msim::Side::Buy;
          b.type = msim::OrderType::Limit;

          msim::Price px = static_cast<msim::Price>(fpx - off + j);
          if (px < tick) px = tick;
          b.price = px;

          b.qty = qty;
          b.tif = msim::TimeInForce::GTC;
          (void)world.submit_order(b);
          bid_ids[lvl] = b.id;
        }

        // Ask
        {
          msim::Order a{};
          a.owner = MM_OWNER;
          a.id = make_oid_(MM_OWNER, seq++);
          a.side = msim::Side::Sell;
          a.type = msim::OrderType::Limit;

          msim::Price px = static_cast<msim::Price>(fpx + off + j);
          if (px < tick) px = static_cast<msim::Price>(tick + off);
          a.price = px;

          a.qty = qty;
          a.tif = msim::TimeInForce::GTC;
          (void)world.submit_order(a);
          ask_ids[lvl] = a.id;
        }
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(kMMRefreshMs));
    }

    // best-effort cleanup
    for (std::size_t i = 0; i < kMMLevels; ++i) {
      if (bid_ids[i] != 0) (void)live_cancel_(world, bid_ids[i]);
      if (ask_ids[i] != 0) (void)live_cancel_(world, ask_ids[i]);
    }
  });

  // ---- Aggressors (market orders => trades/sec + occasional sweeps) ----
  ft.aggressors.reserve(static_cast<std::size_t>(kAggressorThreads));
  for (int k = 0; k < kAggressorThreads; ++k) {
    ft.aggressors.emplace_back([run, &world, bias, seed, k]() {
      const msim::OwnerId OWNER = static_cast<msim::OwnerId>(100 + k);
      std::uint32_t seq = 1;

      using RT = std::mt19937_64::result_type;
      const RT s = static_cast<RT>(seed) ^ (static_cast<RT>(0x1234ABCDull) + static_cast<RT>(k));
      std::mt19937_64 rng(s);

      std::uniform_int_distribution<int> sleep_ms(kAggressorSleepMinMs, kAggressorSleepMaxMs);
      std::uniform_int_distribution<int> side01(0, 1);
      std::uniform_int_distribution<int> q_small(static_cast<int>(kAggrMinQ), static_cast<int>(kAggrMaxQ));
      std::uniform_int_distribution<int> q_big(static_cast<int>(kBigMinQ), static_cast<int>(kBigMaxQ));
      std::uniform_real_distribution<double> U01(0.0, 1.0);

      std::uint64_t local_count = 0;

      while (run->load(std::memory_order_relaxed)) {
        ++local_count;

        // biased side selection
        const int b = bias->load(std::memory_order_relaxed);
        bool buy = (side01(rng) == 1);
        if (b > 0) buy = (U01(rng) < 0.65);
        if (b < 0) buy = (U01(rng) < 0.35);

        msim::Qty q = static_cast<msim::Qty>(q_small(rng));
        if ((local_count % static_cast<std::uint64_t>(kBigTradeEveryN)) == 0ull) {
          q = static_cast<msim::Qty>(q_big(rng)); // occasional sweep
        }

        msim::Order o{};
        o.owner = OWNER;
        o.id = make_oid_(OWNER, seq++);
        o.side = buy ? msim::Side::Buy : msim::Side::Sell;

        o.type = msim::OrderType::Market;
        o.tif = msim::TimeInForce::IOC;
        o.price = 0;
        o.qty = q;

        if constexpr (requires(msim::Order x) { x.mkt_style = msim::MarketStyle::PureMarket; }) {
          o.mkt_style = msim::MarketStyle::PureMarket;
        }

        (void)world.submit_order(o);

        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms(rng)));
      }
    });
  }

  return ft;
}

static void stop_background_flow(FlowThreads& ft) {
  if (ft.running) ft.running->store(false, std::memory_order_relaxed);

  if (ft.regime_thread.joinable()) ft.regime_thread.join();
  if (ft.fundamental_thread.joinable()) ft.fundamental_thread.join();
  if (ft.mm_thread.joinable()) ft.mm_thread.join();

  for (auto& t : ft.aggressors) {
    if (t.joinable()) t.join();
  }
  ft.aggressors.clear();
}

// ---------------- main ----------------
int main(int argc, char** argv) {
  // args: [port] [seed]
  int port = 8080;
  std::uint64_t seed = 1;

  if (argc > 1) port = std::atoi(argv[1]);
  if (argc > 2) seed = static_cast<std::uint64_t>(std::strtoull(argv[2], nullptr, 10));

  // long horizon to behave "live"
  const double horizon_seconds = 3600.0 * 24.0 * 365.0;

  msim::RulesConfig rcfg{};
  msim::MatchingEngine eng{msim::RuleSet(rcfg)};
  msim::LiveWorld world{std::move(eng)};
  world.start(seed, horizon_seconds);

  // Start background flow
  FlowThreads flow = start_background_flow(world, rcfg, seed);

  httplib::Server svr;

  // Allow typing "exit" or "quit" to stop cleanly
  std::thread stdin_thread([&]() {
    std::string line;
    while (std::getline(std::cin, line)) {
      if (line == "exit" || line == "quit") {
        svr.stop();
        break;
      }
    }
  });

  // ---- Static files from ./web ----
  svr.Get("/", [&](const httplib::Request&, httplib::Response& res) {
    auto html = read_text_file("web/index.html");
    if (html.empty()) {
      res.set_content("<h1>Missing web/index.html</h1><p>Run from repo root.</p>", "text/html");
      return;
    }
    set_no_cache(res);
    res.set_content(html, "text/html; charset=utf-8");
  });

  svr.Get("/styles.css", [&](const httplib::Request&, httplib::Response& res) {
    auto css = read_text_file("web/styles.css");
    if (css.empty()) {
      res.set_content("/* Missing web/styles.css */", "text/css");
      return;
    }
    set_no_cache(res);
    res.set_content(css, "text/css; charset=utf-8");
  });

  svr.Get("/app.js", [&](const httplib::Request&, httplib::Response& res) {
    auto js = read_text_file("web/app.js");
    if (js.empty()) {
      res.set_content("// Missing web/app.js", "application/javascript");
      return;
    }
    set_no_cache(res);
    res.set_content(js, "application/javascript; charset=utf-8");
  });

  // ---- APIs ----
  svr.Get("/api/snapshot", [&](const httplib::Request&, httplib::Response& res) {
    constexpr std::size_t MAX_TRADES = 250;
    auto snap = world.snapshot(MAX_TRADES);

    std::ostringstream oss;
    oss << "{";
    oss << "\"ts\":" << snap.ts << ",";
    oss << "\"best_bid\":" << (snap.best_bid ? std::to_string(*snap.best_bid) : "null") << ",";
    oss << "\"best_ask\":" << (snap.best_ask ? std::to_string(*snap.best_ask) : "null") << ",";
    oss << "\"mid\":" << (snap.mid ? std::to_string(*snap.mid) : "null") << ",";
    oss << "\"last_trade\":" << (snap.last_trade ? std::to_string(*snap.last_trade) : "null") << ",";

    oss << "\"recent_trades\":[";
    for (std::size_t i = 0; i < snap.recent_trades.size(); ++i) {
      const auto& t = snap.recent_trades[i];
      if (i) oss << ",";
      oss << "{";
      oss << "\"id\":" << t.id << ",";
      oss << "\"ts\":" << t.ts << ",";
      oss << "\"price\":" << t.price << ",";
      oss << "\"qty\":" << t.qty << ",";
      oss << "\"maker_order_id\":" << t.maker_order_id << ",";
      oss << "\"taker_order_id\":" << t.taker_order_id;
      oss << "}";
    }
    oss << "]";
    oss << "}";

    set_no_cache(res);
    res.set_content(oss.str(), "application/json");
  });

  svr.Get("/api/depth", [&](const httplib::Request& req, httplib::Response& res) {
    std::size_t levels = 5;
    if (req.has_param("levels")) {
      try {
        const auto v = std::stoull(req.get_param_value("levels"));
        if (v > 0 && v <= 200) levels = static_cast<std::size_t>(v);
      } catch (...) {}
    }

    auto d = world.book_depth(levels);

    long long max_cum = 1;
    {
      long long acc = 0;
      for (const auto& x : d.asks) { acc += level_qty_(x); max_cum = std::max(max_cum, acc); }
      acc = 0;
      for (const auto& x : d.bids) { acc += level_qty_(x); max_cum = std::max(max_cum, acc); }
    }

    std::ostringstream oss;
    oss << "{";
    oss << "\"max_cum\":" << max_cum << ",";
    oss << "\"bids\":[";
    for (std::size_t i = 0; i < d.bids.size(); ++i) {
      const auto& x = d.bids[i];
      if (i) oss << ",";
      oss << "{"
          << "\"price\":" << level_price_(x) << ","
          << "\"qty\":" << level_qty_(x) << ","
          << "\"orders\":" << level_orders_(x)
          << "}";
    }
    oss << "],";
    oss << "\"asks\":[";
    for (std::size_t i = 0; i < d.asks.size(); ++i) {
      const auto& x = d.asks[i];
      if (i) oss << ",";
      oss << "{"
          << "\"price\":" << level_price_(x) << ","
          << "\"qty\":" << level_qty_(x) << ","
          << "\"orders\":" << level_orders_(x)
          << "}";
    }
    oss << "]";
    oss << "}";

    set_no_cache(res);
    res.set_content(oss.str(), "application/json");
  });

  svr.Post("/api/order", [&](const httplib::Request& req, httplib::Response& res) {
    const auto side_s = req.has_param("side") ? req.get_param_value("side") : "Buy";
    const auto type_s = req.has_param("type") ? req.get_param_value("type") : "Limit";
    const auto tif_s  = req.has_param("tif") ? req.get_param_value("tif") : "GTC";

    const auto id_ll = get_ll(req, "id", 0);
    const auto price_ll = get_ll(req, "price", 100);
    const auto qty_ll = get_ll(req, "qty", 1);

    msim::Order o{};
    o.owner = static_cast<msim::OwnerId>(999);
    if (id_ll > 0) o.id = static_cast<msim::OrderId>(id_ll);

    o.side = (side_s == "Sell") ? msim::Side::Sell : msim::Side::Buy;
    o.type = (type_s == "Market") ? msim::OrderType::Market : msim::OrderType::Limit;
    o.tif  = (tif_s == "IOC") ? msim::TimeInForce::IOC
           : (tif_s == "FOK") ? msim::TimeInForce::FOK
                              : msim::TimeInForce::GTC;

    o.price = static_cast<msim::Price>(price_ll);
    o.qty   = static_cast<msim::Qty>(qty_ll);

    if (o.type == msim::OrderType::Market) {
      o.price = 0;
      if constexpr (requires(msim::Order x) { x.mkt_style = msim::MarketStyle::PureMarket; }) {
        o.mkt_style = msim::MarketStyle::PureMarket;
      }
      if (o.tif == msim::TimeInForce::GTC) o.tif = msim::TimeInForce::IOC;
    }

    const auto ack = world.submit_order(o);

    std::ostringstream oss;
    oss << "{";
    oss << "\"accepted\":" << json_bool(ack_accepted_(ack)) << ",";
    oss << "\"reason\":" << ack_reason_(ack) << ",";
    oss << "\"order_id\":" << ack_order_id_(ack);
    oss << "}";
    set_no_cache(res);
    res.set_content(oss.str(), "application/json");
  });

  svr.Post("/api/cancel", [&](const httplib::Request& req, httplib::Response& res) {
    const auto id_ll = get_ll(req, "id", 0);
    const bool ok = live_cancel_(world, static_cast<msim::OrderId>(id_ll));
    set_no_cache(res);
    res.set_content(std::string("{\"ok\":") + json_bool(ok) + "}", "application/json");
  });

  svr.Post("/api/modify", [&](const httplib::Request& req, httplib::Response& res) {
    const auto id_ll = get_ll(req, "id", 0);
    const auto q_ll  = get_ll(req, "qty", 0);
    const bool ok = live_modify_(world,
                                static_cast<msim::OrderId>(id_ll),
                                static_cast<msim::Qty>(q_ll));
    set_no_cache(res);
    res.set_content(std::string("{\"ok\":") + json_bool(ok) + "}", "application/json");
  });

  std::cout << "MSIM gateway listening on http://localhost:" << port << "/\n";
  std::cout << "Run from repo root so it can read: web/index.html, web/styles.css, web/app.js\n";
  std::cout << "Type 'exit' (or 'quit') then press Enter to stop cleanly.\n";

  svr.listen("0.0.0.0", port);

  // Clean shutdown
  stop_background_flow(flow);
  if constexpr (requires { world.stop(); }) {
    world.stop();
  }
  if (stdin_thread.joinable()) stdin_thread.join();
  return 0;
}
