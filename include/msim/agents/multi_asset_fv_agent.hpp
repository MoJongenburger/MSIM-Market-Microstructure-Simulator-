#pragma once
// ============================================================
// include/msim/agents/multi_asset_fv_agent.hpp
//
// Informed trader using a correlated fundamental value signal
// shared across multiple assets (instruments).
//
// The agent holds a shared_ptr<SharedFundamental> and an asset
// index.  On each step it reads V = shared_fv->get(asset_idx, t)
// from the pre-computed signal sequence and trades when the gap
// between V and the current quote exceeds a threshold, exactly as
// the single-asset FundamentalValueAgent does.
//
// Cross-asset information leakage arises naturally: if asset A
// and asset B have correlated fundamentals (ρ > 0), an informed
// trader on asset A can infer the direction of asset B.  Running
// two correlated World instances with MultiAssetFVAgents that share
// the same SharedFundamental demonstrates this empirically via the
// correlation of trade signs across markets.
//
// Implements msim::IAgent exactly.
// ============================================================

#include <cmath>
#include <memory>
#include <vector>

#include "msim/shared_fundamental.hpp"
#include "msim/world.hpp"
#include "msim/order.hpp"
#include "msim/types.hpp"

namespace msim::agents {

struct MultiAssetFVConfig {
  std::size_t asset_idx  = 0;      // which asset in SharedFundamental
  double      threshold  = 1.0;    // min mispricing (ticks) to trade
  Qty         lot_size   = 5;
};

class MultiAssetFVAgent final : public IAgent {
public:
  // shared_fv must have already called generate() before World::run().
  MultiAssetFVAgent(OwnerId owner_id,
                    std::shared_ptr<SharedFundamental> shared_fv,
                    MultiAssetFVConfig cfg = {})
    : owner_(owner_id), sf_(std::move(shared_fv)), cfg_(cfg) {}

  // ── IAgent ───────────────────────────────────────────────────────────────
  OwnerId owner() const noexcept override { return owner_; }

  void seed(uint64_t /*s*/) override {
    // Signal is pre-generated and reproducible from SharedFundamental::generate().
    // Reset step counter so replay starts from the beginning of the signal.
    step_ = 0;
  }

  void step(Ts /*ts*/,
            const MarketView&  view,
            const AgentState&  /*self*/,
            std::vector<Action>& out) override
  {
    if (!view.best_bid || !view.best_ask) { ++step_; return; }

    // ── 1. Read pre-computed fundamental value ───────────────────────
    double V = sf_->get(cfg_.asset_idx, step_);
    ++step_;

    // ── 2. On very first step, shift signal to current mid ───────────
    //    (so agents start with V ≈ market price)
    if (step_ == 1 && !initialised_) {
      const double mid = view.mid
          ? static_cast<double>(*view.mid)
          : static_cast<double>(*view.best_bid + *view.best_ask) / 2.0;
      // Compute the offset between signal and market and cache it.
      // We apply the same offset every step so the OU dynamics are preserved.
      v_offset_     = mid - V;
      initialised_  = true;
      V            += v_offset_;
    } else if (initialised_) {
      V += v_offset_;
    }

    // ── 3. Trade when mispricing exceeds threshold ───────────────────
    const double ask = static_cast<double>(*view.best_ask);
    const double bid = static_cast<double>(*view.best_bid);

    if (V - ask > cfg_.threshold)
      out.push_back(Action::submit(make_order(/* ts already in step arg */ 0,
                                              Side::Buy)));
    else if (bid - V > cfg_.threshold)
      out.push_back(Action::submit(make_order(0, Side::Sell)));
  }

  double fundamental_value() const noexcept {
    if (!sf_->is_generated() || step_ == 0) return 0.0;
    return sf_->get(cfg_.asset_idx, step_ - 1) + v_offset_;
  }

  std::size_t asset_index() const noexcept { return cfg_.asset_idx; }

private:
  // ts is overwritten by world.cpp anyway; pass 0
  Order make_order(Ts ts, Side side) const {
    Order o{};
    o.id        = (owner_ << 24) | (counter_++ & 0xFF'FFFFull);
    o.owner     = owner_;
    o.side      = side;
    o.type      = OrderType::Market;
    o.price     = 0;
    o.qty       = cfg_.lot_size;
    o.ts        = ts;
    o.tif       = TimeInForce::IOC;
    o.mkt_style = MarketStyle::PureMarket;
    return o;
  }

  OwnerId                            owner_;
  std::shared_ptr<SharedFundamental> sf_;
  MultiAssetFVConfig                 cfg_;
  int                                step_        = 0;
  bool                               initialised_ = false;
  double                             v_offset_    = 0.0;
  mutable uint64_t                   counter_     = 0;
};

} // namespace msim::agents
