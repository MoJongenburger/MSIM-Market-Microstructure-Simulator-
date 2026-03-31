#pragma once
// ============================================================
// include/msim/agents/vwap_agent.hpp
//
// VWAP Execution Agent
// ====================
// Executes a parent order by slicing it into child orders
// proportional to a target volume schedule, targeting the
// Volume-Weighted Average Price over a specified horizon.
//
// Model
// -----
// The VWAP schedule is the empirically observed U-shaped intraday
// volume pattern.  We approximate it with a discretised schedule:
//
//   v_k = V_total * w_k / sum(w)
//
// where w_k is the weight for bucket k.  Three schedule types:
//
//   FLAT    — uniform w_k = 1 (identical to TWAP in execution)
//   U_SHAPE — higher volume at open/close, lower at midday.
//              Approximated as: w_k = 1 + A * cos(pi * k / N_buckets)
//              with A = 0.6.  Matches the broad shape of equity markets.
//   CUSTOM  — user provides an explicit weight vector.
//
// Execution
// ---------
// Each step, the agent computes how many lots remain in the current
// bucket vs how many it has already executed.  If it is behind
// schedule (filled < target_so_far), it submits an IOC market order
// for the deficit.  If ahead, it waits.
//
// The agent tracks its own fills by watching AgentState::position
// change (the ledger does this automatically).
//
// For the purposes of TCA, "arrival price" is the mid when the
// parent order is first submitted (the start of the schedule).
// Implementation shortfall vs VWAP benchmark is computed in the
// companion Python function msim.analysis.vwap_is().
//
// Reference
// ---------
// Madhavan (2002), "VWAP Strategies", Transaction Performance:
// The Changing Face of Trading.
// ============================================================

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "msim/world.hpp"
#include "msim/order.hpp"
#include "msim/types.hpp"

namespace msim::agents {

// ─── Schedule types ───────────────────────────────────────────────────────────
enum class VWAPSchedule {
  FLAT,     // Uniform — equal weight each bucket
  U_SHAPE,  // Higher at open/close, lower midday (typical equity pattern)
  CUSTOM,   // User-supplied weight vector
};

struct VWAPConfig {
  // Parent order
  Qty    total_qty{100};          // Total lots to execute
  Side   side{Side::Buy};         // Direction
  int    n_buckets{20};           // Number of equal-width time buckets

  // Schedule
  VWAPSchedule schedule{VWAPSchedule::U_SHAPE};
  std::vector<double> custom_weights;  // used when schedule=CUSTOM

  // Execution style
  // use_limit: try a passive limit order first, fall back to IOC market
  // if not filled within limit_patience_steps steps.
  bool  use_limit{false};         // false = always IOC market (pure VWAP)
  int   limit_patience_steps{3};  // steps to wait for limit fill before lifting
  Price limit_offset_ticks{1};    // how far inside the spread to post

  // Urgency: if behind schedule by more than urgency_threshold fraction
  // of remaining qty, use market order regardless of use_limit.
  double urgency_threshold{0.10};
};

class VWAPAgent final : public IAgent {
public:
  VWAPAgent(OwnerId owner_id, VWAPConfig cfg = {})
    : owner_(owner_id), cfg_(cfg) {
    if (cfg_.total_qty <= 0)
      throw std::invalid_argument("VWAPAgent: total_qty must be > 0");
    if (cfg_.n_buckets <= 0)
      throw std::invalid_argument("VWAPAgent: n_buckets must be > 0");
    build_schedule();
  }

  // ── IAgent ───────────────────────────────────────────────────────────────
  OwnerId owner() const noexcept override { return owner_; }

  void seed(uint64_t /*s*/) override {
    // Deterministic — reset execution state only
    qty_executed_  = 0;
    step_          = 0;
    done_          = false;
    pending_limit_ = 0;
    pending_id_    = 0;
    arrival_price_ = 0;
    counter_       = 0;
  }

  void step(Ts ts,
            const MarketView&  view,
            const AgentState&  self,
            std::vector<Action>& out) override
  {
    if (done_) return;
    if (!view.best_bid || !view.best_ask) { ++step_; return; }

    // Record arrival price on first step
    if (step_ == 0 && arrival_price_ == 0)
      arrival_price_ = view.mid ? *view.mid
                                : (*view.best_bid + *view.best_ask) / 2;

    // Current filled qty from ledger (signed position for buys,
    // negated position for sells)
    const Qty pos = static_cast<Qty>(
        cfg_.side == Side::Buy ? self.position : -self.position);
    qty_executed_ = pos;

    // Which bucket are we in?
    const int bucket = std::min(
        static_cast<int>(step_ * cfg_.n_buckets / std::max(total_steps_, 1)),
        cfg_.n_buckets - 1);

    // Target filled qty up to and including current bucket
    const Qty target = cumulative_targets_[static_cast<std::size_t>(bucket)];

    // How much we need to trade this step
    const Qty deficit = target - qty_executed_;

    // Cancel any stale pending limit if it's still out there
    if (pending_limit_ > 0 && cfg_.use_limit) {
      out.push_back(Action::cancel(pending_id_));
      pending_limit_ = 0;
    }

    if (deficit > 0) {
      // Are we urgently behind?
      const Qty remaining   = cfg_.total_qty - qty_executed_;
      const double urgency  = static_cast<double>(deficit)
                            / static_cast<double>(std::max(remaining, Qty{1}));
      const bool urgent     = urgency > cfg_.urgency_threshold;

      if (cfg_.use_limit && !urgent) {
        // Post a limit order at best_bid + offset (buy) or best_ask - offset (sell)
        const Price px = (cfg_.side == Side::Buy)
            ? *view.best_bid + cfg_.limit_offset_ticks
            : *view.best_ask - cfg_.limit_offset_ticks;
        const OrderId oid = next_id();
        out.push_back(Action::submit(make_limit(ts, px, deficit, oid)));
        pending_id_    = oid;
        pending_limit_ = deficit;
      } else {
        // IOC market order for the full deficit
        out.push_back(Action::submit(make_market(ts, deficit)));
      }
    }

    ++step_;

    // Done when fully executed or schedule exhausted
    if (qty_executed_ >= cfg_.total_qty || step_ >= total_steps_)
      done_ = true;
  }

  // ── Diagnostics ──────────────────────────────────────────────────────────
  bool   is_done()        const noexcept { return done_; }
  Qty    qty_executed()   const noexcept { return qty_executed_; }
  Qty    qty_remaining()  const noexcept {
    return std::max(Qty{0}, cfg_.total_qty - qty_executed_);
  }
  Price  arrival_price()  const noexcept { return arrival_price_; }
  double pct_complete()   const noexcept {
    return cfg_.total_qty > 0
        ? 100.0 * static_cast<double>(qty_executed_)
              / static_cast<double>(cfg_.total_qty)
        : 0.0;
  }

  void set_total_steps(int n) {
    total_steps_ = std::max(n, 1);
    build_schedule();
  }

private:
  // Build the cumulative VWAP target schedule
  void build_schedule() {
    const int N = cfg_.n_buckets;
    std::vector<double> w(static_cast<std::size_t>(N));

    switch (cfg_.schedule) {
      case VWAPSchedule::FLAT:
        std::fill(w.begin(), w.end(), 1.0);
        break;
      case VWAPSchedule::U_SHAPE:
        for (int k = 0; k < N; ++k) {
          const double x = static_cast<double>(k) / static_cast<double>(N - 1);
          w[static_cast<std::size_t>(k)] = 1.0 + 0.6 * std::cos(M_PI * x);
        }
        break;
      case VWAPSchedule::CUSTOM:
        if (static_cast<int>(cfg_.custom_weights.size()) != N)
          throw std::invalid_argument(
              "VWAPAgent: custom_weights.size() must equal n_buckets");
        w = cfg_.custom_weights;
        break;
    }

    // Normalise and compute cumulative targets
    const double w_sum = std::accumulate(w.begin(), w.end(), 0.0);
    cumulative_targets_.resize(static_cast<std::size_t>(N));
    double cum = 0.0;
    for (int k = 0; k < N; ++k) {
      cum += w[static_cast<std::size_t>(k)] / w_sum;
      cumulative_targets_[static_cast<std::size_t>(k)] = static_cast<Qty>(
          std::round(cum * static_cast<double>(cfg_.total_qty)));
    }
    // Ensure last bucket target == total_qty exactly
    if (!cumulative_targets_.empty())
      cumulative_targets_.back() = cfg_.total_qty;
  }

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

  OwnerId              owner_;
  VWAPConfig           cfg_;
  int                  total_steps_{2000};   // set via set_total_steps()
  std::vector<Qty>     cumulative_targets_;

  // Runtime state (reset by seed())
  Qty      qty_executed_  = 0;
  int      step_          = 0;
  bool     done_          = false;
  Qty      pending_limit_ = 0;
  OrderId  pending_id_    = 0;
  Price    arrival_price_ = 0;
  mutable uint64_t counter_ = 0;
};

} // namespace msim::agents
