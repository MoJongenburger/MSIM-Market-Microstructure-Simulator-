#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "httplib.h"

#include "msim/live_world.hpp"
#include "msim/rules.hpp"
#include "msim/world.hpp"

#include "msim/agents/noise_trader.hpp"
#include "msim/agents/market_maker.hpp"

namespace fs = std::filesystem;

// -------------------- small file loader (fixes MSVC C2026) --------------------
static std::string read_file_binary(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return {};
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static fs::path exe_dir(const char* argv0) {
  try {
    if (argv0 && *argv0) {
      fs::path p(argv0);
      if (p.is_relative()) p = fs::absolute(p);
      if (p.has_parent_path()) return p.parent_path();
    }
  } catch (...) {
  }
  return fs::current_path();
}

static std::string load_index_html(const char* argv0) {
  const fs::path base = exe_dir(argv0);

  const std::vector<fs::path> candidates = {
      fs::current_path() / "web" / "index.html",
      fs::current_path() / ".." / "web" / "index.html",
      base / "web" / "index.html",
      base / ".." / "web" / "index.html",
      base / ".." / ".." / "web" / "index.html",
      base / ".." / ".." / ".." / "web" / "index.html",
  };

  for (const auto& p : candidates) {
    auto s = read_file_binary(p);
    if (!s.empty()) return s;
  }

  // fallback page (kept tiny on purpose)
  return R"HTML(<!doctype html>
<html><head><meta charset="utf-8"/><title>MSIM Gateway</title></head>
<body style="font-family:system-ui;margin:40px;">
<h2>MSIM Gateway UI not found</h2>
<p>Expected <code>web/index.html</code> next to your repo root.</p>
<p>Fix: create <code>web/index.html</code> (see README) and restart <code>msim_gateway</code>.</p>
</body></html>)HTML";
}

// -------------------- JSON helpers --------------------
static std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c; break;
    }
  }
  return out;
}

static std::string opt_to_json(std::optional<msim::Price> x) {
  if (!x) return "null";
  return std::to_string(*x);
}

static std::string status_to_str(msim::OrderStatus s) {
  switch (s) {
    case msim::OrderStatus::Accepted: return "Accepted";
    case msim::OrderStatus::Rejected: return "Rejected";
    default: return "Unknown";
  }
}

// Keep this robust: only reference enum members we KNOW exist.
static std::string reject_to_str(msim::RejectReason r) {
  using RR = msim::RejectReason;
  switch (r) {
    case RR::None: return "None";
    case RR::InvalidOrder: return "InvalidOrder";
    case RR::MarketHalted: return "MarketHalted";
    case RR::NoReferencePrice: return "NoReferencePrice";
    case RR::PriceNotAtLast: return "PriceNotAtLast";
    default: break;
  }
  return "Other(" + std::to_string(static_cast<int>(r)) + ")";
}

static msim::Side parse_side(const std::string& s) {
  if (s == "sell" || s == "Sell" || s == "S") return msim::Side::Sell;
  return msim::Side::Buy;
}

static msim::OrderType parse_type(const std::string& s) {
  if (s == "market" || s == "Market") return msim::OrderType::Market;
  return msim::OrderType::Limit;
}

static msim::TimeInForce parse_tif(const std::string& s) {
  if (s == "ioc" || s == "IOC") return msim::TimeInForce::IOC;
  if (s == "fok" || s == "FOK") return msim::TimeInForce::FOK;
  return msim::TimeInForce::GTC;
}

static long long to_ll_safe(const std::string& s, long long def = 0) {
  if (s.empty()) return def;
  try { return std::stoll(s); } catch (...) { return def; }
}

// -------------------- main --------------------
int main(int argc, char** argv) {
  int port = 8080;
  uint64_t seed = 1;
  double horizon_s = 3600.0; // run for 1h by default
  if (argc >= 2) port = std::atoi(argv[1]);
  if (argc >= 3) seed = static_cast<uint64_t>(std::stoull(argv[2]));
  if (argc >= 4) horizon_s = std::stod(argv[3]);

  msim::RulesConfig rcfg{};
  msim::MatchingEngine eng{msim::RuleSet(rcfg)};

  msim::LiveWorld world{std::move(eng)};

  // Agents
  {
    msim::agents::NoiseTraderConfig nt{};
    nt.tick_size = 1;
    nt.lot_size = 1;
    nt.default_mid = 100;

    world.add_agent(std::make_unique<msim::agents::NoiseTrader>(msim::OwnerId{1}, nt));

    msim::MarketMakerParams mm{};
    world.add_agent(std::make_unique<msim::MarketMaker>(msim::OwnerId{2}, rcfg, mm));
  }

  msim::WorldConfig wcfg{};
  wcfg.dt_ns = 1'000'000; // 1ms
  world.start(seed, horizon_s, wcfg);

  httplib::Server svr;

  svr.Get("/", [&](const httplib::Request&, httplib::Response& res) {
    res.set_content(load_index_html(argc > 0 ? argv[0] : nullptr), "text/html; charset=utf-8");
  });

  // Snapshot: top + last trade + recent trades
  svr.Get("/api/snapshot", [&](const httplib::Request& req, httplib::Response& res) {
    long long mt = req.has_param("max_trades") ? to_ll_safe(req.get_param_value("max_trades"), 50) : 50;
    if (mt < 0) mt = 0;
    if (mt > 500) mt = 500;
    const std::size_t max_trades = static_cast<std::size_t>(mt);

    auto snap = world.snapshot(max_trades);

    std::ostringstream o;
    o << "{";
    o << "\"ts\":" << snap.ts << ",";
    o << "\"best_bid\":" << opt_to_json(snap.best_bid) << ",";
    o << "\"best_ask\":" << opt_to_json(snap.best_ask) << ",";
    o << "\"mid\":" << opt_to_json(snap.mid) << ",";
    o << "\"last_trade\":" << opt_to_json(snap.last_trade) << ",";
    o << "\"recent_trades\":[";
    for (std::size_t i = 0; i < snap.recent_trades.size(); ++i) {
      const auto& t = snap.recent_trades[i];
      if (i) o << ",";
      o << "{"
        << "\"id\":" << t.id
        << ",\"ts\":" << t.ts
        << ",\"price\":" << t.price
        << ",\"qty\":" << t.qty
        << ",\"maker_order_id\":" << t.maker_order_id
        << ",\"taker_order_id\":" << t.taker_order_id
        << "}";
    }
    o << "]";
    o << "}";
    res.set_content(o.str(), "application/json");
  });

  // Mid series windowed
  svr.Get("/api/mid_series", [&](const httplib::Request& req, httplib::Response& res) {
    long long win_s = req.has_param("window_s") ? to_ll_safe(req.get_param_value("window_s"), 60) : 60;
    if (win_s < 1) win_s = 1;
    const msim::Ts window_ns = static_cast<msim::Ts>(win_s) * 1'000'000'000ll;

    auto pts = world.mid_series(window_ns);

    std::ostringstream o;
    o << "{";
    o << "\"points\":[";
    for (std::size_t i = 0; i < pts.size(); ++i) {
      if (i) o << ",";
      o << "{"
        << "\"ts\":" << pts[i].ts << ","
        << "\"mid\":" << (pts[i].mid ? std::to_string(*pts[i].mid) : std::string("null"))
        << "}";
    }
    o << "]";
    o << "}";
    res.set_content(o.str(), "application/json");
  });

  // Order book: top-N aggregated price levels
  svr.Get("/api/book", [&](const httplib::Request& req, httplib::Response& res) {
    long long lv = req.has_param("levels") ? to_ll_safe(req.get_param_value("levels"), 5) : 5;
    if (lv < 1) lv = 1;
    if (lv > 50) lv = 50;
    const std::size_t levels = static_cast<std::size_t>(lv);

    const auto bd = world.book_depth(levels);

    std::ostringstream o;
    o << "{";
    o << "\"bids\":[";
    for (std::size_t i = 0; i < bd.bids.size(); ++i) {
      if (i) o << ",";
      o << "{"
        << "\"price\":" << bd.bids[i].price
        << ",\"qty\":" << bd.bids[i].qty
        << "}";
    }
    o << "],";
    o << "\"asks\":[";
    for (std::size_t i = 0; i < bd.asks.size(); ++i) {
      if (i) o << ",";
      o << "{"
        << "\"price\":" << bd.asks[i].price
        << ",\"qty\":" << bd.asks[i].qty
        << "}";
    }
    o << "]";
    o << "}";
    res.set_content(o.str(), "application/json");
  });

  // Place order
  svr.Post("/api/order", [&](const httplib::Request& req, httplib::Response& res) {
    const std::string side_s = req.get_param_value("side");
    const std::string type_s = req.get_param_value("type");
    const std::string tif_s  = req.get_param_value("tif");

    const long long price_ll = req.has_param("price") ? to_ll_safe(req.get_param_value("price"), 0) : 0;
    const long long qty_ll   = req.has_param("qty") ? to_ll_safe(req.get_param_value("qty"), 0) : 0;

    msim::Order o{};
    o.owner = msim::OwnerId{999}; // manual trader owner id
    o.side  = parse_side(side_s);
    o.type  = parse_type(type_s);
    o.tif   = parse_tif(tif_s);
    o.qty   = static_cast<msim::Qty>(qty_ll);

    if (o.type == msim::OrderType::Limit) {
      o.price = static_cast<msim::Price>(price_ll);
    } else {
      o.price = 0;
      o.mkt_style = msim::MarketStyle::PureMarket;
      if (o.tif == msim::TimeInForce::GTC) o.tif = msim::TimeInForce::IOC;
    }

    auto ack = world.submit_order(o);

    std::ostringstream out;
    out << "{"
        << "\"id\":" << ack.id << ","
        << "\"status\":\"" << json_escape(status_to_str(ack.status)) << "\","
        << "\"reject_reason\":\"" << json_escape(reject_to_str(ack.reject_reason)) << "\""
        << "}";
    res.set_content(out.str(), "application/json");
  });

  // Cancel
  svr.Post("/api/cancel", [&](const httplib::Request& req, httplib::Response& res) {
    const long long id_ll = req.has_param("id") ? to_ll_safe(req.get_param_value("id"), 0) : 0;
    const bool ok = world.cancel_order(static_cast<msim::OrderId>(id_ll));

    std::ostringstream o;
    o << "{\"ok\":" << (ok ? "true" : "false") << "}";
    res.set_content(o.str(), "application/json");
  });

  // Modify qty
  svr.Post("/api/modify", [&](const httplib::Request& req, httplib::Response& res) {
    const long long id_ll = req.has_param("id") ? to_ll_safe(req.get_param_value("id"), 0) : 0;
    const long long q_ll  = req.has_param("new_qty") ? to_ll_safe(req.get_param_value("new_qty"), 0) : 0;

    const bool ok = world.modify_qty(static_cast<msim::OrderId>(id_ll), static_cast<msim::Qty>(q_ll));

    std::ostringstream o;
    o << "{\"ok\":" << (ok ? "true" : "false") << "}";
    res.set_content(o.str(), "application/json");
  });

  std::cout << "MSIM gateway running on http://localhost:" << port
            << " (seed=" << seed << ", horizon_s=" << horizon_s << ")\n";

  svr.listen("0.0.0.0", port);

  world.stop();
  return 0;
}
