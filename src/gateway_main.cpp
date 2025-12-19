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

// ---------------- Compile-time detection helpers ----------------
template <class T>
static bool ack_accepted_(const T& a) {
  if constexpr (requires { a.accepted; }) {
    return static_cast<bool>(a.accepted);
  } else if constexpr (requires { a.ok; }) {
    return static_cast<bool>(a.ok);
  } else if constexpr (requires { a.status; }) {
    // Most codebases: Accepted==0
    return static_cast<int>(a.status) == 0;
  } else {
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

// Depth level accessors (supports several layouts)
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

// ---------------- HTML (split to avoid MSVC C2026) ----------------
static std::string html_page() {
  std::string s;
  s.reserve(90'000);

  s += R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>MSIM Live Exchange</title>
  <style>
    :root{
      --bg:#0b0f14;
      --panel:#0f172a;
      --panel2:#0b1220;
      --border:#1f2a37;
      --text:#e5e7eb;
      --muted:#94a3b8;
      --muted2:#cbd5e1;
      --up:#22c55e;
      --down:#ef4444;
      --wick:#94a3b8;
      --grid:#1f2a37;
    }
    body { margin:0; padding:0; font-family: ui-sans-serif, system-ui, -apple-system, Segoe UI, Roboto, Arial; background:var(--bg); color:var(--text); }
    .mono { font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; font-size: 12px; }
    .topline { padding: 10px 14px; border-bottom: 1px solid var(--border); background:var(--bg); position: sticky; top: 0; z-index: 5; }

    .wrap { padding: 14px; display: grid; grid-template-columns: 360px 1fr; gap: 14px; }

    .card {
      background:var(--panel);
      border:1px solid var(--border);
      border-radius:12px;
      padding:14px;
      box-shadow: 0 8px 24px rgba(0,0,0,.25);
    }

    h2 { margin: 0 0 10px 0; font-size: 13px; letter-spacing: .02em; color: var(--muted2); text-transform: uppercase; }
    .row { display:flex; gap:8px; align-items:center; margin: 6px 0; }
    label { width: 90px; font-size: 12px; color:var(--muted); }

    input, select, button {
      background:var(--panel2);
      color:var(--text);
      border:1px solid var(--border);
      border-radius:8px;
      padding:6px 8px;
      font: 12px ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
      outline: none;
    }
    input:focus, select:focus { border-color:#334155; }
    button { cursor:pointer; }
    button:hover { filter: brightness(1.12); }

    .rightCol { display:grid; grid-template-rows: auto auto auto; gap: 14px; }
    canvas { width: 100%; height: 260px; background:var(--bg); border:1px solid var(--border); border-radius:10px; }

    table { width:100%; border-collapse:collapse; font-size:12px; }
    th, td { padding:6px 8px; border-bottom:1px solid var(--border); text-align: left; }
    th { color:var(--muted); font-weight: 600; }

    .controls { display:flex; gap:10px; align-items:center; margin: 8px 0 10px 0; color:var(--muted); font-size: 12px; flex-wrap: wrap; }

    /* Ladder styling */
    .ladder {
      width:100%;
      border:1px solid var(--border);
      border-radius:10px;
      overflow:hidden;
      background:var(--bg);
    }
    .ladderHead, .ladderRow {
      display:grid;
      grid-template-columns: 1fr 90px 90px 1fr; /* bidQty, bidPx, askPx, askQty */
      gap: 0;
      align-items: center;
    }
    .ladderHead {
      background: #0a0e13;
      border-bottom: 1px solid var(--border);
      color: var(--muted);
      font-size: 11px;
      padding: 6px 8px;
      text-transform: uppercase;
      letter-spacing: .04em;
    }
    .ladderRow {
      padding: 4px 8px;
      border-bottom: 1px solid rgba(31,42,55,0.7);
      font-size: 12px;
    }
    .ladderRow:last-child { border-bottom: none; }
    .cell { position: relative; height: 18px; display:flex; align-items:center; }
    .cell.right { justify-content: flex-end; }
    .bar {
      position:absolute; top:2px; bottom:2px; border-radius:6px;
      opacity: 0.22;
    }
    .bar.bid { right:0; background: var(--up); }
    .bar.ask { left:0; background: var(--down); }
    .pxBid { color: #86efac; } /* green-ish */
    .pxAsk { color: #fca5a5; } /* red-ish */
    .muted { color: var(--muted); }

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

      <div style="margin-top:10px; color:var(--muted); font-size:12px;">
        Chart is <b>OHLC</b> aggregated client-side from trades.
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

          <span style="margin-left:10px;">Depth:</span>
          <select id="depthSel">
            <option value="5" selected>5</option>
            <option value="10">10</option>
            <option value="20">20</option>
          </select>
        </div>

        <canvas id="chart"></canvas>
      </div>

      <div class="card">
        <h2>Order Book (L2 Ladder)</h2>
        <div class="ladder">
          <div class="ladderHead">
            <div class="cell">Bid Qty</div>
            <div class="cell right">Bid Px</div>
            <div class="cell">Ask Px</div>
            <div class="cell right">Ask Qty</div>
          </div>
          <div id="ladderBody"></div>
        </div>
        <div style="margin-top:8px;" class="mono muted" id="ladderMeta">levels=5</div>
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
const state = { tape: [], lastTradeId: 0, lastDepthTs: 0 };

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

  const buckets = new Map();
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

  // fill gaps with previous close
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
function drawGrid(ctx, w, h, padL, padR, padT, padB){
  ctx.save();
  ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--bg').trim();
  ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue('--grid').trim();
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

  const css = getComputedStyle(document.documentElement);
  const text = css.getPropertyValue('--muted2').trim();
  const up = css.getPropertyValue('--up').trim();
  const down = css.getPropertyValue('--down').trim();
  const wick = css.getPropertyValue('--wick').trim();

  const padL = 52, padR = 10, padT = 10, padB = 22;
  drawGrid(ctx, w, h, padL, padR, padT, padB);

  ctx.font = "11px ui-monospace, SFMono-Regular, Menlo, Consolas, monospace";
  ctx.fillStyle = text;

  if(!candles || candles.length === 0){
    ctx.fillText("waiting for trades...", 60, 30);
    return;
  }

  let lo = Infinity, hi = -Infinity;
  for(const c of candles){ lo = Math.min(lo, c.l); hi = Math.max(hi, c.h); }
  if(!isFinite(lo) || !isFinite(hi) || hi <= lo){ lo -= 1; hi += 1; }
  const pad = (hi-lo)*0.06;
  lo -= pad; hi += pad;

  const px = (i)=> padL + (w-padL-padR) * (i / Math.max(1, candles.length-1));
  const py = (p)=> padT + (h-padT-padB) * (1 - (p - lo) / (hi - lo));

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

    ctx.strokeStyle = wick;
    ctx.beginPath(); ctx.moveTo(x, yH); ctx.lineTo(x, yL); ctx.stroke();

    const isUp = c.c >= c.o;
    ctx.fillStyle = isUp ? up : down;

    const top = Math.min(yO, yC);
    const bot = Math.max(yO, yC);
    const height = Math.max(1, bot - top);
    ctx.fillRect(Math.floor(x - bodyW/2), Math.floor(top), bodyW, Math.floor(height));
  }

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

  const xs = trades.slice().reverse().slice(0, 22); // slightly calmer
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

function renderLadder(depth){
  const body = $("ladderBody");
  body.innerHTML = "";

  const bids = Array.isArray(depth.bids) ? depth.bids : [];
  const asks = Array.isArray(depth.asks) ? depth.asks : [];

  const n = Math.max(bids.length, asks.length);
  if(n === 0){
    body.innerHTML = `<div style="padding:10px;" class="mono muted">no depth yet</div>`;
    return;
  }

  let maxQty = 1;
  for(const b of bids) maxQty = Math.max(maxQty, Number(b.qty||0));
  for(const a of asks) maxQty = Math.max(maxQty, Number(a.qty||0));

  for(let i=0;i<n;i++){
    const b = bids[i];
    const a = asks[i];

    const bidPx = (b && b.price!=null) ? b.price : "";
    const bidQty = (b && b.qty!=null) ? b.qty : "";
    const askPx = (a && a.price!=null) ? a.price : "";
    const askQty = (a && a.qty!=null) ? a.qty : "";

    const bidW = (b && b.qty!=null) ? Math.max(0, Math.min(1, Number(b.qty)/maxQty)) : 0;
    const askW = (a && a.qty!=null) ? Math.max(0, Math.min(1, Number(a.qty)/maxQty)) : 0;

    const row = document.createElement("div");
    row.className = "ladderRow";

    row.innerHTML = `
      <div class="cell right mono">
        <div class="bar bid" style="width:${Math.floor(bidW*100)}%;"></div>
        ${bidQty}
      </div>
      <div class="cell right mono pxBid">${bidPx}</div>
      <div class="cell mono pxAsk">${askPx}</div>
      <div class="cell mono">
        <div class="bar ask" style="width:${Math.floor(askW*100)}%;"></div>
        <span style="margin-left:auto;">${askQty}</span>
      </div>
    `;
    body.appendChild(row);
  }
}

// ---------------- Main refresh loop ----------------
async function refresh(){
  try{
    const snap = await apiGet("/api/snapshot");
    setTopLine(snap);
    setTradesTable(snap.recent_trades);
    updateTape(snap.recent_trades);

    // candles
    const winS = Number($("windowSel").value || 60);
    const winNs = Math.floor(winS * 1e9);

    const cs = $("candleSel").value;
    const intervalS = (cs === "auto") ? autoIntervalSeconds(winS) : Number(cs);
    const intervalNs = Math.floor(intervalS * 1e9);

    const candles = buildCandles(state.tape, Number(snap.ts), winNs, intervalNs);
    drawCandles(candles);

    // depth (L2 ladder)
    const levels = Number($("depthSel").value || 5);
    const depth = await apiGet("/api/depth?levels=" + encodeURIComponent(levels));
    renderLadder(depth);
    $("ladderMeta").textContent = `levels=${levels}  max_qty=${depth.max_qty ?? "-"}`;

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
$("depthSel").addEventListener("change", ()=>{ refresh(); });

refresh();
setInterval(refresh, 350);
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

  // Make a long horizon so it runs "forever" for the gateway session.
  const double horizon_seconds = 3600.0 * 24.0 * 365.0; // ~1 year sim time

  msim::RulesConfig rcfg{};
  msim::MatchingEngine eng{msim::RuleSet(rcfg)};
  msim::LiveWorld world{std::move(eng)};

  world.start(seed, horizon_seconds);

  httplib::Server svr;

  svr.Get("/", [&](const httplib::Request&, httplib::Response& res) {
    res.set_content(html_page(), "text/html");
  });

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

    res.set_content(oss.str(), "application/json");
  });

  // New: Depth endpoint for L2 ladder
  svr.Get("/api/depth", [&](const httplib::Request& req, httplib::Response& res) {
    std::size_t levels = 5;
    if (req.has_param("levels")) {
      try {
        const auto v = std::stoull(req.get_param_value("levels"));
        if (v > 0 && v <= 200) levels = static_cast<std::size_t>(v);
      } catch (...) { /* ignore */ }
    }

    // LiveWorld API
    auto d = world.book_depth(levels);

    // We assume d.bids / d.asks are vectors of some DepthLevel type
    // Serialize as {bids:[{price,qty,orders}], asks:[...], max_qty:...}
    long long max_qty = 1;
    for (const auto& x : d.bids) max_qty = std::max(max_qty, level_qty_(x));
    for (const auto& x : d.asks) max_qty = std::max(max_qty, level_qty_(x));

    std::ostringstream oss;
    oss << "{";
    oss << "\"max_qty\":" << max_qty << ",";
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

  if constexpr (requires { world.stop(); }) {
    world.stop();
  }
  return 0;
}
