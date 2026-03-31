#pragma once
// ============================================================
// include/msim/agents/is_agent.hpp
//
// Implementation Shortfall (IS) / Almgren-Chriss Agent
// ======================================================
// Executes a parent order using the optimal trajectory derived
// from the Almgren-Chriss (2000) model.
//
// Background
// ----------
// Implementation Shortfall is defined as:
//
//   IS = (execution_price - arrival_price) * qty  [buys]
//      = (arrival_price - execution_price) * qty  [sells]
//
// The Almgren-Chriss model minimises a utility function that
// trades off expected IS against IS variance:
//
//   U = E[IS] + λ * Var[IS]
//
// where λ is the risk aversion parameter.
//
// Optimal trajectory
// ------------------
// Under linear temporary impact η and linear permanent impact γ:
//
//   x_k = X * sinh(κ(T-t_k)) / sinh(κT)
//
// where:
//   X    = total quantity to execute
//   κ    = sqrt(λ * σ² / η)   (urgency parameter)
//   T    = horizon in steps
//   t_k  = k (step index)
//
// Special cases:
//   λ → 0  : κ → 0, trajectory approaches TWAP (even distribution)
//   λ → ∞  : κ → ∞, trajectory front-loads (act immediately)
//
// This is the canonical model in execution finance and is the
// standard benchmark against which more sophisticated algorithms
// (e.g. RL-based) are measured.
//
// Implementation notes
// --------------------
// - The trajectory is pre-computed in seed() as a vector of
//   target_qty[k] for k = 0..T-1.
// - The agent tracks its own fills via AgentState::position.
// - If behind target (execution < target), it submits an IOC market
//   order for the shortfall.  If ahead, it waits.
// - Optional adaptive recomputation: every adapt_interval steps,
//   κ is recomputed using the current remaining qty and steps,
//   simulating a "receding horizon" controller.
//
// Reference
// ---------
// Almgren & Chriss (2000), "Optimal execution of portfolio
// transactions", Journal of Risk 3(2):5-39.
// Almgren (2003), "Optimal execution with nonlinear impact",
// Journal of Risk 5(2):1-24.
// ============================================================

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "msim/world.hpp"
#include "msim/order.hpp"
#include "msim/types.hpp"

namespace msim::agents {

struct ISConfig {
  // Parent order
  Qty    total_qty{100};      // Total lots to execute
  Side   side{Side::Buy};     // Direction

  // Almgren-Chriss model parameters
  // ─────────────────────────────────────────────────────────────
  // risk_aversion (λ): controls urgency.
  //   0.0  → TWAP (fully patient, no variance penalty)
  //   0.01 → moderate urgency (default — good starting point)
  //   0.1  → aggressive (front-loads execution)
  double risk_aversion{0.01};

  // sigma: annualised price volatility in ticks/step.
  // Estimated from recent mid-price moves if sigma_ewma > 0.
  // Set explicitly if you know the volatility.
  double sigma{2.0};

  // eta: temporary market impact coefficient (ticks / lot).
  // Controls how much a single trade moves the price temporarily.
  // Calibrate as: observed spread / typical trade size.
  double eta{0.5};

  // gamma: permanent market impact (ticks / lot).
  // Fraction of temporary impact that persists.  Usually eta/2.
  double gamma{0.25};

  // Adaptive recomputation of trajectory
  // Every adapt_interval steps, recompute κ using current remaining.
  // 0 = no recomputation (fixed trajectory).
  int adapt_interval{0};

  // Sigma estimation: EWMA of squared mid-price changes.
  // 0 = use cfg.sigma directly (no estimation).
  double sigma_ewma{0.0};
};

class ISAgent final : public IAgent {
public:
  ISAgent(OwnerId owner_id, int horizon_steps, ISConfig cfg = {})
    : owner_(owner_id), horizon_steps_(horizon_steps), cfg_(cfg)
  {
    if (horizon_steps_ <= 0)
      throw std::invalid_argument("ISAgent: horizon_steps must be > 0");
    if (cfg_.total_qty <= 0)
      throw std::invalid_argument("ISAgent: total_qty must be > 0");
  }

  // ── IAgent ───────────────────────────────────────────────────────────────
  OwnerId owner() const noexcept override { return owner_; }

  void seed(uint64_t /*s*/) override {
    step_          = 0;
    done_          = false;
    qty_executed_  = 0;
    arrival_price_ = 0;
    sigma2_        = cfg_.sigma * cfg_.sigma;
    prev_mid_      = 0;
    counter_       = 0;
    build_trajectory(cfg_.total_qty, horizon_steps_);
  }

  void step(Ts ts,
            const MarketView&  view,
            const AgentState&  self,
            std::vector<Action>& out) override
  {
    if (done_) return;
    if (!view.best_bid || !view.best_ask) { ++step_; return; }

    const Price cur_mid = view.mid
        ? *view.mid
        : (*view.best_bid + *view.best_ask) / 2;

    // Record arrival price on first step
    if (step_ == 0 && arrival_price_ == 0)
      arrival_price_ = cur_mid;

    // ── EWMA volatility update ───────────────────────────────────────────
    if (cfg_.sigma_ewma > 0.0 && prev_mid_ > 0) {
      const double dm = static_cast<double>(cur_mid - prev_mid_);
      sigma2_ = cfg_.sigma_ewma * dm * dm
              + (1.0 - cfg_.sigma_ewma) * sigma2_;
    }
    prev_mid_ = cur_mid;

    // ── Adaptive trajectory recomputation ─────────────────────────────────
    if (cfg_.adapt_interval > 0 && step_ > 0
        && (step_ % cfg_.adapt_interval) == 0)
    {
      const Qty remaining      = cfg_.total_qty - qty_executed_;
      const int remaining_steps = horizon_steps_ - step_;
      if (remaining > 0 && remaining_steps > 0) {
        // Use updated sigma if EWMA is on
        if (cfg_.sigma_ewma > 0.0) {
          ISConfig adapted   = cfg_;
          adapted.sigma      = std::sqrt(std::max(sigma2_, 1e-8));
          build_trajectory_with(remaining, remaining_steps, adapted,
                                trajectory_);
        } else {
          build_trajectory(remaining, remaining_steps);
        }
      }
    }

    // ── Sync executed qty from ledger ─────────────────────────────────────
    qty_executed_ = static_cast<Qty>(
        cfg_.side == Side::Buy ? self.position : -self.position);

    const Qty remaining = cfg_.total_qty - qty_executed_;
    if (remaining <= 0) { done_ = true; return; }

    // ── How much should we have traded by now? ────────────────────────────
    const Qty target = (step_ < static_cast<int>(trajectory_.size()))
        ? trajectory_[static_cast<std::size_t>(step_)]
        : cfg_.total_qty;

    const Qty deficit = target - qty_executed_;

    if (deficit > 0) {
      const Qty to_trade = std::min(deficit, remaining);
      out.push_back(Action::submit(make_market(ts, to_trade)));
    }

    ++step_;
    if (qty_executed_ >= cfg_.total_qty || step_ >= horizon_steps_)
      done_ = true;
  }

  // ── Diagnostics ──────────────────────────────────────────────────────────
  bool   is_done()        const noexcept { return done_; }
  Qty    qty_executed()   const noexcept { return qty_executed_; }
  Qty    qty_remaining()  const noexcept {
    return std::max(Qty{0}, cfg_.total_qty - qty_executed_);
  }
  Price  arrival_price()  const noexcept { return arrival_price_; }
  double urgency()        const noexcept { return kappa_; }
  double pct_complete()   const noexcept {
    return cfg_.total_qty > 0
        ? 100.0 * static_cast<double>(qty_executed_)
              / static_cast<double>(cfg_.total_qty)
        : 0.0;
  }

  // Theoretical expected IS under current model parameters (ticks)
  double expected_is() const noexcept {
    // E[IS] ≈ gamma * X^2 / (2*T) + eta * sum_k (n_k^2)
    // For the AC optimal trajectory this simplifies to:
    // E[IS] = 0.5 * gamma * X * T + 0.5 * eta * X / T
    //       (approx for κT << 1)
    const double X = static_cast<double>(cfg_.total_qty);
    const double T = static_cast<double>(horizon_steps_);
    return 0.5 * cfg_.gamma * X * X / T
         + 0.5 * cfg_.eta   * X * X / T;
  }

private:
  // Build AC optimal trajectory: x_k = X * sinh(κ(T-k)) / sinh(κT)
  // trajectory_[k] = cumulative qty to have executed by step k
  void build_trajectory(Qty total, int T) {
    ISConfig c = cfg_;
    c.sigma = std::sqrt(std::max(sigma2_, 1e-8));
    build_trajectory_with(total, T, c, trajectory_);
  }

  static void build_trajectory_with(Qty total, int T,
                                     const ISConfig& c,
                                     std::vector<Qty>& traj)
  {
    traj.resize(static_cast<std::size_t>(T));
    const double X     = static_cast<double>(total);
    const double lam   = c.risk_aversion;
    const double sigma = c.sigma;
    const double eta   = std::max(c.eta, 1e-8);
    const double kappa = std::sqrt(lam * sigma * sigma / eta);

    if (kappa * static_cast<double>(T) < 1e-6) {
      // κ → 0: degenerate to TWAP
      for (int k = 0; k < T; ++k) {
        traj[static_cast<std::size_t>(k)] = static_cast<Qty>(
            std::round(X * static_cast<double>(k + 1)
                         / static_cast<double>(T)));
      }
    } else {
      const double sinhKT = std::sinh(kappa * static_cast<double>(T));
      for (int k = 0; k < T; ++k) {
        // Cumulative executed by end of step k:
        // cum_k = X * (1 - sinh(κ(T-k)) / sinh(κT))
        const double cum = X * (1.0 - std::sinh(
            kappa * static_cast<double>(T - k - 1)) / sinhKT);
        traj[static_cast<std::size_t>(k)] = static_cast<Qty>(
            std::min(std::round(cum), static_cast<double>(total)));
      }
    }
    // Guarantee last step = total
    if (!traj.empty()) traj.back() = total;

    // Cache urgency for diagnostics
    (void)kappa;
  }

  void update_kappa() {
    const double sigma = std::sqrt(std::max(sigma2_, 1e-8));
    kappa_ = std::sqrt(cfg_.risk_aversion * sigma * sigma
                       / std::max(cfg_.eta, 1e-8));
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

  OwnerId owner_;
  int     horizon_steps_;
  ISConfig cfg_;

  std::vector<Qty> trajectory_;   // pre-computed cumulative targets

  // Runtime state (reset by seed())
  int     step_          = 0;
  bool    done_          = false;
  Qty     qty_executed_  = 0;
  Price   arrival_price_ = 0;
  Price   prev_mid_      = 0;
  double  sigma2_        = 0.0;
  double  kappa_         = 0.0;
  mutable uint64_t counter_ = 0;
};

} // namespace msim::agents
