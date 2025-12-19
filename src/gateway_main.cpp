#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "httplib.h"

#include "msim/matching_engine.hpp"
#include "msim/rules.hpp"
#include "msim/live_world.hpp"
#include "msim/order.hpp"
#include "msim/types.hpp"

// ---------------- Small helpers ----------------
static long long get_ll(const httplib::Request& req, const char* key, long long def = 0) {
  if (!req.has_param(key)) return def;
  try { return std::stoll(req.get_param_value(key)); } catch (...) { return def; }
}

static std::string json_bool(bool v) { return v ? "true" : "false"; }

// Compile-time member detection (C++20 requires-expressions)
template <class T>
static bool ack_accepted_(const T& a) {
  if constexpr (requires { a.accepted; }) {
    return static_cast<bool>(a.accepted);
  } else if constexpr (requires { a.ok; }) {
    return static_cast<bool>(a.ok);
  } else if constexpr (requires { a.status; }) {
    // If it uses msim::OrderStatus or similar, treat 0/Accepted as true when possible.
    // We don’t assume exact enum values, but most use Accepted=0.
    return static_cast<int>(a.status) == 0;
  } else {
    // fallback: assume accepted if no field exists
    return true;
  }
}

template <class T>
static int ack_reason_(const T& a) {
  if constexpr (requires { a.reject_reason; }) {
    return static_cast<int>(a.reject_reason);
  } else if constexpr (requires { a.reason; }) {
    return static_cast<int>(a.reason);
  } else if constexpr (requires { a.reject; }) {
    return static_cast<int>(a.reject);
  } else {
    return 0;
  }
}

template <class T>
static long long ack_order_id_(const T& a) {
  if constexpr (requires { a.order_id; }) {
    return static_cast<long long>(a.order_id);
  } else if constexpr (requires { a.id; }) {
    return static_cast<long long>(a.id);
  } else if constexpr (requires { a.oid; }) {
    return static_cast<long long>(a.oid);
  } else {
    return 0;
  }
}

template <class LiveW>
static bool live_cancel_(LiveW& w, msim::OrderId id) {
  if constexpr (requires { w.cancel(id); }) {
    return w.cancel(id);
  } else if constexpr (requires { w.cancel_order(id); }) {
    return w.cancel_order(id);
  } else if constexpr (requires { w.cancel_order_id(id); }) {
    return w.cancel_order_id(id);
  } else {
    return false;
  }
}

template <class LiveW>
static bool live_modify_(LiveW& w, msim::OrderId id, msim::Qty q) {
  if constexpr (requires { w.modify_qty(id, q); }) {
    return w.modify_qty(id, q);
  } else if constexpr (requires { w.modify(id, q); }) {
    return w.modify(id, q);
  } else if constexpr (requires { w.modify_order_qty(id, q); }) {
    return w.modify_order_qty(id, q);
  } else {
    return false;
  }
}

// ---------------- HTML (split to avoid MSVC C2026) ----------------
static std::string html_page() {
  std::string s;
  s.reserve(60'000);

  s += R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>MSIM Live Exchange</title>
  <style>
    body { margin:0; padding:0; font-family: ui-sans-serif, system-ui, -apple-system, Segoe UI, Roboto, Arial; background:#0b0f14; color:#e5e7eb; }
    .wrap { padding: 14px; display: grid; grid-template-columns: 360px 1fr; gap: 14px; }
    .card { background:#0f172a; border:1px solid #1f2a37; border-radius:12px; padding:14px; box-shadow: 0 8px 24px rgba(0,0,0,.25); }
    h2 { margin: 0 0 10px 0; font-size: 14px; letter-spacing: .02em; color: #cbd5e1; }
    .row { display:flex; gap:8px; align-items:center; margin: 6px 0; }
    label { width: 90px; font-size: 12px; color:#94a3b8; }
    input, select, button { background:#0b1220; color:#e5e7eb; border:1px solid #1f2a37; border-radius:8px; padding:6px 8px; font: 12px ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }
    button { cursor:pointer; }
    button:hover { filter: brightness(1.1); }
    .mono { font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; font-size: 12px; }
    .topline { padding: 10px 14px; border-bottom: 1px solid #1f2a37; background:#0b0f14; position: sticky; top: 0; z-index: 5; }
    canvas { width: 100%; height: 260px; background:#0b0f14; border:1px solid #1f2a37; border-radius:10px; }
    table { width:100%; border-collapse:collapse; font-size:12px; }
    th, td { padding:6px 8px; border-bottom:1px solid #1f2a37; text-align: left; }
    th { color:#94a3b8; font-weight: 600; }
    .controls { display:flex; gap:10px; align-items:center; margin: 8px 0 10px 0; color:#94a3b8; font-size: 12px; flex-wrap: wrap; }
    .rightCol { display:grid; grid-template-rows: auto auto 1fr; gap: 14px; }
  </style>
</head>
<body>
  <div class="topline mono" id="topline">loading...</div>

  <div class="wrap">
    <div class="card">
      <h2>Order Entry</h2>

      <div class="row"><label>Order ID</label><input id="oid" value="1"/></div>

      <div class="row">
        <label>Side</label>
        <select id="side">
          <option>Buy</option>
          <option>Sell</option>
        </select>
      </div>

      <div class="row">
        <label>Type</label>
        <select id="type">
          <option>Limit</option>
          <option>Market</option>
        </select>
      </div>

      <div class="row">
        <label>TIF</label>
        <select id="tif">
          <option>GTC</option>
          <option>IOC</option>
          <option>FOK</option>
        </select>
      </div>

      <div class="row"><label>Price</label><input id="price" value="100"/></div>
      <div class="row"><label>Qty</label><input id="qty" value="10"/></div>

      <div class="row" style="margin-top:10px;">
        <button id="sendBtn">Submit</button>
        <button id="cancelBtn">Cancel</button>
      </div>

      <div class="row" style="margin-top:10px;">
        <label>New Qty</label><input id="newQty" value="5"/>
        <button id="modifyBtn">Modify</button>
      </div>

      <div style="margin-top:10px; color:#94a3b8; font-size:12px;">
        Chart is <b>OHLC from trades</b> aggregated client-side (TradingView vibe).
      </div>
    </div>

    <div class="rightCol">
      <div class="card">
        <h2>Price</h2>

        <div class="controls">
          <span>Window:</span>
          <select id="windowSel">
            <option value="10">10s</option>
            <option value="60" selected>1m</option>
            <option value="300">5m</option>
            <option value="900">15m</option>
            <option value="3600">1h</option>
            <option value="86400">1d</option>
          </select>

          <span style="margin-left:10px;">Candles:</span>
          <select id="candleSel">
            <option value="auto" selected>auto</option>
            <option value="1">1s</option>
            <option value="5">5s</option>
            <option value="15">15s</option>
            <option value="60">1m</option>
            <option value="300">5m</option>
            <option value="900">15m</option>
          </select>
        </div>

        <canvas id="chart"></canvas>
      </div>

      <div class="card">
        <h2>Recent Trades</h2>
        <table id="tradesTbl">
          <thead>
            <tr>
              <th>ts</th><th>px</th><th>qty</th><th>maker</th><th>taker</th>
            </tr>
          </thead>
          <tbody></tbody>
        </table>
      </div>
    </div>
  </div>
)HTML";

  s += R"HTML(
<script>
const $ = (id)=>document.getElementById(id);

async function apiGet(url){
  const r = await fetch(url);
  if(!r.ok) throw new Error(url+" -> "+r.status);
  return await r.json();
}
async function apiPostForm(url, params){
  const body = new URLSearchParams(params);
  const r = await fetch(url, {method:"POST", headers:{"Content-Type":"application/x-www-form-urlencoded"}, body});
  if(!r.ok) throw new Error(url+" -> "+r.status);
  return await r.json();
}

// ---------------- Tape (client-side trade history) ----------------
const state = { tape: [], lastTradeId: 0 };

function updateTape(recent){
  if(!Array.isArray(recent)) return;
  for(const t of recent){
    const id = Number(t.id ?? 0);
    if(id > state.lastTradeId){
      state.tape.push({
        id,
        ts: Number(t.ts ?? 0),
        price: Number(t.price ?? 0),
        qty: Number(t.qty ?? 0),
      });
      state.lastTradeId = id;
    }
  }
  const MAX = 200000;
  if(state.tape.length > MAX){
    state.tape = state.tape.slice(state.tape.length - MAX);
  }
}

// ---------------- Candles (OHLCV from trades) ----------------
function autoIntervalSeconds(windowSeconds){
  if(windowSeconds <= 15) return 1;
  if(windowSeconds <= 60) return 2;
  if(windowSeconds <= 300) return 5;
  if(windowSeconds <= 900) return 15;
  if(windowSeconds <= 3600) return 60;
  if(windowSeconds <= 21600) return 300;
  return 900;
}

function buildCandles(trades, nowNs, windowNs, intervalNs){
  const startNs = nowNs - windowNs;
  if(intervalNs <= 0) intervalNs = 1_000_000_000;

  const xs = [];
  for(const t of trades){
    if(t.ts >= startNs && t.ts <= nowNs) xs.push(t);
  }
  if(xs.length === 0) return [];

  xs.sort((a,b)=>a.ts-b.ts);

  const buckets = new Map(); // key: bucketStartNs

  for(const t of xs){
    const k = Math.floor(t.ts / intervalNs) * intervalNs;
    let c = buckets.get(k);
    if(!c){
      c = { t:k, o:t.price, h:t.price, l:t.price, c:t.price, v:t.qty };
      buckets.set(k, c);
    }else{
      c.h = Math.max(c.h, t.price);
      c.l = Math.min(c.l, t.price);
      c.c = t.price;
      c.v += t.qty;
    }
  }

  const keys = Array.from(buckets.keys()).sort((a,b)=>a-b);
  const out = keys.map(k => buckets.get(k));

  // Fill gaps with previous close
  const filled = [];
  let prev = out[0];
  filled.push(prev);
  for(let i=1;i<out.length;i++){
    const cur = out[i];
    let k = prev.t + intervalNs;
    while(k < cur.t){
      const p = prev.c;
      filled.push({ t:k, o:p, h:p, l:p, c:p, v:0 });
      k += intervalNs;
    }
    filled.push(cur);
    prev = cur;
  }
  return filled;
}

// ---------------- Chart rendering ----------------
function drawGrid(ctx, w, h, padL, padR, padT, padB, bg, grid){
  ctx.save();
  ctx.fillStyle = bg;
  ctx.strokeStyle = grid;
  ctx.lineWidth = 1;

  ctx.fillRect(0,0,w,h);

  const gx = 6, gy = 5;
  for(let i=0;i<=gx;i++){
    const x = padL + (w-padL-padR)*i/gx;
    ctx.beginPath(); ctx.moveTo(x, padT); ctx.lineTo(x, h-padB); ctx.stroke();
  }
  for(let j=0;j<=gy;j++){
    const y = padT + (h-padT-padB)*j/gy;
    ctx.beginPath(); ctx.moveTo(padL, y); ctx.lineTo(w-padR, y); ctx.stroke();
  }
  ctx.restore();
}

function drawCandles(candles){
  const canvas = $("chart");
  const ctx = canvas.getContext("2d");

  const w = canvas.width = canvas.clientWidth;
  const h = canvas.height = canvas.clientHeight;

  const bg = "#0b0f14";
  const grid = "#1f2a37";
  const text = "#cbd5e1";
  const up = "#22c55e";
  const down = "#ef4444";
  const wick = "#94a3b8";

  const padL = 52, padR = 10, padT = 10, padB = 22;

  drawGrid(ctx, w, h, padL, padR, padT, padB, bg, grid);

  ctx.font = "11px ui-monospace, SFMono-Regular, Menlo, Consolas, monospace";
  ctx.fillStyle = text;

  if(!candles || candles.length === 0){
    ctx.fillText("waiting for trades...", 60, 30);
    return;
  }

  let lo = Infinity, hi = -Infinity;
  for(const c of candles){
    lo = Math.min(lo, c.l);
    hi = Math.max(hi, c.h);
  }
  if(!isFinite(lo) || !isFinite(hi) || hi <= lo){
    lo = lo - 1; hi = hi + 1;
  }
  const pad = (hi-lo)*0.06;
  lo -= pad; hi += pad;

  const px = (i)=> padL + (w-padL-padR) * (i / Math.max(1, candles.length-1));
  const py = (p)=> padT + (h-padT-padB) * (1 - (p - lo) / (hi - lo));

  // y-axis labels
  for(let j=0;j<=5;j++){
    const p = lo + (hi-lo)*j/5;
    const y = py(p);
    ctx.fillText(p.toFixed(2), 6, y+4);
  }

  const innerW = (w-padL-padR);
  const step = innerW / Math.max(1, candles.length);
  const bodyW = Math.max(2, Math.floor(step * 0.65));

  for(let i=0;i<candles.length;i++){
    const c = candles[i];
    const x = px(i);
    const yO = py(c.o), yC = py(c.c), yH = py(c.h), yL = py(c.l);

    // wick
    ctx.strokeStyle = wick;
    ctx.beginPath();
    ctx.moveTo(x, yH);
    ctx.lineTo(x, yL);
    ctx.stroke();

    const isUp = c.c >= c.o;
    ctx.fillStyle = isUp ? up : down;

    const top = Math.min(yO, yC);
    const bot = Math.max(yO, yC);
    const height = Math.max(1, bot - top);
    ctx.fillRect(Math.floor(x - bodyW/2), Math.floor(top), bodyW, Math.floor(height));
  }

  // x-axis labels: show simulation seconds
  const labelCount = 5;
  for(let i=0;i<=labelCount;i++){
    const idx = Math.floor((candles.length-1) * i/labelCount);
    const t = candles[idx].t;
    const x = px(idx);
    const sec = (t/1e9).toFixed(0);
    ctx.fillText(sec+"s", x-10, h-6);
  }
}

// ---------------- UI update helpers ----------------
function setTopLine(snap){
  const bb = (snap.best_bid==null) ? "-" : snap.best_bid;
  const ba = (snap.best_ask==null) ? "-" : snap.best_ask;
  const mid = (snap.mid==null) ? "-" : snap.mid;
  const last = (snap.last_trade==null) ? "-" : snap.last_trade;
  $("topline").textContent = `ts=${snap.ts}  bid=${bb}  ask=${ba}  mid=${mid}  last=${last}`;
}

function setTradesTable(trades){
  const tbody = document.querySelector("#tradesTbl tbody");
  tbody.innerHTML = "";
  if(!Array.isArray(trades)) return;

  const xs = trades.slice().reverse().slice(0, 30);
  for(const t of xs){
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td class="mono">${t.ts}</td>
      <td class="mono">${t.price}</td>
      <td class="mono">${t.qty}</td>
      <td class="mono">${t.maker_order_id}</td>
      <td class="mono">${t.taker_order_id}</td>
    `;
    tbody.appendChild(tr);
  }
}

// ---------------- Main refresh loop ----------------
async function refresh(){
  try{
    const snap = await apiGet("/api/snapshot");

    setTopLine(snap);
    setTradesTable(snap.recent_trades);
    updateTape(snap.recent_trades);

    const winS = Number($("windowSel").value || 60);
    const winNs = Math.floor(winS * 1e9);

    const cs = $("candleSel").value;
    const intervalS = (cs === "auto") ? autoIntervalSeconds(winS) : Number(cs);
    const intervalNs = Math.floor(intervalS * 1e9);

    const candles = buildCandles(state.tape, Number(snap.ts), winNs, intervalNs);
    drawCandles(candles);

  }catch(e){
    $("topline").textContent = "error: " + e;
  }
}

// ---------------- Form handlers ----------------
$("sendBtn").addEventListener("click", async ()=>{
  try{
    const side = $("side").value;
    const type = $("type").value;
    const tif = $("tif").value;
    const id = $("oid").value;
    const price = $("price").value;
    const qty = $("qty").value;

    const res = await apiPostForm("/api/order", {side, type, tif, id, price, qty});
    alert(JSON.stringify(res));
  }catch(e){
    alert("error: " + e);
  }
});

$("cancelBtn").addEventListener("click", async ()=>{
  try{
    const id = $("oid").value;
    const res = await apiPostForm("/api/cancel", {id});
    alert(JSON.stringify(res));
  }catch(e){
    alert("error: " + e);
  }
});

$("modifyBtn").addEventListener("click", async ()=>{
  try{
    const id = $("oid").value;
    const qty = $("newQty").value;
    const res = await apiPostForm("/api/modify", {id, qty});
    alert(JSON.stringify(res));
  }catch(e){
    alert("error: " + e);
  }
});

$("windowSel").addEventListener("change", ()=>{ refresh(); });
$("candleSel").addEventListener("change", ()=>{ refresh(); });

refresh();
setInterval(refresh, 250);
</script>
</body>
</html>
)HTML";

  return s;
}

int main(int argc, char** argv) {
  // args: [port] [seed]
  int port = 8080;
  uint64_t seed = 1;

  if (argc > 1) port = std::atoi(argv[1]);
  if (argc > 2) seed = static_cast<uint64_t>(std::strtoull(argv[2], nullptr, 10));

  // Make a long horizon so it effectively runs "forever" for the gateway session.
  // (Still safely within int64 nanoseconds.)
  const double horizon_seconds = 3600.0 * 24.0 * 365.0; // ~1 year sim time

  msim::RulesConfig rcfg{};
  msim::MatchingEngine eng{msim::RuleSet(rcfg)};
  msim::LiveWorld world{std::move(eng)};

  // Start background simulation
  world.start(seed, horizon_seconds);

  httplib::Server svr;

  svr.Get("/", [&](const httplib::Request&, httplib::Response& res) {
    res.set_content(html_page(), "text/html");
  });

  svr.Get("/api/snapshot", [&](const httplib::Request&, httplib::Response& res) {
    // keep recent trades bounded (also used by chart tape)
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

    res.set_content(oss.str(), "application/json");
  });

  svr.Post("/api/order", [&](const httplib::Request& req, httplib::Response& res) {
    const auto side_s = req.has_param("side") ? req.get_param_value("side") : "Buy";
    const auto type_s = req.has_param("type") ? req.get_param_value("type") : "Limit";
    const auto tif_s  = req.has_param("tif")  ? req.get_param_value("tif")  : "GTC";

    const auto id_ll    = get_ll(req, "id", 0);
    const auto price_ll = get_ll(req, "price", 100);
    const auto qty_ll   = get_ll(req, "qty", 1);

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

    const auto ack = world.submit_order(o);

    const bool accepted = ack_accepted_(ack);
    const int reason = ack_reason_(ack);
    const long long oid = ack_order_id_(ack);

    std::ostringstream oss;
    oss << "{";
    oss << "\"accepted\":" << json_bool(accepted) << ",";
    oss << "\"reason\":" << reason << ",";
    oss << "\"order_id\":" << oid;
    oss << "}";
    res.set_content(oss.str(), "application/json");
  });

  svr.Post("/api/cancel", [&](const httplib::Request& req, httplib::Response& res) {
    const auto id_ll = get_ll(req, "id", 0);
    const bool ok = live_cancel_(world, static_cast<msim::OrderId>(id_ll));
    res.set_content(std::string("{\"ok\":") + json_bool(ok) + "}", "application/json");
  });

  svr.Post("/api/modify", [&](const httplib::Request& req, httplib::Response& res) {
    const auto id_ll = get_ll(req, "id", 0);
    const auto q_ll  = get_ll(req, "qty", 0);
    const bool ok = live_modify_(world,
                                static_cast<msim::OrderId>(id_ll),
                                static_cast<msim::Qty>(q_ll));
    res.set_content(std::string("{\"ok\":") + json_bool(ok) + "}", "application/json");
  });

  std::cout << "MSIM gateway listening on http://localhost:" << port << "/\n";
  svr.listen("0.0.0.0", port);

  // If LiveWorld has a stop() in your codebase, it will compile-time-detect and call it.
  if constexpr (requires { world.stop(); }) {
    world.stop();
  }

  return 0;
}
