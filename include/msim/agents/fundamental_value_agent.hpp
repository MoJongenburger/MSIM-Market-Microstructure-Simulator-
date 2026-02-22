#pragma once
// ============================================================
// include/msim/agents/fundamental_value_agent.hpp
//
// Glosten-Milgrom informed trader.
// Private signal follows discrete Ornstein-Uhlenbeck process:
//
//   V_{t+1} = V_t + κ(μ − V_t) + σ_v · Z,   Z ~ N(0,1)
//
// Buys  when V_t − ask > threshold  (asset underpriced).
// Sells when bid − V_t > threshold  (asset overpriced).
// Submits IOC market orders — no resting quotes.
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
  double sigma_v   = 1.5;   // volatility of fundamental value (ticks/step)
  double threshold = 1.0;   // minimum mispricing (ticks) before trading
  Qty    lot_size  = 5;     // fixed order size in lots
  // Long-run mean mu: if 0, initialised to first observed mid-price.
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
    initialised_ = false;   // re-initialise V on first step after re-seed
  }

  void step(Ts ts,
            const MarketView&  view,
            const AgentState&  /*self*/,
            std::vector<Action>& out) override
  {
    if (!view.best_bid || !view.best_ask) return;

    // Initialise V to first observed mid-price
    if (!initialised_) {
      if (view.mid)
        V_ = mu_ = static_cast<double>(*view.mid);
      else
        V_ = mu_ = static_cast<double>(*view.best_bid + *view.best_ask) / 2.0;
      initialised_ = true;
    }

    // Step OU process: V_{t+1} = V_t + κ(μ − V_t) + σ_v · Z
    V_ += cfg_.kappa * (mu_ - V_) + cfg_.sigma_v * normal_(rng_);

    const double ask = static_cast<double>(*view.best_ask);
    const double bid = static_cast<double>(*view.best_bid);

    if (V_ - ask > cfg_.threshold)
      out.push_back(Action::submit(make_order(ts, Side::Buy)));
    else if (bid - V_ > cfg_.threshold)
      out.push_back(Action::submit(make_order(ts, Side::Sell)));
  }

  // For optional FV signal logging in world.cpp
  double fundamental_value() const noexcept { return V_; }

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
  double                           V_  = 0.0;
  double                           mu_ = 0.0;
  mutable uint64_t                 counter_ = 0;
};

} // namespace msim::agents
