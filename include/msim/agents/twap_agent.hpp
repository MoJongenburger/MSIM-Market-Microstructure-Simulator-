#pragma once
// ============================================================
// include/msim/agents/twap_agent.hpp
//
// TWAP Execution Agent
// ====================
// Executes a parent order by slicing it uniformly over a fixed
// horizon — the simplest possible execution benchmark.
//
// Model
// -----
// Child order size per step = ceil(remaining_qty / remaining_steps).
// The last step mops up any rounding remainder.
//
// On each step the agent:
//   1. Computes child_qty = ceil(remaining / remaining_steps)
//   2. Submits one IOC market order (or a GTC limit if use_limit=true)
//   3. Adjusts remaining_qty from ledger position
//
// Unlike VWAP the schedule is not volume-shaped — it is purely
// time-based.  TWAP is used as a cost baseline in TCA: any strategy
// that does not beat TWAP slippage is not adding value.
//
// Passive variant
// ---------------
// When use_limit=true the agent posts a limit order at
//   best_bid (for buys) or best_ask (for sells)
// i.e. it tries to provide liquidity at the touch.  If the limit
// order does not fill within limit_patience_steps, it cancels and
// uses a market order for that bucket.
//
// Reference
// ---------
// Kissell & Glantz (2003), "Optimal Trading Strategies",
// AMACOM.  Chapter 3: TWAP, VWAP and Simple Slicing.
// ============================================================

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "msim/world.hpp"
#include "msim/order.hpp"
#include "msim/types.hpp"

namespace msim::agents {

struct TWAPConfig {
  Qty   total_qty{100};           // Total lots to execute
  Side  side{Side::Buy};          // Direction

  // Execution style
  bool  use_limit{false};         // false = pure market (default)
  int   limit_patience_steps{5};  // steps before cancelling unfilled limit
  Price limit_offset_ticks{0};    // ticks inside spread (0 = at the touch)

  // Minimum child order size to avoid many tiny orders
  Qty   min_child_qty{1};
};

class TWAPAgent final : public IAgent {
public:
  TWAPAgent(OwnerId owner_id, int horizon_steps, TWAPConfig cfg = {})
    : owner_(owner_id), horizon_steps_(horizon_steps), cfg_(cfg)
  {
    if (horizon_steps_ <= 0)
      throw std::invalid_argument("TWAPAgent: horizon_steps must be > 0");
    if (cfg_.total_qty <= 0)
      throw std::invalid_argument("TWAPAgent: total_qty must be > 0");
  }

  // ── IAgent ───────────────────────────────────────────────────────────────
  OwnerId owner() const noexcept override { return owner_; }

  void seed(uint64_t /*s*/) override {
    step_             = 0;
    done_             = false;
    qty_executed_     = 0;
    arrival_price_    = 0;
    pending_id_       = 0;
    pending_qty_      = 0;
    patience_counter_ = 0;
    counter_          = 0;
  }

  void step(Ts ts,
            const MarketView&  view,
            const AgentState&  self,
            std::vector<Action>& out) override
  {
    if (done_) return;
    if (!view.best_bid || !view.best_ask) { ++step_; return; }

    // Record arrival price on first active step
    if (step_ == 0 && arrival_price_ == 0)
      arrival_price_ = view.mid ? *view.mid
                                : (*view.best_bid + *view.best_ask) / 2;

    // Sync executed qty from ledger position
    qty_executed_ = static_cast<Qty>(
        cfg_.side == Side::Buy ? self.position : -self.position);

    const Qty remaining      = cfg_.total_qty - qty_executed_;
    const int remaining_steps = std::max(1, horizon_steps_ - step_);

    if (remaining <= 0) { done_ = true; return; }

    // ── Handle pending limit order ────────────────────────────────────────
    if (pending_qty_ > 0 && cfg_.use_limit) {
      ++patience_counter_;
      if (patience_counter_ < cfg_.limit_patience_steps) {
        ++step_;
        return;   // still waiting
      }
      // Timed out — cancel and fall through to market
      out.push_back(Action::cancel(pending_id_));
      pending_qty_      = 0;
      patience_counter_ = 0;
    }

    // ── Compute child size for this step ─────────────────────────────────
    const Qty child_qty = std::max(
        cfg_.min_child_qty,
        static_cast<Qty>(std::ceil(
            static_cast<double>(remaining) /
            static_cast<double>(remaining_steps))));

    // Clamp to actual remaining
    const Qty to_trade = std::min(child_qty, remaining);

    if (cfg_.use_limit) {
      // Post limit at best bid/ask (or with offset)
      const Price px = (cfg_.side == Side::Buy)
          ? *view.best_bid + cfg_.limit_offset_ticks
          : *view.best_ask - cfg_.limit_offset_ticks;
      const OrderId oid = next_id();
      out.push_back(Action::submit(make_limit(ts, px, to_trade, oid)));
      pending_id_       = oid;
      pending_qty_      = to_trade;
      patience_counter_ = 0;
    } else {
      out.push_back(Action::submit(make_market(ts, to_trade)));
    }

    ++step_;
    if (qty_executed_ + to_trade >= cfg_.total_qty || step_ >= horizon_steps_)
      done_ = true;
  }

  // ── Diagnostics ──────────────────────────────────────────────────────────
  bool   is_done()       const noexcept { return done_; }
  Qty    qty_executed()  const noexcept { return qty_executed_; }
  Qty    qty_remaining() const noexcept {
    return std::max(Qty{0}, cfg_.total_qty - qty_executed_);
  }
  Price  arrival_price() const noexcept { return arrival_price_; }
  double pct_complete()  const noexcept {
    return cfg_.total_qty > 0
        ? 100.0 * static_cast<double>(qty_executed_)
              / static_cast<double>(cfg_.total_qty)
        : 0.0;
  }

private:
  OrderId next_id() const noexcept {
    return (owner_ << 24) | (counter_++ & 0xFF'FFFFull);
  }

  Order make_market(Ts ts, Qty qty) const {
    Order o{};
    o.id        = next_id();
    o.owner     = owner_;
    o.side      = cfg_.side;
    o.type      = OrderType::Market;
    o.price     = 0;
    o.qty       = qty;
    o.ts        = ts;
    o.tif       = TimeInForce::IOC;
    o.mkt_style = MarketStyle::PureMarket;
    return o;
  }

  Order make_limit(Ts ts, Price px, Qty qty, OrderId oid) const {
    Order o{};
    o.id        = oid;
    o.owner     = owner_;
    o.side      = cfg_.side;
    o.type      = OrderType::Limit;
    o.price     = px;
    o.qty       = qty;
    o.ts        = ts;
    o.tif       = TimeInForce::GTC;
    o.mkt_style = MarketStyle::PureMarket;
    return o;
  }

  OwnerId owner_;
  int     horizon_steps_;
  TWAPConfig cfg_;

  // Runtime state (reset by seed())
  int     step_             = 0;
  bool    done_             = false;
  Qty     qty_executed_     = 0;
  Price   arrival_price_    = 0;
  OrderId pending_id_       = 0;
  Qty     pending_qty_      = 0;
  int     patience_counter_ = 0;
  mutable uint64_t counter_ = 0;
};

} // namespace msim::agents
