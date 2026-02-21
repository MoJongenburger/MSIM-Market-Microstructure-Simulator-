#pragma once
// =============================================================================
// msim/agents/fundamental_value_agent.hpp
//
// Fundamental Value Agent — informed trader whose private value signal follows
// a discrete-time Ornstein-Uhlenbeck (mean-reverting) process.
//
// Theory (Glosten-Milgrom 1985):
//   An informed trader observes a private estimate V_t of the asset's true value
//   and submits orders when the market price deviates from V_t by more than a
//   configurable threshold.  Over time, trading by informed agents causes the
//   market price to converge towards the true fundamental value, generating the
//   adverse-selection component of the bid-ask spread.
//
// Discrete OU process (Euler-Maruyama discretisation):
//   V_{t+1} = V_t + κ(μ - V_t)·Δt + σ_v·√Δt·Z,   Z ~ N(0,1)
//
//   κ  = mean-reversion speed  (default 0.01 per step)
//   μ  = long-run fundamental  (default = initial mid-price)
//   σ_v = fundamental volatility (default 1 tick per √step)
//
// Trading rule:
//   if  V_t - ask > threshold   → submit market buy  (asset is underpriced)
//   if  bid - V_t > threshold   → submit market sell (asset is overpriced)
//   else                        → null action (wait)
//
// The agent sizes its order as a fixed lot_size or proportionally to |V_t - mid|.
// =============================================================================

#include <cstdint>
#include <optional>
#include <random>
#include <cmath>
#include "../types.hpp"   // Price, Qty, Ts, Side, TimeInForce, OrderId

namespace msim::agents {

struct FundamentalValueConfig {
    // OU process parameters
    double kappa      = 0.01;   ///< Mean-reversion speed per simulation step
    double sigma_v    = 1.0;    ///< Fundamental value volatility (ticks/√step)
    Price  mu         = 0;      ///< Long-run mean; 0 = use initial mid-price

    // Trading rule
    double threshold  = 1.0;    ///< Minimum |V_t - price| in ticks to trade
    Qty    lot_size   = 1;      ///< Fixed order size in lots (0 = proportional)
    Qty    max_qty    = 100;    ///< Maximum order size when proportional sizing

    // Order parameters
    TimeInForce tif   = TimeInForce::IOC; ///< IOC: execute or cancel; informed traders
                                           ///< do not post resting orders
};

class FundamentalValueAgent {
public:
    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------
    explicit FundamentalValueAgent(uint64_t seed,
                                   FundamentalValueConfig cfg = {})
        : cfg_(cfg)
        , rng_(seed)
        , normal_(0.0, 1.0)
        , initialised_(false)
        , V_(0.0)
        , next_order_id_(1)
    {}

    // -------------------------------------------------------------------------
    // act() — called by World at each simulation step.
    //
    // MarketView must expose (at minimum):
    //   Price best_bid, best_ask, mid;   (integer ticks)
    //   bool  has_quote() const;         (false if book is empty on either side)
    //
    // Returns std::nullopt (no action) or an Order to submit.
    // -------------------------------------------------------------------------
    struct Order {
        Side        side;
        OrderType   type  = OrderType::MARKET;
        Price       price = 0;          ///< ignored for market orders
        Qty         qty;
        TimeInForce tif;
    };

    template <typename MarketView>
    std::optional<Order> act(const MarketView& view, Ts /*ts*/) {
        // ── Initialise fundamental value to first observed mid-price ──────────
        if (!initialised_) {
            if (!view.has_quote()) return std::nullopt;
            V_ = (cfg_.mu != 0)
                     ? static_cast<double>(cfg_.mu)
                     : static_cast<double>(view.mid);
            mu_ = V_;
            initialised_ = true;
        }

        // ── Step the OU process ───────────────────────────────────────────────
        // V_{t+1} = V_t + κ(μ - V_t) + σ_v·Z,   Z ~ N(0,1)
        // (Δt = 1 per step; absorbed into κ and σ_v configuration)
        const double Z = normal_(rng_);
        V_ = V_ + cfg_.kappa * (mu_ - V_) + cfg_.sigma_v * Z;

        if (!view.has_quote()) return std::nullopt;

        const double ask = static_cast<double>(view.best_ask);
        const double bid = static_cast<double>(view.best_bid);

        // ── Trading rule ──────────────────────────────────────────────────────
        const double buy_signal  = V_ - ask;   // positive → asset underpriced
        const double sell_signal = bid - V_;   // positive → asset overpriced

        if (buy_signal > cfg_.threshold) {
            return Order{Side::BUY, OrderType::MARKET, 0, compute_qty(buy_signal), cfg_.tif};
        }
        if (sell_signal > cfg_.threshold) {
            return Order{Side::SELL, OrderType::MARKET, 0, compute_qty(sell_signal), cfg_.tif};
        }
        return std::nullopt;
    }

    // Expose the current private value signal (for logging / research output)
    double fundamental_value() const { return V_; }

    // Reset agent state (for re-use across episodes)
    void reset(Price new_mu = 0) {
        initialised_ = false;
        if (new_mu != 0) { cfg_.mu = new_mu; mu_ = new_mu; }
    }

private:
    Qty compute_qty(double signal) const {
        if (cfg_.lot_size > 0) return cfg_.lot_size;
        // Proportional: qty = clamp(round(signal), 1, max_qty)
        const Qty q = static_cast<Qty>(std::round(signal));
        return std::max(Qty{1}, std::min(q, cfg_.max_qty));
    }

    FundamentalValueConfig  cfg_;
    std::mt19937_64         rng_;
    std::normal_distribution<double> normal_;
    bool                    initialised_;
    double                  V_;     ///< Current private fundamental value (ticks)
    double                  mu_;    ///< Effective long-run mean
    uint64_t                next_order_id_;
};

} // namespace msim::agents
