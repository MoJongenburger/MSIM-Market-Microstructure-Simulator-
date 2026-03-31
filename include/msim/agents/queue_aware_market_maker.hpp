#pragma once
// ============================================================
// include/msim/agents/queue_aware_market_maker.hpp
//
// Queue-Aware Market Maker
// ========================
// Extends the Avellaneda-Stoikov model with queue position logic.
//
// The classic A-S model assumes continuous quote refreshing.  In
// practice, a market maker must decide:
//
//   1. WHEN to cancel a resting quote:
//      - If at the front of the queue (qty_ahead == 0) and the
//        spread is narrowing or price is moving adversely, cancel
//        immediately — you are about to be adversely selected.
//      - If deep in the queue (queue_fraction > threshold), the
//        quote is unlikely to fill soon; reconsider pricing.
//
//   2. WHEN to stay patient:
//      - If deep in the queue and the spread is wide, stay.
//        Cancelling and reposting costs a queue position.
//      - "Queue priority" is valuable — once you're at the front,
//        you have a fill option with zero adverse selection risk.
//
// This agent implements a simple version of these rules on top
// of the A-S reservation price, demonstrating the queue_positions
// field that makes realistic market making strategies possible.
//
// Reference:
//   Avellaneda & Stoikov (2008), "High-frequency trading in a
//   limit order book", Quantitative Finance 8(3):217-224.
//   Cont & de Larrard (2013), "Price dynamics in a Markovian
//   limit order book market", SIAM J. Financial Math. 4(1).
// ============================================================

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "msim/world.hpp"
#include "msim/order.hpp"
#include "msim/types.hpp"

namespace msim::agents {

struct QueueAwareMMConfig {
  // A-S core parameters (same as MarketMakerASConfig)
  double gamma        = 0.01;
  double kappa        = 1.5;
  int    T_steps      = 500;
  double sigma_init   = 2.0;
  double sigma_ewma   = 0.02;
  double alpha_imb    = 0.5;
  Qty    lot_size     = 1;
  Qty    max_inv      = 20;
  int    warmup       = 10;

  // ── Queue-aware cancel logic ────────────────────────────────────────────
  // cancel_at_front_threshold:
  //   If qty_ahead == 0 (at queue front) AND the mid has moved by more than
  //   this many ticks since the order was posted, cancel and requote.
  //   Rationale: being at the front of a stale quote is dangerous.
  double cancel_at_front_threshold = 1.0;

  // cancel_if_deep_fraction:
  //   If queue_fraction > this value, the quote is so deep it won't fill
  //   soon; cancel and requote at current A-S prices.
  //   0.8 means "cancel if 80% of the level is ahead of me".
  double cancel_if_deep_fraction = 0.80;

  // min_refresh_interval:
  //   Don't requote more often than this (steps).  Prevents thrashing.
  int    min_refresh_steps = 3;
};

class QueueAwareMarketMaker final : public IAgent {
public:
  QueueAwareMarketMaker(OwnerId owner_id, QueueAwareMMConfig cfg = {})
    : owner_(owner_id), cfg_(cfg) {}

  OwnerId owner() const noexcept override { return owner_; }

  void seed(uint64_t /*s*/) override {
    sigma2_          = cfg_.sigma_init * cfg_.sigma_init;
    prev_mid_        = std::nullopt;
    bid_id_ = ask_id_ = 0;
    has_bid_ = has_ask_ = false;
    bid_posted_mid_ = ask_posted_mid_ = 0;
    steps_since_refresh_ = 0;
    step_count_          = 0;
    counter_             = 0;
  }

  void step(Ts ts,
            const MarketView&  view,
            const AgentState&  self,
            std::vector<Action>& out) override
  {
    ++step_count_;
    ++steps_since_refresh_;

    if (!view.best_bid || !view.best_ask) return;

    const double mid = view.mid
        ? static_cast<double>(*view.mid)
        : static_cast<double>(*view.best_bid + *view.best_ask) / 2.0;

    // Update volatility estimate
    if (prev_mid_) {
      const double dm = mid - *prev_mid_;
      sigma2_ = cfg_.sigma_ewma * dm * dm
              + (1.0 - cfg_.sigma_ewma) * sigma2_;
    }
    prev_mid_ = mid;

    if (step_count_ < cfg_.warmup) return;

    // ── Decide whether to cancel existing quotes ──────────────────────────
    bool cancel_bid = false;
    bool cancel_ask = false;

    if (steps_since_refresh_ >= cfg_.min_refresh_steps) {
      for (const auto& qp : self.queue_positions) {

        const double mid_drift = std::abs(mid - static_cast<double>(
            qp.side == Side::Buy ? bid_posted_mid_ : ask_posted_mid_));

        // Rule 1: at front of queue but mid has drifted — adverse selection risk
        if (qp.is_front() && mid_drift > cfg_.cancel_at_front_threshold) {
          if (qp.side == Side::Buy)  cancel_bid = true;
          if (qp.side == Side::Sell) cancel_ask = true;
        }

        // Rule 2: too deep in queue — quote is stale / unlikely to fill
        if (qp.queue_fraction() > cfg_.cancel_if_deep_fraction) {
          if (qp.side == Side::Buy)  cancel_bid = true;
          if (qp.side == Side::Sell) cancel_ask = true;
        }
      }

      // Always refresh if we have no resting orders at all
      if (!has_bid_) cancel_bid = true;
      if (!has_ask_) cancel_ask = true;
    }

    // Issue cancels
    if (has_bid_ && cancel_bid) {
      out.push_back(Action::cancel(bid_id_));
      has_bid_ = false;
    }
    if (has_ask_ && cancel_ask) {
      out.push_back(Action::cancel(ask_id_));
      has_ask_ = false;
    }

    // ── Compute A-S reservation price and spread ──────────────────────────
    const double q   = static_cast<double>(self.position);
    const double T_t = static_cast<double>(cfg_.T_steps);
    const double sigma = std::sqrt(sigma2_);

    double r = mid - q * cfg_.gamma * sigma2_ * T_t;
    r += cfg_.alpha_imb * view.imbalance * sigma;

    const double inv_term   = (2.0 / cfg_.gamma) *
                               std::log(1.0 + cfg_.gamma / cfg_.kappa);
    const double spread_raw = cfg_.gamma * sigma2_ * T_t + inv_term;
    const double spread     = std::max(spread_raw, 2.0);

    const Price bid_px = static_cast<Price>(std::floor(r - spread / 2.0));
    const Price ask_px = static_cast<Price>(std::ceil (r + spread / 2.0));
    if (ask_px <= bid_px) return;

    // ── Post new quotes where needed ──────────────────────────────────────
    if (!has_bid_ && q < static_cast<double>(cfg_.max_inv)) {
      const OrderId oid = next_id();
      out.push_back(Action::submit(make_limit(ts, Side::Buy, bid_px, oid)));
      bid_id_         = oid;
      has_bid_        = true;
      bid_posted_mid_ = static_cast<Price>(std::round(mid));
      steps_since_refresh_ = 0;
    }

    if (!has_ask_ && q > -static_cast<double>(cfg_.max_inv)) {
      const OrderId oid = next_id();
      out.push_back(Action::submit(make_limit(ts, Side::Sell, ask_px, oid)));
      ask_id_         = oid;
      has_ask_        = true;
      ask_posted_mid_ = static_cast<Price>(std::round(mid));
      steps_since_refresh_ = 0;
    }
  }

  double sigma()       const noexcept { return std::sqrt(sigma2_); }
  bool   has_resting() const noexcept { return has_bid_ || has_ask_; }

private:
  OrderId next_id() const noexcept {
    return (owner_ << 24) | (counter_++ & 0xFF'FFFFull);
  }

  Order make_limit(Ts ts, Side side, Price px, OrderId oid) const {
    Order o{};
    o.id        = oid;
    o.owner     = owner_;
    o.side      = side;
    o.type      = OrderType::Limit;
    o.price     = px;
    o.qty       = cfg_.lot_size;
    o.ts        = ts;
    o.tif       = TimeInForce::GTC;
    o.mkt_style = MarketStyle::PureMarket;
    return o;
  }

  OwnerId               owner_;
  QueueAwareMMConfig    cfg_;
  double                sigma2_              = 0.0;
  std::optional<double> prev_mid_            {};
  OrderId               bid_id_              = 0;
  OrderId               ask_id_              = 0;
  bool                  has_bid_             = false;
  bool                  has_ask_             = false;
  Price                 bid_posted_mid_      = 0;
  Price                 ask_posted_mid_      = 0;
  int                   steps_since_refresh_ = 0;
  int                   step_count_          = 0;
  mutable uint64_t      counter_             = 0;
};

} // namespace msim::agents
