#pragma once
// ============================================================
// include/msim/agents/momentum_agent.hpp
//
// MACD trend-following trader.
//
//   signal_t = EMA_fast(mid, α_f) − EMA_slow(mid, α_s)
//   α = 2 / (N + 1)
//
// Buys  when signal >  entry_band.
// Sells when signal < −entry_band.
// Flattens when |signal| < exit_band.
// Position capped at ±max_position.
//
// Implements msim::IAgent exactly.
// ============================================================

#include <algorithm>
#include <cmath>
#include <vector>

#include "msim/world.hpp"
#include "msim/order.hpp"
#include "msim/types.hpp"

namespace msim::agents {

struct MomentumConfig {
  double alpha_fast   = 2.0 / 6.0;   // ≈ 5-step EMA
  double alpha_slow   = 2.0 / 21.0;  // ≈ 20-step EMA
  double entry_band   = 0.30;   // signal magnitude (ticks) to open
  double exit_band    = 0.05;   // signal magnitude (ticks) to flatten
  Qty    lot_size     = 3;      // lots per signal
  Qty    max_position = 15;     // absolute inventory cap
  int    warmup_steps = 20;     // steps before any trading
};

class MomentumAgent final : public IAgent {
public:
  MomentumAgent(OwnerId owner_id, MomentumConfig cfg = {})
    : owner_(owner_id), cfg_(cfg) {}

  // ── IAgent ───────────────────────────────────────────────────────────────
  OwnerId owner() const noexcept override { return owner_; }

  // No RNG — reset state deterministically on re-seed
  void seed(uint64_t /*s*/) override {
    ema_fast_    = ema_slow_ = 0.0;
    step_        = 0;
    position_    = 0;
    initialised_ = false;
  }

  void step(Ts ts,
            const MarketView&  view,
            const AgentState&  /*self*/,
            std::vector<Action>& out) override
  {
    // Compute current mid (prefer view.mid; fall back to (bid+ask)/2)
    double mid = 0.0;
    if (view.mid) {
      mid = static_cast<double>(*view.mid);
    } else if (view.best_bid && view.best_ask) {
      mid = static_cast<double>(*view.best_bid + *view.best_ask) / 2.0;
    } else {
      return;
    }

    // Initialise EMAs on first call
    if (!initialised_) {
      ema_fast_ = ema_slow_ = mid;
      initialised_ = true;
    }

    // EMA update: EMA_t = α·mid + (1−α)·EMA_{t−1}
    ema_fast_ = cfg_.alpha_fast * mid + (1.0 - cfg_.alpha_fast) * ema_fast_;
    ema_slow_ = cfg_.alpha_slow * mid + (1.0 - cfg_.alpha_slow) * ema_slow_;
    ++step_;

    if (step_ < cfg_.warmup_steps) return;

    const double signal = ema_fast_ - ema_slow_;

    // Flatten existing position when signal decays
    if (std::abs(signal) < cfg_.exit_band && position_ != 0) {
      const Side exit_side = (position_ > 0) ? Side::Sell : Side::Buy;
      const Qty  exit_qty  = std::abs(position_);
      out.push_back(Action::submit(make_order(ts, exit_side, exit_qty)));
      position_ = 0;
      return;
    }

    // Open / add to long
    if (signal > cfg_.entry_band && position_ < cfg_.max_position) {
      const Qty add = std::min(cfg_.lot_size, cfg_.max_position - position_);
      if (add > 0) {
        out.push_back(Action::submit(make_order(ts, Side::Buy, add)));
        position_ += add;
      }
    }
    // Open / add to short
    else if (signal < -cfg_.entry_band && position_ > -cfg_.max_position) {
      const Qty add = std::min(cfg_.lot_size, cfg_.max_position + position_);
      if (add > 0) {
        out.push_back(Action::submit(make_order(ts, Side::Sell, add)));
        position_ -= add;
      }
    }
  }

  double signal()   const noexcept { return ema_fast_ - ema_slow_; }
  Qty    position() const noexcept { return position_; }

private:
  Order make_order(Ts ts, Side side, Qty qty) const {
    Order o{};
    o.id        = (owner_ << 24) | (counter_++ & 0xFF'FFFFull);
    o.owner     = owner_;
    o.side      = side;
    o.type      = OrderType::Market;
    o.price     = 0;
    o.qty       = qty;
    o.ts        = ts;
    o.tif       = TimeInForce::IOC;
    o.mkt_style = MarketStyle::PureMarket;
    return o;
  }

  OwnerId        owner_;
  MomentumConfig cfg_;
  double         ema_fast_    = 0.0;
  double         ema_slow_    = 0.0;
  int            step_        = 0;
  Qty            position_    = 0;
  bool           initialised_ = false;
  mutable uint64_t counter_  = 0;
};

} // namespace msim::agents
