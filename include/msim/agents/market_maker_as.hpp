#pragma once
// ============================================================
// include/msim/agents/market_maker_as.hpp
//
// Avellaneda-Stoikov (2008) optimal market maker with LOB
// imbalance skew.
//
// ── Model ────────────────────────────────────────────────────
// The A-S model solves the HJB equation for a risk-averse MM:
//
//   Reservation price (inventory-adjusted mid):
//     r = s − q · γ · σ² · (T − t)
//
//   Optimal full spread:
//     δ* = γ · σ² · (T − t)  +  (2/γ) · ln(1 + γ/κ)
//
//   Optimal quotes:
//     bid* = r − δ*/2,    ask* = r + δ*/2
//
// where:
//   s   = current mid-price (ticks)
//   q   = signed inventory (+ = long)
//   γ   = absolute risk aversion parameter
//   σ²  = estimated price variance per step (tick²)
//   κ   = order arrival intensity parameter
//   T−t = remaining horizon in steps (rolling window)
//
// ── LOB Imbalance Extension ───────────────────────────────────
// Real order flow is predictable from the imbalance I:
//
//   I = (V_bid − V_ask) / (V_bid + V_ask) ∈ [−1, 1]
//
// We shift the reservation price by:
//   r → r + α_imb · I · σ
//
// When I > 0 (bid heavier), the MM shifts quotes up, anticipating
// upward price pressure.  This captures the empirical finding
// (Cont, Kukanov & Stoikov 2014) that LOB imbalance is a strong
// short-term predictor of price moves.
//
// ── Volatility Estimation ────────────────────────────────────
// σ² is estimated from a running EWMA of squared mid-price changes:
//   σ²_t = α_σ · (Δmid_t)² + (1 − α_σ) · σ²_{t−1}
//
// ── Implementation Notes ─────────────────────────────────────
// - The agent cancels its resting bid/ask at the start of each
//   step and re-quotes at the new optimal prices.
// - If |q| ≥ max_inv the MM stops quoting on the long/short side.
// - Prices are rounded to the nearest tick (integer).
// - Minimum spread enforced: ask ≥ bid + 2 ticks.
//
// Reference:
//   Avellaneda & Stoikov (2008), "High-frequency trading in a
//   limit order book", Quantitative Finance 8(3):217-224.
//   Cont, Kukanov & Stoikov (2014), "The price impact of order
//   book events", Journal of Financial Econometrics 12(1):47-88.
// ============================================================

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "msim/world.hpp"
#include "msim/order.hpp"
#include "msim/types.hpp"

namespace msim::agents {

struct MarketMakerASConfig {
  // A-S risk aversion (higher → wider spread, stronger inventory skew)
  double gamma        = 0.01;

  // A-S order arrival intensity (κ in the paper; higher → tighter spread)
  double kappa        = 1.5;

  // Rolling horizon in steps.  Reservation price skew = γ·σ²·T_steps.
  int    T_steps      = 500;

  // Initial σ estimate (ticks/step).  EWMA updates each step.
  double sigma_init   = 2.0;

  // EWMA decay for σ² estimation (smaller α = slower adaptation)
  double sigma_ewma   = 0.02;

  // LOB imbalance sensitivity (0 = ignore imbalance, 1 = full shift)
  double alpha_imb    = 0.5;

  // Order size and inventory limits
  Qty    lot_size     = 1;
  Qty    max_inv      = 20;      // hard cap: stop quoting beyond this

  // Minimum tick distance from mid for each quote
  int    min_half_spread_ticks = 1;

  // Warm-up steps before quoting (wait for σ to stabilise)
  int    warmup       = 10;
};

class MarketMakerAS final : public IAgent {
public:
  MarketMakerAS(OwnerId owner_id, MarketMakerASConfig cfg = {})
    : owner_(owner_id), cfg_(cfg) {}

  // ── IAgent ───────────────────────────────────────────────────────────────
  OwnerId owner() const noexcept override { return owner_; }

  void seed(uint64_t /*s*/) override {
    // Deterministic — no RNG used
    sigma2_     = cfg_.sigma_init * cfg_.sigma_init;
    prev_mid_   = std::nullopt;
    bid_id_     = 0;
    ask_id_     = 0;
    has_bid_    = false;
    has_ask_    = false;
    step_count_ = 0;
  }

  void step(Ts ts,
            const MarketView&  view,
            const AgentState&  self,
            std::vector<Action>& out) override
  {
    ++step_count_;

    // ── 1. Cancel stale resting quotes ──────────────────────────────
    if (has_bid_) { out.push_back(Action::cancel(bid_id_)); has_bid_ = false; }
    if (has_ask_) { out.push_back(Action::cancel(ask_id_)); has_ask_ = false; }

    if (!view.best_bid || !view.best_ask) return;

    const double mid = view.mid
        ? static_cast<double>(*view.mid)
        : static_cast<double>(*view.best_bid + *view.best_ask) / 2.0;

    // ── 2. Update σ² estimate ────────────────────────────────────────
    if (prev_mid_) {
      const double dm = mid - *prev_mid_;
      sigma2_ = cfg_.sigma_ewma * (dm * dm)
                + (1.0 - cfg_.sigma_ewma) * sigma2_;
    }
    prev_mid_ = mid;

    if (step_count_ < cfg_.warmup) return;

    // ── 3. A-S reservation price with imbalance skew ─────────────────
    const double q   = static_cast<double>(self.position);
    const double T_t = static_cast<double>(cfg_.T_steps);   // rolling horizon

    //   r = s − q · γ · σ² · (T−t)
    double r = mid - q * cfg_.gamma * sigma2_ * T_t;

    //   imbalance shift: r → r + α_imb · I · σ
    const double sigma = std::sqrt(sigma2_);
    r += cfg_.alpha_imb * view.imbalance * sigma;

    // ── 4. A-S optimal spread ────────────────────────────────────────
    //   δ* = γ·σ²·(T−t)  +  (2/γ)·ln(1 + γ/κ)
    const double inv_term   = (2.0 / cfg_.gamma)
                              * std::log(1.0 + cfg_.gamma / cfg_.kappa);
    const double spread_raw = cfg_.gamma * sigma2_ * T_t + inv_term;

    // Enforce minimum spread
    const double min_spread = static_cast<double>(
        2 * cfg_.min_half_spread_ticks);
    const double spread = std::max(spread_raw, min_spread);

    // ── 5. Compute quoted prices (round to nearest tick) ─────────────
    const Price bid_px = static_cast<Price>(std::floor(r - spread / 2.0));
    const Price ask_px = static_cast<Price>(std::ceil (r + spread / 2.0));

    // Sanity: ask must be strictly above bid
    if (ask_px <= bid_px) return;

    // ── 6. Post bid (unless inventory is at cap on long side) ─────────
    if (q < static_cast<double>(cfg_.max_inv)) {
      const OrderId oid = next_id();
      out.push_back(Action::submit(make_limit(ts, Side::Buy, bid_px, oid)));
      bid_id_  = oid;
      has_bid_ = true;
    }

    // ── 7. Post ask (unless inventory is at cap on short side) ────────
    if (q > -static_cast<double>(cfg_.max_inv)) {
      const OrderId oid = next_id();
      out.push_back(Action::submit(make_limit(ts, Side::Sell, ask_px, oid)));
      ask_id_  = oid;
      has_ask_ = true;
    }
  }

  // Diagnostics
  double sigma()        const noexcept { return std::sqrt(sigma2_); }
  double sigma_sq()     const noexcept { return sigma2_; }
  bool   has_resting()  const noexcept { return has_bid_ || has_ask_; }

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

  OwnerId              owner_;
  MarketMakerASConfig  cfg_;
  double               sigma2_     = 0.0;
  std::optional<double> prev_mid_  {};
  OrderId              bid_id_     = 0;
  OrderId              ask_id_     = 0;
  bool                 has_bid_    = false;
  bool                 has_ask_    = false;
  int                  step_count_ = 0;
  mutable uint64_t     counter_    = 0;
};

} // namespace msim::agents
