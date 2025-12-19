#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <deque>
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

// -------------------- HTML (embedded) --------------------
static std::string html_page() {
  return R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>MSIM — Local Gateway</title>
  <style>
    :root{
      --bg:#0b0f14;
      --panel:#0f1622;
      --panel2:#0c131e;
      --border:#1f2a3a;
      --text:#e7edf6;
      --muted:#94a3b8;
      --good:#22c55e;
      --bad:#ef4444;
      --accent:#60a5fa;
      --warn:#fbbf24;
      --shadow: 0 10px 30px rgba(0,0,0,.35);
      --radius: 14px;
      --mono: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace;
      --sans: system-ui, -apple-system, Segoe UI, Roboto, sans-serif;
    }
    *{ box-sizing:border-box; }
    body{
      margin:0;
      font-family:var(--sans);
      background: radial-gradient(1200px 600px at 20% -10%, rgba(96,165,250,.15), transparent 55%),
                  radial-gradient(900px 500px at 90% 0%, rgba(34,197,94,.10), transparent 60%),
                  var(--bg);
      color:var(--text);
    }
    .topbar{
      display:flex;
      align-items:center;
      justify-content:space-between;
      padding:14px 18px;
      border-bottom:1px solid var(--border);
      background: rgba(10,14,20,.75);
      backdrop-filter: blur(10px);
      position: sticky;
      top:0;
      z-index:10;
    }
    .brand{
      display:flex;
      gap:10px;
      align-items:baseline;
    }
    .brand h1{
      font-size:18px;
      margin:0;
      letter-spacing:.4px;
    }
    .brand .tag{
      font-size:12px;
      color:var(--muted);
    }
    .status{
      display:flex;
      gap:10px;
      align-items:center;
      font-family:var(--mono);
      font-size:12px;
      color:var(--muted);
    }
    .dot{
      width:10px;height:10px;border-radius:999px;
      background:var(--good);
      box-shadow: 0 0 14px rgba(34,197,94,.35);
    }

    .wrap{
      padding:16px 18px 22px;
      max-width: 1400px;
      margin: 0 auto;
    }

    .grid{
      display:grid;
      grid-template-columns: 1.6fr 1fr;
      gap:14px;
      align-items:start;
    }

    .panel{
      background: linear-gradient(180deg, rgba(255,255,255,.02), transparent 30%), var(--panel);
      border:1px solid var(--border);
      border-radius: var(--radius);
      box-shadow: var(--shadow);
      overflow:hidden;
    }
    .panel .hd{
      padding:12px 14px;
      border-bottom:1px solid var(--border);
      background: rgba(255,255,255,.02);
      display:flex;
      align-items:center;
      justify-content:space-between;
      gap:10px;
    }
    .panel .hd .title{
      font-weight: 700;
      letter-spacing:.2px;
      font-size:13px;
    }
    .panel .bd{
      padding:12px 14px;
    }

    .mono{ font-family: var(--mono); }
    .muted{ color:var(--muted); font-size:12px; }

    .pill{
      display:inline-flex;
      gap:8px;
      align-items:center;
      padding:6px 10px;
      border:1px solid var(--border);
      border-radius:999px;
      background: rgba(255,255,255,.02);
      font-family: var(--mono);
      font-size:12px;
      color: var(--muted);
      white-space:nowrap;
    }

    /* Controls */
    .row{ display:flex; gap:10px; align-items:center; flex-wrap:wrap; }
    select, input, button{
      height:32px;
      border-radius:10px;
      border:1px solid var(--border);
      background: var(--panel2);
      color: var(--text);
      padding:0 10px;
      outline:none;
    }
    input::placeholder{ color: rgba(148,163,184,.65); }
    button{
      cursor:pointer;
      background: linear-gradient(180deg, rgba(96,165,250,.18), rgba(96,165,250,.08));
      border:1px solid rgba(96,165,250,.35);
    }
    button:hover{ filter: brightness(1.06); }
    button.ghost{
      background: rgba(255,255,255,.02);
      border:1px solid var(--border);
    }
    button.danger{
      background: linear-gradient(180deg, rgba(239,68,68,.20), rgba(239,68,68,.10));
      border:1px solid rgba(239,68,68,.35);
    }

    /* Chart */
    .chartWrap{
      border:1px solid var(--border);
      border-radius: 12px;
      overflow:hidden;
      background: #0a111a;
    }
    canvas{ display:block; width:100%; height:320px; }

    /* Right column layout */
    .rightCol{
      display:grid;
      grid-template-rows: auto auto;
      gap:14px;
    }
    .rightSplit{
      display:grid;
      grid-template-columns: 1fr 1.2fr;
      gap:14px;
    }

    /* Tables */
    table{
      width:100%;
      border-collapse:collapse;
      font-family: var(--mono);
      font-size:12px;
    }
    th, td{
      padding:6px 6px;
      border-bottom:1px solid rgba(31,42,58,.55);
      text-align:right;
    }
    th{
      color:var(--muted);
      font-weight:600;
      text-transform: uppercase;
      letter-spacing:.6px;
      font-size:10px;
      background: rgba(255,255,255,.02);
      position: sticky;
      top:0;
      z-index:2;
    }
    td.l{ text-align:left; }
    .tblBox{
      border:1px solid var(--border);
      border-radius: 12px;
      overflow:auto;
      background: rgba(10,17,26,.55);
      max-height: 360px;
    }

    /* Order book coloring */
    .askRow td{ color: rgba(239,68,68,.95); }
    .bidRow td{ color: rgba(34,197,94,.95); }

    /* Trade tape */
    .tapeMeta{
      display:flex;
      justify-content:space-between;
      align-items:center;
      gap:10px;
      margin-bottom:8px;
    }

    /* Toast */
    .toast{
      position: fixed;
      right: 16px;
      bottom: 16px;
      width: 340px;
      display:flex;
      flex-direction:column;
      gap:10px;
      z-index:50;
      pointer-events:none;
    }
    .toast .msg{
      pointer-events:none;
      padding:10px 12px;
      border-radius: 12px;
      border:1px solid var(--border);
      background: rgba(15,22,34,.92);
      box-shadow: var(--shadow);
      font-family: var(--mono);
      font-size:12px;
      color: var(--text);
    }
    .toast .msg.good{ border-color: rgba(34,197,94,.35); }
    .toast .msg.bad{ border-color: rgba(239,68,68,.35); }
    .toast .msg.warn{ border-color: rgba(251,191,36,.35); }

    @media (max-width: 1100px){
      .grid{ grid-template-columns: 1fr; }
      .rightSplit{ grid-template-columns: 1fr; }
      canvas{ height: 280px; }
    }
  </style>
</head>
<body>
  <div class="topbar">
    <div class="brand">
      <h1>MSIM</h1>
      <div class="tag">Local Gateway • live matching + agents + manual orders</div>
    </div>
    <div class="status">
      <span class="dot" id="connDot"></span>
      <span id="connTxt">connected</span>
      <span class="pill" id="topline">ts=… bid=… ask=… mid=… last=…</span>
    </div>
  </div>

  <div class="wrap">
    <div class="grid">
      <!-- LEFT: Market -->
      <div class="panel">
        <div class="hd">
          <div class="title">Market</div>
          <div class="row">
            <span class="muted">Window</span>
            <select id="windowSel">
              <option value="60" selected>1m</option>
              <option value="300">5m</option>
              <option value="900">15m</option>
              <option value="3600">1h</option>
              <option value="14400">4h</option>
              <option value="86400">1d</option>
              <option value="604800">1w</option>
              <option value="2592000">1mo</option>
              <option value="7776000">3mo</option>
              <option value="15552000">6mo</option>
              <option value="31536000">1y</option>
            </select>
            <button class="ghost" id="btnReset">Reset view</button>
          </div>
        </div>
        <div class="bd">
          <div class="chartWrap">
            <canvas id="chart" width="1100" height="360"></canvas>
          </div>
          <div class="muted" style="margin-top:10px;">
            Tip: chart shows <b>mid</b> (fallback to last trade). Order book view requires an /api/book endpoint.
          </div>
        </div>
      </div>

      <!-- RIGHT: Order entry + book + tape -->
      <div class="rightCol">
        <div class="panel">
          <div class="hd">
            <div class="title">Order Entry</div>
            <div class="muted mono">owner=999</div>
          </div>
          <div class="bd">
            <div class="row" style="margin-bottom:10px;">
              <select id="side">
                <option value="buy">buy</option>
                <option value="sell">sell</option>
              </select>
              <select id="type">
                <option value="limit">limit</option>
                <option value="market">market</option>
              </select>
              <input id="price" type="number" placeholder="price (limit)" style="width:120px;" />
              <input id="qty" type="number" placeholder="qty" value="10" style="width:100px;" />
              <select id="tif">
                <option value="gtc" selected>GTC</option>
                <option value="ioc">IOC</option>
                <option value="fok">FOK</option>
              </select>
              <button id="sendBtn">Send</button>
            </div>

            <div class="row">
              <span class="muted">Cancel / Modify</span>
              <input id="oid" type="number" placeholder="order_id" style="width:140px;" />
              <input id="newQty" type="number" placeholder="new_qty" style="width:120px;" />
              <button class="danger" id="cancelBtn">Cancel</button>
              <button class="ghost" id="modifyBtn">ModifyQty</button>
            </div>
          </div>
        </div>

        <div class="rightSplit">
          <div class="panel">
            <div class="hd">
              <div class="title">Order Book (Top 5)</div>
              <div class="muted mono" id="obHint">(waiting for /api/book)</div>
            </div>
            <div class="bd">
              <div class="tblBox" style="max-height:360px;">
                <table id="bookTbl">
                  <thead>
                    <tr>
                      <th class="l">side</th>
                      <th>price</th>
                      <th>qty</th>
                    </tr>
                  </thead>
                  <tbody></tbody>
                </table>
              </div>
              <div class="muted" style="margin-top:10px;">
                Goal: aggregate resting liquidity by price level (e.g. 1200 @ 98).
              </div>
            </div>
          </div>

          <div class="panel">
            <div class="hd">
              <div class="title">Trade Tape</div>
              <div class="row">
                <button class="ghost" id="pauseBtn">Pause</button>
                <button class="ghost" id="clearBtn">Clear</button>
              </div>
            </div>
            <div class="bd">
              <div class="tapeMeta">
                <div class="muted mono" id="tapeMeta">0 trades shown</div>
                <div class="muted mono" id="tapeSpeed">refresh 500ms</div>
              </div>
              <div class="tblBox">
                <table id="tradesTbl">
                  <thead>
                    <tr>
                      <th class="l">ts</th>
                      <th>px</th>
                      <th>qty</th>
                      <th>maker</th>
                      <th>taker</th>
                    </tr>
                  </thead>
                  <tbody></tbody>
                </table>
              </div>
              <div class="muted" style="margin-top:10px;">
                Only appends new trades (no full redraw).
              </div>
            </div>
          </div>
        </div>

      </div>
    </div>
  </div>

  <div class="toast" id="toast"></div>

<script>
  const connDot = document.getElementById('connDot');
  const connTxt = document.getElementById('connTxt');
  const topline = document.getElementById('topline');

  const canvas = document.getElementById('chart');
  const ctx = canvas.getContext('2d');
  const windowSel = document.getElementById('windowSel');

  const tradesBody = document.querySelector('#tradesTbl tbody');
  const tapeMeta = document.getElementById('tapeMeta');

  const bookBody = document.querySelector('#bookTbl tbody');
  const obHint = document.getElementById('obHint');

  const toast = document.getElementById('toast');

  let paused = false;
  let lastTradeId = 0;
  let tradeRows = []; // {id, html}
  const MAX_TAPE_ROWS = 40;

  function showToast(text, kind="good") {
    const d = document.createElement('div');
    d.className = `msg ${kind}`;
    d.textContent = text;
    toast.appendChild(d);
    setTimeout(()=>{ d.remove(); }, 2200);
  }

  async function apiGet(url) {
    const r = await fetch(url);
    if (!r.ok) throw new Error(`GET ${url} -> ${r.status}`);
    return await r.json();
  }

  async function apiPost(url, obj) {
    const body = new URLSearchParams(obj);
    const r = await fetch(url, {
      method: 'POST',
      headers: {'Content-Type':'application/x-www-form-urlencoded'},
      body
    });
    if (!r.ok) throw new Error(`POST ${url} -> ${r.status}`);
    return await r.json();
  }

  function setConn(ok) {
    connDot.style.background = ok ? 'var(--good)' : 'var(--bad)';
    connTxt.textContent = ok ? 'connected' : 'disconnected';
    connTxt.style.color = ok ? 'var(--muted)' : 'var(--bad)';
  }

  // ---- Chart rendering (better-looking canvas) ----
  function drawChart(points) {
    const W = canvas.width, H = canvas.height;
    ctx.clearRect(0,0,W,H);

    // background
    ctx.fillStyle = '#0a111a';
    ctx.fillRect(0,0,W,H);

    const usable = points.filter(p => p.mid !== null);
    if (!usable.length) return;

    let min = Infinity, max = -Infinity;
    for (const p of usable) { min = Math.min(min, p.mid); max = Math.max(max, p.mid); }
    if (!isFinite(min) || !isFinite(max)) return;
    if (min === max) { min -= 1; max += 1; }

    const padL = 52, padR = 12, padT = 16, padB = 26;
    const plotW = W - padL - padR;
    const plotH = H - padT - padB;

    const t0 = usable[0].ts;
    const t1 = usable[usable.length - 1].ts;

    const xOf = (ts) => {
      if (t1 === t0) return padL;
      return padL + plotW * (ts - t0) / (t1 - t0);
    };
    const yOf = (v) => padT + plotH * (1 - (v - min) / (max - min));

    // grid
    ctx.strokeStyle = 'rgba(148,163,184,.10)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    const gridY = 5;
    for (let i=0;i<=gridY;i++){
      const y = padT + (plotH * i / gridY);
      ctx.moveTo(padL, y); ctx.lineTo(W - padR, y);
    }
    const gridX = 6;
    for (let i=0;i<=gridX;i++){
      const x = padL + (plotW * i / gridX);
      ctx.moveTo(x, padT); ctx.lineTo(x, H - padB);
    }
    ctx.stroke();

    // line
    ctx.strokeStyle = 'rgba(96,165,250,.95)';
    ctx.lineWidth = 2;
    ctx.beginPath();
    let started=false;
    for (const p of usable) {
      const x = xOf(p.ts);
      const y = yOf(p.mid);
      if (!started){ ctx.moveTo(x,y); started=true; }
      else ctx.lineTo(x,y);
    }
    ctx.stroke();

    // area fill
    ctx.lineTo(xOf(usable[usable.length-1].ts), H - padB);
    ctx.lineTo(xOf(usable[0].ts), H - padB);
    ctx.closePath();
    const grad = ctx.createLinearGradient(0, padT, 0, H - padB);
    grad.addColorStop(0, 'rgba(96,165,250,.18)');
    grad.addColorStop(1, 'rgba(96,165,250,0)');
    ctx.fillStyle = grad;
    ctx.fill();

    // axes labels (prices)
    ctx.fillStyle = 'rgba(148,163,184,.9)';
    ctx.font = '12px ' + getComputedStyle(document.body).getPropertyValue('--mono');
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    for (let i=0;i<=gridY;i++){
      const v = max - (max-min)*i/gridY;
      const y = padT + (plotH*i/gridY);
      ctx.fillText(v.toFixed(2), padL - 8, y);
    }

    // last price marker
    const last = usable[usable.length-1];
    const lx = xOf(last.ts);
    const ly = yOf(last.mid);
    ctx.fillStyle = 'rgba(96,165,250,1)';
    ctx.beginPath(); ctx.arc(lx, ly, 3.5, 0, Math.PI*2); ctx.fill();

    // last price label
    const label = `mid ${last.mid}`;
    ctx.font = '12px ' + getComputedStyle(document.body).getPropertyValue('--mono');
    ctx.textAlign = 'left';
    ctx.textBaseline = 'middle';
    const boxW = ctx.measureText(label).width + 12;
    const boxH = 20;
    const bx = Math.min(W - padR - boxW, lx + 10);
    const by = Math.max(padT + boxH/2, Math.min(H - padB - boxH/2, ly));
    ctx.fillStyle = 'rgba(15,22,34,.90)';
    ctx.strokeStyle = 'rgba(96,165,250,.35)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.roundRect(bx, by - boxH/2, boxW, boxH, 8);
    ctx.fill(); ctx.stroke();
    ctx.fillStyle = 'rgba(231,237,246,.95)';
    ctx.fillText(label, bx + 6, by);
  }

  // polyfill for ctx.roundRect in older browsers
  if (!CanvasRenderingContext2D.prototype.roundRect) {
    CanvasRenderingContext2D.prototype.roundRect = function(x,y,w,h,r){
      const rr = Math.min(r, w/2, h/2);
      this.beginPath();
      this.moveTo(x+rr, y);
      this.arcTo(x+w, y, x+w, y+h, rr);
      this.arcTo(x+w, y+h, x, y+h, rr);
      this.arcTo(x, y+h, x, y, rr);
      this.arcTo(x, y, x+w, y, rr);
      this.closePath();
      return this;
    }
  }

  // ---- Trade tape (incremental append) ----
  function renderTape() {
    tradesBody.innerHTML = '';
    for (const r of tradeRows.slice().reverse()) {
      const tr = document.createElement('tr');
      tr.innerHTML = r.html;
      tradesBody.appendChild(tr);
    }
    tapeMeta.textContent = `${tradeRows.length} trades shown`;
  }

  function appendTrades(trades) {
    for (const t of trades) {
      if (t.id <= lastTradeId) continue;
      lastTradeId = Math.max(lastTradeId, t.id);
      const html = `<td class="l">${t.ts}</td><td>${t.price}</td><td>${t.qty}</td><td>${t.maker_order_id}</td><td>${t.taker_order_id}</td>`;
      tradeRows.push({id: t.id, html});
      if (tradeRows.length > MAX_TAPE_ROWS) tradeRows.shift();
    }
    renderTape();
  }

  // ---- Order book UI (expects /api/book, else shows hint) ----
  function renderBook(book) {
    bookBody.innerHTML = '';
    if (!book || !book.bids || !book.asks) {
      obHint.textContent = '(waiting for /api/book)';
      return;
    }
    obHint.textContent = 'live';

    // show asks top->down (best ask first), then bids (best bid first)
    for (const a of book.asks) {
      const tr = document.createElement('tr');
      tr.className = 'askRow';
      tr.innerHTML = `<td class="l">ask</td><td>${a.price}</td><td>${a.qty}</td>`;
      bookBody.appendChild(tr);
    }
    // separator
    const sep = document.createElement('tr');
    sep.innerHTML = `<td class="l muted" colspan="3" style="text-align:center;padding:10px 6px;">— spread —</td>`;
    bookBody.appendChild(sep);

    for (const b of book.bids) {
      const tr = document.createElement('tr');
      tr.className = 'bidRow';
      tr.innerHTML = `<td class="l">bid</td><td>${b.price}</td><td>${b.qty}</td>`;
      bookBody.appendChild(tr);
    }
  }

  // ---- Refresh loop ----
  async function refresh() {
    try {
      const snap = await apiGet('/api/snapshot?max_trades=80');
      setConn(true);

      const bb = snap.best_bid === null ? '-' : snap.best_bid;
      const ba = snap.best_ask === null ? '-' : snap.best_ask;
      const mid = snap.mid === null ? '-' : snap.mid;
      const lt = snap.last_trade === null ? '-' : snap.last_trade;
      topline.textContent = `ts=${snap.ts}  bid=${bb}  ask=${ba}  mid=${mid}  last=${lt}`;

      if (!paused) {
        appendTrades(snap.recent_trades || []);
      }

      const winS = parseInt(windowSel.value, 10);
      const series = await apiGet(`/api/mid_series?window_s=${winS}`);
      drawChart((series.points || []).map(p => ({ts:p.ts, mid:p.mid})));

      // Try order book endpoint (optional). If 404, just ignore.
      try {
        const book = await apiGet('/api/book?levels=5');
        renderBook(book);
      } catch (e) {
        renderBook(null);
      }

    } catch (e) {
      setConn(false);
    }
  }

  document.getElementById('btnReset').onclick = () => {
    lastTradeId = 0;
    tradeRows = [];
    renderTape();
    showToast('view reset', 'warn');
  };

  document.getElementById('pauseBtn').onclick = () => {
    paused = !paused;
    document.getElementById('pauseBtn').textContent = paused ? 'Resume' : 'Pause';
    showToast(paused ? 'tape paused' : 'tape resumed', 'warn');
  };

  document.getElementById('clearBtn').onclick = () => {
    tradeRows = [];
    renderTape();
    showToast('tape cleared', 'warn');
  };

  document.getElementById('sendBtn').onclick = async () => {
    try {
      const side = document.getElementById('side').value;
      const type = document.getElementById('type').value;
      const price = document.getElementById('price').value;
      const qty = document.getElementById('qty').value;
      const tif = document.getElementById('tif').value;

      if (type === 'limit' && (!price || Number(price) <= 0)) {
        showToast('limit needs price > 0', 'bad');
        return;
      }
      if (!qty || Number(qty) <= 0) {
        showToast('qty must be > 0', 'bad');
        return;
      }

      const res = await apiPost('/api/order', { side, type, price, qty, tif });
      const ok = (res.status || '').toLowerCase().includes('accept');
      showToast(`order id=${res.id}  ${res.status}  ${res.reject_reason}`, ok ? 'good' : 'bad');
    } catch (e) {
      showToast('order submit failed', 'bad');
    }
  };

  document.getElementById('cancelBtn').onclick = async () => {
    try {
      const id = document.getElementById('oid').value;
      const res = await apiPost('/api/cancel', { id });
      showToast(`cancel ok=${res.ok}`, res.ok ? 'good' : 'bad');
    } catch (e) {
      showToast('cancel failed', 'bad');
    }
  };

  document.getElementById('modifyBtn').onclick = async () => {
    try {
      const id = document.getElementById('oid').value;
      const new_qty = document.getElementById('newQty').value;
      const res = await apiPost('/api/modify', { id, new_qty });
      showToast(`modify ok=${res.ok}`, res.ok ? 'good' : 'bad');
    } catch (e) {
      showToast('modify failed', 'bad');
    }
  };

  // slightly slower refresh to reduce UI churn
  setInterval(refresh, 500);
  refresh();
</script>
</body>
</html>
)HTML";
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
    res.set_content(html_page(), "text/html; charset=utf-8");
  });

  // Snapshot: top + last trade + recent trades
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

  // Mid series windowed
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

  // (Optional) Order book endpoint:
  // We intentionally do NOT implement it here yet, because we need to read depth from the live book safely.
  // Once LiveWorld exposes depth, we’ll add:
  //   GET /api/book?levels=5  -> { "bids":[{"price":..,"qty":..}], "asks":[...] }

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
