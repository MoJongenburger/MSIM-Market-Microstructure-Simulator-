#include <algorithm>
#include <cstdlib>
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

static std::string html_page() {
  return R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <title>MSIM Gateway</title>
  <style>
    body { font-family: system-ui, -apple-system, Segoe UI, Roboto, sans-serif; margin: 16px; }
    .row { display:flex; gap:16px; align-items:flex-start; }
    .card { border:1px solid #ddd; border-radius:12px; padding:12px; box-shadow: 0 1px 2px rgba(0,0,0,.05); }
    .w50 { width: 50%; }
    .mono { font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace; }
    table { width:100%; border-collapse: collapse; }
    th, td { padding: 6px; border-bottom: 1px solid #eee; text-align:left; }
    .muted { color:#666; font-size: 12px; }
    .controls input, .controls select { padding:6px; margin-right:8px; }
    canvas { width: 100%; height: 240px; border:1px solid #eee; border-radius: 10px; }
  </style>
</head>
<body>
  <h1>MSIM — Local Gateway</h1>
  <div class="muted">
    Live top-of-book + trades + mid-series. Place orders into the same matching engine the agents trade on.
  </div>

  <div class="row" style="margin-top:12px;">
    <div class="card w50">
      <div><b>Market</b></div>
      <div class="mono" id="topline" style="margin-top:8px;">loading...</div>
      <div style="margin-top:10px;">
        <label>Chart window:</label>
        <select id="windowSel">
          <option value="10">10s</option>
          <option value="60" selected>1m</option>
          <option value="300">5m</option>
          <option value="900">15m</option>
          <option value="3600">1h</option>
          <option value="86400">1d</option>
        </select>
      </div>
      <div style="margin-top:10px;">
        <canvas id="chart" width="800" height="240"></canvas>
      </div>
      <div class="muted" style="margin-top:8px;">
        Note: full L2 depth view is the next increment (we can expose top-N levels via the book API).
      </div>
    </div>

    <div class="card w50">
      <div><b>Order Entry</b></div>
      <div class="controls" style="margin-top:10px;">
        <select id="side">
          <option value="buy">buy</option>
          <option value="sell">sell</option>
        </select>
        <select id="type">
          <option value="limit">limit</option>
          <option value="market">market</option>
        </select>
        <input id="price" type="number" placeholder="price" />
        <input id="qty" type="number" placeholder="qty" value="10" />
        <select id="tif">
          <option value="gtc" selected>GTC</option>
          <option value="ioc">IOC</option>
          <option value="fok">FOK</option>
        </select>
        <button id="sendBtn">Send</button>
      </div>

      <div style="margin-top:10px;">
        <div class="muted">Cancel / Modify</div>
        <input id="oid" type="number" placeholder="order_id" />
        <input id="newQty" type="number" placeholder="new_qty" />
        <button id="cancelBtn">Cancel</button>
        <button id="modifyBtn">ModifyQty</button>
      </div>

      <div style="margin-top:12px;">
        <div><b>Recent Trades</b></div>
        <table class="mono" id="tradesTbl" style="margin-top:6px;">
          <thead><tr><th>ts</th><th>px</th><th>qty</th><th>maker</th><th>taker</th></tr></thead>
          <tbody></tbody>
        </table>
      </div>
    </div>
  </div>

<script>
  const topline = document.getElementById('topline');
  const tradesBody = document.querySelector('#tradesTbl tbody');
  const canvas = document.getElementById('chart');
  const ctx = canvas.getContext('2d');
  const windowSel = document.getElementById('windowSel');

  async function apiGet(url) {
    const r = await fetch(url);
    return await r.json();
  }

  async function apiPost(url, obj) {
    const body = new URLSearchParams(obj);
    const r = await fetch(url, { method: 'POST', headers: {'Content-Type':'application/x-www-form-urlencoded'}, body });
    return await r.json();
  }

  function drawLine(points) {
    ctx.clearRect(0,0,canvas.width,canvas.height);
    if (!points.length) return;

    let min = Infinity, max = -Infinity;
    for (const p of points) {
      if (p.mid === null) continue;
      min = Math.min(min, p.mid);
      max = Math.max(max, p.mid);
    }
    if (!isFinite(min) || !isFinite(max) || min === max) { min -= 1; max += 1; }

    const pad = 10;
    const W = canvas.width, H = canvas.height;

    const t0 = points[0].ts;
    const t1 = points[points.length - 1].ts;

    function xOf(ts) {
      if (t1 === t0) return pad;
      return pad + (W - 2*pad) * (ts - t0) / (t1 - t0);
    }
    function yOf(mid) {
      return pad + (H - 2*pad) * (1 - (mid - min) / (max - min));
    }

    ctx.beginPath();
    let started = false;
    for (const p of points) {
      if (p.mid === null) continue;
      const x = xOf(p.ts);
      const y = yOf(p.mid);
      if (!started) { ctx.moveTo(x,y); started = true; }
      else ctx.lineTo(x,y);
    }
    ctx.stroke();
  }

  async function refresh() {
    const snap = await apiGet('/api/snapshot?max_trades=30');
    const bb = snap.best_bid === null ? '-' : snap.best_bid;
    const ba = snap.best_ask === null ? '-' : snap.best_ask;
    const mid = snap.mid === null ? '-' : snap.mid;
    const lt = snap.last_trade === null ? '-' : snap.last_trade;
    topline.textContent = `ts=${snap.ts}  bid=${bb}  ask=${ba}  mid=${mid}  last=${lt}`;

    tradesBody.innerHTML = '';
    for (const t of snap.recent_trades.slice(-15).reverse()) {
      const tr = document.createElement('tr');
      tr.innerHTML = `<td>${t.ts}</td><td>${t.price}</td><td>${t.qty}</td><td>${t.maker_order_id}</td><td>${t.taker_order_id}</td>`;
      tradesBody.appendChild(tr);
    }

    const winS = parseInt(windowSel.value, 10);
    const series = await apiGet(`/api/mid_series?window_s=${winS}`);
    drawLine(series.points);
  }

  document.getElementById('sendBtn').onclick = async () => {
    const side = document.getElementById('side').value;
    const type = document.getElementById('type').value;
    const price = document.getElementById('price').value;
    const qty = document.getElementById('qty').value;
    const tif = document.getElementById('tif').value;

    const res = await apiPost('/api/order', { side, type, price, qty, tif });
    alert(`submitted id=${res.id} status=${res.status} reason=${res.reject_reason}`);
  };

  document.getElementById('cancelBtn').onclick = async () => {
    const id = document.getElementById('oid').value;
    const res = await apiPost('/api/cancel', { id });
    alert(`cancel queued: ${res.ok}`);
  };

  document.getElementById('modifyBtn').onclick = async () => {
    const id = document.getElementById('oid').value;
    const new_qty = document.getElementById('newQty').value;
    const res = await apiPost('/api/modify', { id, new_qty });
    alert(`modify queued: ${res.ok}`);
  };

  setInterval(refresh, 250);
  refresh();
</script>
</body>
</html>
)HTML";
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

static std::string reject_to_str(msim::RejectReason r) {
  using RR = msim::RejectReason;
  switch (r) {
    case RR::None: return "None";
    case RR::InvalidOrder: return "InvalidOrder";
    case RR::MarketHalted: return "MarketHalted";
    case RR::TickViolation: return "TickViolation";
    case RR::LotViolation: return "LotViolation";
    case RR::MinQtyViolation: return "MinQtyViolation";
    case RR::NoReferencePrice: return "NoReferencePrice";
    case RR::PriceNotAtLast: return "PriceNotAtLast";
    default: return "Other";
  }
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

int main(int argc, char** argv) {
  int port = 8080;
  uint64_t seed = 1;
  double horizon_s = 3600.0; // 1h

  if (argc >= 2) port = std::atoi(argv[1]);
  if (argc >= 3) seed = static_cast<uint64_t>(std::stoull(argv[2]));
  if (argc >= 4) horizon_s = std::stod(argv[3]);

  msim::RulesConfig rcfg{};
  msim::MatchingEngine eng{msim::RuleSet(rcfg)};
  msim::LiveWorld world{std::move(eng)};

  // Agents
  {
    msim::agents::NoiseTraderConfig nt{};
    // keep agent grid consistent with exchange-style rules (defaults are fine too)
    nt.tick_size = 1;
    nt.lot_size  = 1;
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
    res.set_content(html_page(), "text/html; charset=utf-8");
  });

  svr.Get("/api/snapshot", [&](const httplib::Request& req, httplib::Response& res) {
    const std::size_t max_trades =
        req.has_param("max_trades") ? static_cast<std::size_t>(to_ll_safe(req.get_param_value("max_trades"), 50)) : 50;

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

  svr.Get("/api/mid_series", [&](const httplib::Request& req, httplib::Response& res) {
    const long long win_s = req.has_param("window_s") ? to_ll_safe(req.get_param_value("window_s"), 60) : 60;
    const msim::Ts window_ns = static_cast<msim::Ts>(std::max<long long>(1, win_s)) * 1'000'000'000ll;

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

  svr.Post("/api/order", [&](const httplib::Request& req, httplib::Response& res) {
    const std::string side_s = req.get_param_value("side");
    const std::string type_s = req.get_param_value("type");
    const std::string tif_s  = req.get_param_value("tif");

    const long long price_ll = req.has_param("price") ? to_ll_safe(req.get_param_value("price"), 0) : 0;
    const long long qty_ll   = req.has_param("qty") ? to_ll_safe(req.get_param_value("qty"), 0) : 0;

    msim::Order o{};
    o.owner = msim::OwnerId{999}; // manual trader
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
        << "\"status\":\"" << status_to_str(ack.status) << "\","
        << "\"reject_reason\":\"" << reject_to_str(ack.reject_reason) << "\""
        << "}";
    res.set_content(out.str(), "application/json");
  });

  svr.Post("/api/cancel", [&](const httplib::Request& req, httplib::Response& res) {
    const long long id_ll = req.has_param("id") ? to_ll_safe(req.get_param_value("id"), 0) : 0;
    const bool ok = world.cancel_order(static_cast<msim::OrderId>(id_ll));
    std::ostringstream o;
    o << "{\"ok\":" << (ok ? "true" : "false") << "}";
    res.set_content(o.str(), "application/json");
  });

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
