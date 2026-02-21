#pragma once
// =============================================================================
// msim/agents/momentum_agent.hpp
//
// Momentum Agent — trend-following trader that submits orders in the direction
// of recent price movement, modelling positive-feedback order flow.
//
// Theory (Bouchaud et al. 2009 — order flow autocorrelation):
//   Empirical studies of LOBs show strong positive autocorrelation in the
//   sign of order flow at short horizons (seconds to minutes), driven partly
//   by trend-following participants.  This agent models that behaviour:
//
//   Signal: s_t = EMA_fast(mid, α_f) − EMA_slow(mid, α_s)
//
//   where EMA_fast decays faster (shorter lookback) and EMA_slow decays slower.
//   This is a discrete-time MACD (Moving Average Convergence Divergence) signal
//   which is standard in systematic trading literature.
//
//   Trading rule:
//     s_t > +entry_band  → submit market buy  (uptrend detected)
//     s_t < −entry_band  → submit market sell (downtrend detected)
//     |s_t| < exit_band  → close existing position with market order
//     otherwise          → hold
//
// Position tracking:
//   The agent tracks net inventory and will not add to a position beyond
//   max_position lots (risk limit).  It uses market orders (IOC) for speed,
//   consistent with empirical evidence that momentum traders prefer immediacy.
// =============================================================================

#include <cstdint>
#include <optional>
#include <deque>
#include <cmath>
#include "../types.hpp"

namespace msim::agents {

struct MomentumConfig {
    // EMA parameters (α = 2/(N+1) for an N-period EMA)
    double alpha_fast    = 2.0 / (5.0  + 1.0);  ///< Fast EMA decay (≈ 5-step)
    double alpha_slow    = 2.0 / (20.0 + 1.0);  ///< Slow EMA decay (≈ 20-step)

    // Signal thresholds (in ticks)
    double entry_band    = 0.5;   ///< Signal magnitude to open a position
    double exit_band     = 0.1;   ///< Signal magnitude to close a position

    // Risk limits
    Qty    lot_size      = 1;     ///< Order size per signal
    Qty    max_position  = 10;    ///< Maximum net inventory (longs or shorts)

    // Minimum steps before trading (EMA warm-up period)
    int    warmup_steps  = 20;
};

class MomentumAgent {
public:
    explicit MomentumAgent(uint64_t /*seed*/, MomentumConfig cfg = {})
        : cfg_(cfg)
        , ema_fast_(0.0), ema_slow_(0.0)
        , step_(0)
        , position_(0)
        , initialised_(false)
    {}

    struct Order {
        Side        side;
        OrderType   type  = OrderType::MARKET;
        Price       price = 0;
        Qty         qty;
        TimeInForce tif   = TimeInForce::IOC;
    };

    template <typename MarketView>
    std::optional<Order> act(const MarketView& view, Ts /*ts*/) {
        if (!view.has_quote()) return std::nullopt;

        const double mid = static_cast<double>(view.mid);

        // ── Initialise EMAs on first observation ──────────────────────────────
        if (!initialised_) {
            ema_fast_ = mid;
            ema_slow_ = mid;
            initialised_ = true;
        }

        // ── Update exponential moving averages ────────────────────────────────
        // EMA_t = α·mid_t + (1−α)·EMA_{t−1}
        ema_fast_ = cfg_.alpha_fast * mid + (1.0 - cfg_.alpha_fast) * ema_fast_;
        ema_slow_ = cfg_.alpha_slow * mid + (1.0 - cfg_.alpha_slow) * ema_slow_;
        ++step_;

        if (step_ < cfg_.warmup_steps) return std::nullopt;  // warm-up period

        // ── Compute MACD signal ───────────────────────────────────────────────
        const double signal = ema_fast_ - ema_slow_;

        // ── Position exit (flatten) ───────────────────────────────────────────
        if (std::abs(signal) < cfg_.exit_band && position_ != 0) {
            const Side   exit_side = (position_ > 0) ? Side::SELL : Side::BUY;
            const Qty    exit_qty  = std::abs(position_);
            position_ = 0;
            return Order{exit_side, OrderType::MARKET, 0, exit_qty, TimeInForce::IOC};
        }

        // ── Position entry ────────────────────────────────────────────────────
        if (signal > cfg_.entry_band && position_ < cfg_.max_position) {
            const Qty add = std::min(cfg_.lot_size, cfg_.max_position - position_);
            if (add <= 0) return std::nullopt;
            position_ += add;
            return Order{Side::BUY, OrderType::MARKET, 0, add, TimeInForce::IOC};
        }
        if (signal < -cfg_.entry_band && position_ > -cfg_.max_position) {
            const Qty add = std::min(cfg_.lot_size, cfg_.max_position + position_);
            if (add <= 0) return std::nullopt;
            position_ -= add;
            return Order{Side::SELL, OrderType::MARKET, 0, add, TimeInForce::IOC};
        }

        return std::nullopt;
    }

    // Diagnostics
    double signal()   const { return ema_fast_ - ema_slow_; }
    Qty    position() const { return position_; }
    double ema_fast() const { return ema_fast_; }
    double ema_slow() const { return ema_slow_; }

    void reset() {
        ema_fast_ = ema_slow_ = 0.0;
        step_ = 0; position_ = 0; initialised_ = false;
    }

private:
    MomentumConfig cfg_;
    double  ema_fast_, ema_slow_;
    int     step_;
    Qty     position_;
    bool    initialised_;
};

} // namespace msim::agents
