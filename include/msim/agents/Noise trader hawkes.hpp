#pragma once
// ============================================================
// include/msim/agents/noise_trader_hawkes.hpp
//
// Noise trader with Hawkes (self-exciting) order arrival process.
//
// Standard noise traders submit orders at a flat Poisson rate.
// This agent replaces that with a Hawkes process:
//
//   λ(t) = μ + ψ(t),   ψ(t) = Σ_i α · exp(−β · (t − t_i))
//
// Every trade this agent executes *and* every trade it observes
// (via view.last_trade changing) excites future arrivals, producing
// the empirically observed intraday clustering of order flow.
//
// When the agent decides to act (Bernoulli draw against λ(t)·dt):
//   - With prob p_market: submit IOC market order (random side)
//   - With prob 1−p_market: submit GTC limit order around mid
//     at price mid ± U[min_offset, max_offset] ticks
//
// The limit order side is biased by the imbalance in view:
//   imbalance > 0 → slightly more likely to buy (join the pressure)
//
// Implements msim::IAgent exactly.
// ============================================================

#include <algorithm>
#include <cmath>
#include <optional>
#include <random>
#include <vector>

#include "msim/hawkes_process.hpp"
#include "msim/world.hpp"
#include "msim/order.hpp"
#include "msim/types.hpp"

namespace msim::agents {

struct HawkesNoiseConfig {
  // Hawkes process parameters
  HawkesConfig hawkes{
      .mu    = 10.0,   // baseline 10 orders/second
      .alpha =  5.0,   // excitation per event
      .beta  = 20.0,   // decay 20/s → half-life ~35ms
  };

  // Order type mix
  double p_market      = 0.40;   // probability of market vs limit order

  // Limit order placement around mid
  int    min_offset    = 1;      // ticks from mid
  int    max_offset    = 5;

  // Imbalance sensitivity for side selection
  // 0 = random side; 1 = fully follow imbalance
  double imbalance_bias = 0.3;

  // Fixed order size
  Qty    lot_size      = 1;

  // Step width (nanoseconds) — must match WorldConfig::dt_ns
  Ts     dt_ns         = 1'000'000;   // 1 ms default
};

class HawkesNoiseTrader final : public IAgent {
public:
  HawkesNoiseTrader(OwnerId owner_id, HawkesNoiseConfig cfg = {})
    : owner_(owner_id), cfg_(cfg), hawkes_(cfg.hawkes) {}

  // ── IAgent ───────────────────────────────────────────────────────────────
  OwnerId owner() const noexcept override { return owner_; }

  void seed(uint64_t s) override {
    rng_.seed(s);
    hawkes_.reset();
    last_trade_price_ = std::nullopt;
    step_count_       = 0;
  }

  void step(Ts ts,
            const MarketView&  view,
            const AgentState&  /*self*/,
            std::vector<Action>& out) override
  {
    ++step_count_;

    // ── 1. Detect external trades to excite Hawkes ─────────────────────
    int external_events = 0;
    if (view.last_trade && last_trade_price_ != view.last_trade) {
      external_events = 1;     // at least one trade occurred since last step
      last_trade_price_ = view.last_trade;
    }

    // ── 2. Decide whether to submit an order ──────────────────────────
    const double prob = hawkes_.arrival_prob(cfg_.dt_ns);
    const bool   act  = std::bernoulli_distribution{prob}(rng_);

    // ── 3. Advance Hawkes intensity ────────────────────────────────────
    //  Include self-excitation only if we actually submit
    const int self_events = act ? 1 : 0;
    hawkes_.advance(cfg_.dt_ns, self_events + external_events);

    if (!act) return;
    if (!view.best_bid || !view.best_ask) return;

    const double mid = view.mid
        ? static_cast<double>(*view.mid)
        : static_cast<double>(*view.best_bid + *view.best_ask) / 2.0;

    // ── 4. Choose side (biased by LOB imbalance if view provides it) ──
    //   imbalance ∈ [−1, 1]; positive → bid heavier → slight buy bias
    const double imb = view.imbalance;
    const double p_buy = 0.5 + cfg_.imbalance_bias * imb * 0.5;
    const bool   is_buy = std::bernoulli_distribution{p_buy}(rng_);
    const Side   side   = is_buy ? Side::Buy : Side::Sell;

    // ── 5. Choose order type and build order ──────────────────────────
    const bool is_market = std::bernoulli_distribution{cfg_.p_market}(rng_);

    if (is_market) {
      out.push_back(Action::submit(make_market_order(ts, side)));
    } else {
      const int spread_range = cfg_.max_offset - cfg_.min_offset + 1;
      std::uniform_int_distribution<int> offset_dist{0, spread_range - 1};
      const int offset = cfg_.min_offset + offset_dist(rng_);

      // Limit order placed on the passive side (behind mid) to provide liquidity
      const Price px = is_buy
          ? static_cast<Price>(std::round(mid)) - static_cast<Price>(offset)
          : static_cast<Price>(std::round(mid)) + static_cast<Price>(offset);

      out.push_back(Action::submit(make_limit_order(ts, side, px)));
    }
  }

  // Diagnostics
  double hawkes_intensity()     const noexcept { return hawkes_.intensity(); }
  double hawkes_mean_intensity()const noexcept { return hawkes_.mean_intensity(); }
  int    steps_taken()          const noexcept { return step_count_; }

private:
  Order make_market_order(Ts ts, Side side) const {
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

  Order make_limit_order(Ts ts, Side side, Price px) const {
    Order o{};
    o.id        = (owner_ << 24) | (counter_++ & 0xFF'FFFFull);
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

  OwnerId            owner_;
  HawkesNoiseConfig  cfg_;
  HawkesProcess      hawkes_;
  std::mt19937_64    rng_{};
  std::optional<Price> last_trade_price_{};
  int                step_count_ = 0;
  mutable uint64_t   counter_   = 0;
};

} // namespace msim::agents
