#pragma once
// ============================================================
// include/msim/agents/fundamental_value_agent.hpp
//
// Private-signal mean-reversion agent (inspired by Glosten-Milgrom).
//
// The agent holds a latent fundamental value V_t, known only to itself,
// which follows a discrete Ornstein-Uhlenbeck process anchored to mu:
//
//   V_{t+1} = V_t + kappa * (mu - V_t) + sigma_v * Z,   Z ~ N(0,1)
//
// mu is a FIXED scalar initialised once from the first observed mid-price
// at simulation start and does not change during the run.  The agent
// therefore mean-reverts to the initial market mid-price level, not to
// the current mid at each step.
//
// Trading rule (IOC market orders, no resting quotes):
//   Buy  when  V_t - ask > threshold   (asset underpriced vs. private signal)
//   Sell when  bid - V_t > threshold   (asset overpriced  vs. private signal)
//
// This produces directional pressure and endogenous price discovery but
// does not implement the full Glosten-Milgrom informed-trader equilibrium
// (which requires an exogenous value process and a learning market maker).
//
// Implements msim::IAgent exactly.
// ============================================================
#include <cmath>
#include <random>
#include <vector>
#include "msim/world.hpp"   // IAgent, MarketView, AgentState, Action
#include "msim/order.hpp"   // Order, TimeInForce, MarketStyle
#include "msim/types.hpp"   // Price, Qty, Ts, Side, OrderType, OwnerId

namespace msim::agents {

struct FundamentalValueConfig {
  double kappa     = 0.005; // OU mean-reversion speed per step
  double sigma_v   = 1.5;   // volatility of the value process (ticks/step)
  double threshold = 1.0;   // minimum mispricing (ticks) before trading
  Qty    lot_size  = 5;     // fixed order size in lots
  // mu (long-run anchor): if 0.0, initialised from first observed mid-price
  // and then held fixed for the remainder of the simulation.
  double mu        = 0.0;
};

class FundamentalValueAgent final : public IAgent {
public:
  FundamentalValueAgent(OwnerId owner_id,
                        FundamentalValueConfig cfg = {})
    : owner_(owner_id), cfg_(cfg) {}

  // ── IAgent ───────────────────────────────────────────────────────────────
  OwnerId owner() const noexcept override { return owner_; }

  void seed(uint64_t s) override {
    rng_.seed(s);
    initialised_ = false;   // re-initialise V and mu on first step after re-seed
  }

  void step(Ts ts,
            const MarketView&  view,
            const AgentState&  /*self*/,
            std::vector<Action>& out) override
  {
    if (!view.best_bid || !view.best_ask) return;

    // One-time initialisation: set V and the fixed anchor mu from the
    // first observed mid-price (or from cfg_.mu if pre-configured).
    if (!initialised_) {
      double init_mid;
      if (view.mid)
        init_mid = static_cast<double>(*view.mid);
      else
        init_mid = static_cast<double>(*view.best_bid + *view.best_ask) / 2.0;

      V_  = init_mid;
      mu_ = (cfg_.mu != 0.0) ? cfg_.mu : init_mid;  // use cfg override if set
      initialised_ = true;
    }

    // Step OU process around the fixed anchor mu:
    //   V_{t+1} = V_t + kappa * (mu - V_t) + sigma_v * Z
    V_ += cfg_.kappa * (mu_ - V_) + cfg_.sigma_v * normal_(rng_);

    const double ask = static_cast<double>(*view.best_ask);
    const double bid = static_cast<double>(*view.best_bid);

    if (V_ - ask > cfg_.threshold)
      out.push_back(Action::submit(make_order(ts, Side::Buy)));
    else if (bid - V_ > cfg_.threshold)
      out.push_back(Action::submit(make_order(ts, Side::Sell)));
  }

  // For optional FV signal logging in world.cpp
  double fundamental_value()  const noexcept { return V_;  }
  double fundamental_anchor() const noexcept { return mu_; }

private:
  Order make_order(Ts ts, Side side) const {
    Order o{};
    // Unique ID: pack owner into high bits, counter into low 24 bits
    o.id        = (owner_ << 24) | (counter_++ & 0xFF'FFFFull);
    o.owner     = owner_;
    o.side      = side;
    o.type      = OrderType::Market;
    o.price     = 0;              // ignored for market orders
    o.qty       = cfg_.lot_size;
    o.ts        = ts;
    o.tif       = TimeInForce::IOC;
    o.mkt_style = MarketStyle::PureMarket;
    return o;
  }

  OwnerId                          owner_;
  FundamentalValueConfig           cfg_;
  std::mt19937_64                  rng_{};
  std::normal_distribution<double> normal_{0.0, 1.0};
  bool                             initialised_ = false;
  double                           V_  = 0.0;   // current private value estimate
  double                           mu_ = 0.0;   // fixed anchor (set once at init)
  mutable uint64_t                 counter_ = 0;
};

} // namespace msim::agents
