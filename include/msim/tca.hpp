#pragma once
// ============================================================
// include/msim/tca.hpp
//
// Transaction Cost Analysis (TCA) data structures.
//
// These are populated live during World::run() — not reconstructed
// afterwards.  Because they are computed at the point of execution,
// they capture data that is impossible to recover from raw trades
// alone, such as the mid-price at the moment an order was submitted.
//
// Three levels of output:
//
//   FillRecord    — one entry per individual fill, for both
//                   maker (limit) and taker (market/IOC) sides.
//
//   StepSnapshot  — one entry per agent per simulation step,
//                   giving the full mark-to-market PnL series.
//
//   AgentTCA      — per-agent summary computed at end of run:
//                   fill rates, average slippage, final PnL.
//
// Design note: ArrivalInfo is stored internally by World to
// record the market state when each order was submitted.  It is
// defined here so world.hpp can use it as a private map value.
// ============================================================

#include <cmath>
#include <cstdint>
#include <vector>

#include "msim/types.hpp"   // Price, Qty, Ts, OwnerId, OrderId, Side

namespace msim {

// ─── ArrivalInfo ─────────────────────────────────────────────────────────────
// Internal: stored per OrderId to enable slippage computation.
struct ArrivalInfo {
  Price arrival_mid{0};   // mid-price when the order was submitted
  bool  is_limit{false};  // true = limit (maker-eligible), false = market
};

// ─── FillRecord ──────────────────────────────────────────────────────────────
// One record per partial or full fill, from the perspective of one agent.
// Both sides of each trade get a record (maker and taker).
struct FillRecord {
  Ts      ts{};
  OwnerId owner{};
  OrderId order_id{};
  Side    side{};
  Qty     fill_qty{};
  Price   fill_price{};
  Price   arrival_mid{};   // mid when order was submitted (0 if unknown)
  bool    is_maker{};      // true = filled as passive limit order

  // Signed slippage in ticks from the agent's perspective.
  // Positive = paid more than mid (bad for taker buys).
  // Negative = received better than mid (good for maker fills).
  // For buy:  fill_price - arrival_mid
  // For sell: arrival_mid - fill_price
  double slippage_ticks() const noexcept {
    if (arrival_mid == 0) return 0.0;
    const double raw = static_cast<double>(fill_price)
                     - static_cast<double>(arrival_mid);
    return (side == Side::Buy) ? raw : -raw;
  }
};

// ─── StepSnapshot ────────────────────────────────────────────────────────────
// Mark-to-market state for one agent at the end of one simulation step.
// Collecting these for every agent every step gives the full PnL series.
struct StepSnapshot {
  Ts      ts{};
  OwnerId owner{};
  int64_t position{};     // signed inventory (+ = long)
  int64_t cash_ticks{};   // realised cash in ticks
  Price   mid{};          // mid-price at this step (0 if book empty)

  // Mark-to-market PnL = cash_ticks + position * mid
  // (unrealised gain/loss on open position at current mid)
  double mtm_pnl() const noexcept {
    return static_cast<double>(cash_ticks)
         + static_cast<double>(position) * static_cast<double>(mid);
  }
};

// ─── AgentTCA ────────────────────────────────────────────────────────────────
// Per-agent summary statistics computed at the end of World::run().
struct AgentTCA {
  OwnerId owner{};

  // ── Submission counts ──────────────────────────────────────────────────
  int64_t n_orders_submitted{};   // total orders sent to the engine
  int64_t n_limit_submitted{};    // of which: GTC limit orders
  int64_t n_market_submitted{};   // of which: IOC/FOK market orders
  int64_t n_cancels_sent{};       // cancel actions sent

  // ── Fill counts ────────────────────────────────────────────────────────
  int64_t n_fills_maker{};        // times filled as passive limit side
  int64_t n_fills_taker{};        // times filled as aggressive market side
  int64_t total_qty_maker{};      // lots filled passively
  int64_t total_qty_taker{};      // lots filled aggressively
  int64_t total_qty_traded{};     // total = maker + taker

  // ── Rates ──────────────────────────────────────────────────────────────
  // Fill rate: fraction of submitted limit orders that got at least one fill.
  // (0 if no limit orders submitted)
  double limit_fill_rate{};

  // ── Slippage (taker fills only) ────────────────────────────────────────
  // Average slippage vs arrival mid, across all taker fills.
  // Positive = paid above mid (market impact).
  // Negative = filled inside mid (rare, crossed books only).
  double avg_slippage_ticks{};
  double total_slippage_ticks{};

  // ── Turnover ───────────────────────────────────────────────────────────
  // Total notional value traded in ticks (sum of price * qty).
  int64_t turnover_notional_ticks{};

  // ── Final state ────────────────────────────────────────────────────────
  int64_t final_position{};
  int64_t final_cash_ticks{};
  double  final_mtm_pnl{};      // cash_ticks + position * final_mid
};

// ─── Free function: compute AgentTCA from fills + snapshots ──────────────────
// Called by World::run() at the end of the simulation.
inline AgentTCA compute_agent_tca(
    OwnerId owner,
    int64_t n_limit_sub,
    int64_t n_market_sub,
    int64_t n_cancels,
    const std::vector<FillRecord>& fills,
    int64_t final_position,
    int64_t final_cash_ticks,
    Price   final_mid)
{
  AgentTCA t;
  t.owner              = owner;
  t.n_limit_submitted  = n_limit_sub;
  t.n_market_submitted = n_market_sub;
  t.n_orders_submitted = n_limit_sub + n_market_sub;
  t.n_cancels_sent     = n_cancels;

  double total_slippage = 0.0;
  for (const auto& f : fills) {
    if (f.owner != owner) continue;
    if (f.is_maker) {
      t.n_fills_maker++;
      t.total_qty_maker += static_cast<int64_t>(f.fill_qty);
    } else {
      t.n_fills_taker++;
      t.total_qty_taker += static_cast<int64_t>(f.fill_qty);
      total_slippage    += f.slippage_ticks();
    }
    t.turnover_notional_ticks +=
        static_cast<int64_t>(f.fill_price) * static_cast<int64_t>(f.fill_qty);
  }
  t.total_qty_traded    = t.total_qty_maker + t.total_qty_taker;
  t.total_slippage_ticks = total_slippage;

  t.limit_fill_rate = (n_limit_sub > 0)
      ? static_cast<double>(t.n_fills_maker) / static_cast<double>(n_limit_sub)
      : 0.0;

  t.avg_slippage_ticks = (t.n_fills_taker > 0)
      ? total_slippage / static_cast<double>(t.n_fills_taker)
      : 0.0;

  t.final_position   = final_position;
  t.final_cash_ticks = final_cash_ticks;
  t.final_mtm_pnl    = static_cast<double>(final_cash_ticks)
                     + static_cast<double>(final_position)
                     * static_cast<double>(final_mid);
  return t;
}

} // namespace msim
