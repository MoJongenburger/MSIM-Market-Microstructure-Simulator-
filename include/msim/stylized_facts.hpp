#pragma once
// =============================================================================
// msim/stylized_facts.hpp
//
// Stylized Facts Measurement Module
//
// Overview:
//   This module computes the canonical statistical properties of simulated
//   market data that are used to validate whether a LOB simulator reproduces
//   the empirical regularities ("stylized facts") documented in real financial
//   markets.  Failing to exhibit these properties indicates that the simulation
//   is not a credible model of real microstructure.
//
// Facts measured (following Cont 2001, Bouchaud et al. 2018):
//
//   1. Return distribution:
//      - Mean, variance, skewness, excess kurtosis
//      - Fat tails: kurtosis >> 3 in real markets (leptokurtic distribution)
//
//   2. Autocorrelation structure:
//      - Return autocorrelation AC(r_t, r_{t-k}) — near zero at lag > 1 min
//      - Absolute-return autocorrelation AC(|r_t|, |r_{t-k}|) — slowly decaying
//        (proxy for volatility clustering; long-memory property)
//      - Order-flow sign autocorrelation AC(sgn(x_t), sgn(x_{t-k}))
//        — strongly positive at short lags (empirically ≈ 0.3-0.6 at lag 1)
//
//   3. Price impact (Almgren-Chriss / Kyle):
//      - Empirical impact function: E[Δp | x] vs x (signed order size)
//      - Fit a power-law: E[Δp | x] = λ · x^δ  (δ ≈ 0.5 empirically)
//      - Estimate Kyle's lambda λ (linear approximation)
//
//   4. Spread and liquidity:
//      - Time-weighted average bid-ask spread
//      - Realized spread: 2·(trade_price - mid_5s_later) for buys
//      - Adverse selection (price impact) component of spread
//      - Amihud (2002) illiquidity ratio: ILLIQ = |r_t| / volume_t
//
//   5. Trade sign autocorrelation:
//      - Proportion of consecutive same-direction trades (run length)
//
// =============================================================================

#include <vector>
#include <cstdint>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <cassert>
#include <string>
#include <sstream>
#include <optional>
#include "types.hpp"

namespace msim {

// ─── Input data structures ────────────────────────────────────────────────────

struct TradeRecord {
    Ts    ts;            ///< Execution timestamp (ns)
    Price price;         ///< Execution price (ticks)
    Qty   qty;           ///< Executed quantity
    Side  aggressor;     ///< BUY = buyer-initiated, SELL = seller-initiated
    Price mid_at_trade;  ///< Mid-price at time of trade
};

struct TopRecord {
    Ts    ts;
    Price bid;
    Price ask;
    Price mid;
    Qty   bid_qty;
    Qty   ask_qty;
};

// ─── Result structures ────────────────────────────────────────────────────────

struct ReturnStats {
    double mean;
    double variance;
    double std_dev;
    double skewness;
    double excess_kurtosis;   ///< kurtosis - 3; > 0 = fat tails
    double min_return;
    double max_return;
    int    n_obs;
};

struct AutocorrResult {
    std::vector<double> return_ac;       ///< AC of raw returns at lags 1..max_lag
    std::vector<double> abs_return_ac;   ///< AC of |returns| — volatility clustering
    std::vector<double> sign_flow_ac;    ///< AC of trade-sign series
    int                 max_lag;
};

struct PriceImpactResult {
    double kyle_lambda;       ///< Linear price impact coefficient (OLS slope)
    double r_squared;         ///< OLS R² of linear fit
    double power_exponent;    ///< δ in Δp = λ·|x|^δ·sgn(x) (log-log OLS)

    // Binned impact curve: E[Δp | x ∈ bin_k]
    std::vector<double> bin_midpoints;
    std::vector<double> bin_impact;
};

struct SpreadStats {
    double time_weighted_spread;     ///< Average (ask-bid) weighted by duration
    double realized_spread_mean;     ///< Mean realized spread (ticks)
    double adverse_selection_mean;   ///< Mean adverse selection component
    double effective_spread_mean;    ///< 2·|trade_price - mid| at trade time
};

struct AmihudStats {
    double mean_illiq;    ///< Mean |r_t| / volume_t  (Amihud ratio)
    double std_illiq;
    std::vector<double> illiq_series;   ///< Per-trade ILLIQ values
};

struct StyleFacts {
    ReturnStats     returns;
    AutocorrResult  autocorr;
    PriceImpactResult impact;
    SpreadStats     spreads;
    AmihudStats     amihud;

    // Validation flags — true if the statistic is consistent with
    // real market stylized facts
    bool fat_tails_ok;          ///< excess_kurtosis > 1.0
    bool vol_clustering_ok;     ///< abs_return_ac[0] > 0.05
    bool flow_autocorr_ok;      ///< sign_flow_ac[0] > 0.10
    bool positive_spread_ok;    ///< time_weighted_spread > 0
    bool positive_impact_ok;    ///< kyle_lambda > 0
};

// ─── Measurement engine ───────────────────────────────────────────────────────

class StylizedFactsMeasurer {
public:
    explicit StylizedFactsMeasurer(int max_lag = 20, int n_impact_bins = 10)
        : max_lag_(max_lag), n_bins_(n_impact_bins) {}

    // ── Ingest data ──────────────────────────────────────────────────────────
    void add_trade(const TradeRecord& t) { trades_.push_back(t); }
    void add_top(const TopRecord& top)   { tops_.push_back(top); }

    void clear() { trades_.clear(); tops_.clear(); }

    size_t n_trades() const { return trades_.size(); }
    size_t n_tops()   const { return tops_.size();   }

    // ── Compute all stylized facts ───────────────────────────────────────────
    StyleFacts compute() const {
        StyleFacts sf;
        sf.returns  = compute_return_stats();
        sf.autocorr = compute_autocorr();
        sf.impact   = compute_price_impact();
        sf.spreads  = compute_spread_stats();
        sf.amihud   = compute_amihud();

        // Validation against empirical thresholds from Cont (2001)
        sf.fat_tails_ok       = sf.returns.excess_kurtosis > 1.0;
        sf.vol_clustering_ok  = !sf.autocorr.abs_return_ac.empty()
                                && sf.autocorr.abs_return_ac[0] > 0.05;
        sf.flow_autocorr_ok   = !sf.autocorr.sign_flow_ac.empty()
                                && sf.autocorr.sign_flow_ac[0] > 0.10;
        sf.positive_spread_ok = sf.spreads.time_weighted_spread > 0.0;
        sf.positive_impact_ok = sf.impact.kyle_lambda > 0.0;
        return sf;
    }

    // ── Human-readable summary ───────────────────────────────────────────────
    static std::string summary(const StyleFacts& sf) {
        std::ostringstream os;
        os << "=== MSIM Stylized Facts Report ===\n\n";
        os << "Return Distribution (n=" << sf.returns.n_obs << "):\n";
        os << "  Mean:            " << sf.returns.mean         << " ticks\n";
        os << "  Std dev:         " << sf.returns.std_dev      << " ticks\n";
        os << "  Skewness:        " << sf.returns.skewness     << "\n";
        os << "  Excess kurtosis: " << sf.returns.excess_kurtosis
           << (sf.fat_tails_ok ? "  [OK — fat tails]" : "  [WARN — no fat tails]") << "\n\n";

        os << "Autocorrelation (max_lag=" << sf.autocorr.max_lag << "):\n";
        if (!sf.autocorr.return_ac.empty()) {
            os << "  Return AC lag-1:        " << sf.autocorr.return_ac[0]      << "\n";
            os << "  |Return| AC lag-1:      " << sf.autocorr.abs_return_ac[0]
               << (sf.vol_clustering_ok ? "  [OK — vol clustering]" : "  [WARN]") << "\n";
            os << "  Trade-sign AC lag-1:    " << sf.autocorr.sign_flow_ac[0]
               << (sf.flow_autocorr_ok ? "  [OK — flow autocorr]" : "  [WARN]") << "\n\n";
        }

        os << "Price Impact:\n";
        os << "  Kyle's lambda:   " << sf.impact.kyle_lambda    << " ticks/lot\n";
        os << "  R²:              " << sf.impact.r_squared      << "\n";
        os << "  Power exponent:  " << sf.impact.power_exponent << " (theory: ~0.5)\n\n";

        os << "Spread & Liquidity:\n";
        os << "  Time-wt spread:  " << sf.spreads.time_weighted_spread   << " ticks\n";
        os << "  Effective spread:" << sf.spreads.effective_spread_mean   << " ticks\n";
        os << "  Realized spread: " << sf.spreads.realized_spread_mean    << " ticks\n";
        os << "  Adverse select.: " << sf.spreads.adverse_selection_mean  << " ticks\n\n";

        os << "Amihud Illiquidity:\n";
        os << "  Mean ILLIQ:      " << sf.amihud.mean_illiq   << "\n";
        os << "  Std  ILLIQ:      " << sf.amihud.std_illiq    << "\n\n";

        os << "Validation:\n";
        os << "  Fat tails:          " << (sf.fat_tails_ok       ? "PASS" : "FAIL") << "\n";
        os << "  Vol clustering:     " << (sf.vol_clustering_ok  ? "PASS" : "FAIL") << "\n";
        os << "  Flow autocorr:      " << (sf.flow_autocorr_ok   ? "PASS" : "FAIL") << "\n";
        os << "  Positive spread:    " << (sf.positive_spread_ok ? "PASS" : "FAIL") << "\n";
        os << "  Positive impact:    " << (sf.positive_impact_ok ? "PASS" : "FAIL") << "\n";
        return os.str();
    }

    // ── CSV export ────────────────────────────────────────────────────────────
    static std::string to_csv_header() {
        return "excess_kurtosis,return_ac_lag1,abs_return_ac_lag1,"
               "sign_flow_ac_lag1,kyle_lambda,r_squared,power_exp,"
               "tw_spread,eff_spread,realized_spread,adv_selection,"
               "amihud_mean,fat_tails,vol_clustering,flow_ac\n";
    }

    static std::string to_csv_row(const StyleFacts& sf) {
        std::ostringstream os;
        auto ac1 = [&](const std::vector<double>& v) {
            return v.empty() ? 0.0 : v[0];
        };
        os << sf.returns.excess_kurtosis        << ","
           << ac1(sf.autocorr.return_ac)        << ","
           << ac1(sf.autocorr.abs_return_ac)    << ","
           << ac1(sf.autocorr.sign_flow_ac)     << ","
           << sf.impact.kyle_lambda             << ","
           << sf.impact.r_squared               << ","
           << sf.impact.power_exponent          << ","
           << sf.spreads.time_weighted_spread   << ","
           << sf.spreads.effective_spread_mean  << ","
           << sf.spreads.realized_spread_mean   << ","
           << sf.spreads.adverse_selection_mean << ","
           << sf.amihud.mean_illiq              << ","
           << sf.fat_tails_ok                   << ","
           << sf.vol_clustering_ok              << ","
           << sf.flow_autocorr_ok               << "\n";
        return os.str();
    }

private:
    int    max_lag_;
    int    n_bins_;
    std::vector<TradeRecord> trades_;
    std::vector<TopRecord>   tops_;

    // ── Return series from trade prices ──────────────────────────────────────
    std::vector<double> log_returns() const {
        std::vector<double> ret;
        if (trades_.size() < 2) return ret;
        ret.reserve(trades_.size() - 1);
        for (size_t i = 1; i < trades_.size(); ++i) {
            if (trades_[i-1].price <= 0) continue;
            ret.push_back(std::log(static_cast<double>(trades_[i].price)
                                 / static_cast<double>(trades_[i-1].price)));
        }
        return ret;
    }

    // ── Sample autocorrelation at lag k ──────────────────────────────────────
    // AC(k) = Σ(x_t - μ)(x_{t-k} - μ) / Σ(x_t - μ)²
    static double autocorr_at_lag(const std::vector<double>& x, int lag) {
        if (static_cast<int>(x.size()) <= lag) return 0.0;
        const int n = static_cast<int>(x.size());
        const double mu = std::accumulate(x.begin(), x.end(), 0.0) / n;
        double num = 0.0, denom = 0.0;
        for (int t = lag; t < n; ++t) {
            num   += (x[t] - mu) * (x[t - lag] - mu);
            denom += (x[t] - mu) * (x[t] - mu);
        }
        return denom < 1e-14 ? 0.0 : num / denom;
    }

    // ── Central moments ───────────────────────────────────────────────────────
    static void moments(const std::vector<double>& x,
                        double& mean, double& var,
                        double& skew, double& kurt) {
        const int n = static_cast<int>(x.size());
        if (n < 4) { mean = var = skew = kurt = 0.0; return; }
        mean = std::accumulate(x.begin(), x.end(), 0.0) / n;
        double m2 = 0, m3 = 0, m4 = 0;
        for (double v : x) {
            double d = v - mean;
            m2 += d*d; m3 += d*d*d; m4 += d*d*d*d;
        }
        m2 /= n; m3 /= n; m4 /= n;
        var  = m2;
        skew = (m2 < 1e-14) ? 0.0 : m3 / std::pow(m2, 1.5);
        kurt = (m2 < 1e-14) ? 0.0 : m4 / (m2 * m2) - 3.0; // excess kurtosis
    }

    // ── OLS regression y = a + b·x, returns {a, b, r²} ──────────────────────
    static std::tuple<double,double,double> ols(const std::vector<double>& x,
                                                 const std::vector<double>& y) {
        assert(x.size() == y.size());
        const int n = static_cast<int>(x.size());
        if (n < 2) return {0.0, 0.0, 0.0};
        double sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0;
        for (int i = 0; i < n; ++i) {
            sx += x[i]; sy += y[i]; sxx += x[i]*x[i];
            sxy += x[i]*y[i]; syy += y[i]*y[i];
        }
        const double b = (n*sxy - sx*sy) / (n*sxx - sx*sx + 1e-14);
        const double a = (sy - b*sx) / n;
        const double ss_res = syy - 2*b*sxy - 2*a*sy + b*b*sxx + 2*a*b*sx + n*a*a;
        const double ss_tot = syy - sy*sy/n;
        const double r2 = (ss_tot < 1e-14) ? 0.0 : 1.0 - ss_res/ss_tot;
        return {a, b, std::max(0.0, r2)};
    }

    // ── Compute return statistics ─────────────────────────────────────────────
    ReturnStats compute_return_stats() const {
        ReturnStats rs{};
        const auto ret = log_returns();
        if (ret.empty()) return rs;
        rs.n_obs = static_cast<int>(ret.size());
        rs.min_return = *std::min_element(ret.begin(), ret.end());
        rs.max_return = *std::max_element(ret.begin(), ret.end());
        double var, skew, kurt;
        moments(ret, rs.mean, var, skew, kurt);
        rs.variance         = var;
        rs.std_dev          = std::sqrt(var);
        rs.skewness         = skew;
        rs.excess_kurtosis  = kurt;
        return rs;
    }

    // ── Compute autocorrelations ──────────────────────────────────────────────
    AutocorrResult compute_autocorr() const {
        AutocorrResult ac;
        ac.max_lag = max_lag_;

        const auto ret = log_returns();
        // Absolute returns — volatility clustering proxy
        std::vector<double> abs_ret(ret.size());
        std::transform(ret.begin(), ret.end(), abs_ret.begin(),
                       [](double r){ return std::abs(r); });

        // Trade-sign series: +1 = buy-initiated, -1 = sell-initiated
        std::vector<double> signs;
        signs.reserve(trades_.size());
        for (const auto& t : trades_)
            signs.push_back(t.aggressor == Side::BUY ? 1.0 : -1.0);

        for (int lag = 1; lag <= max_lag_; ++lag) {
            ac.return_ac.push_back(autocorr_at_lag(ret, lag));
            ac.abs_return_ac.push_back(autocorr_at_lag(abs_ret, lag));
            ac.sign_flow_ac.push_back(autocorr_at_lag(signs, lag));
        }
        return ac;
    }

    // ── Compute price impact ──────────────────────────────────────────────────
    PriceImpactResult compute_price_impact() const {
        PriceImpactResult pi{};
        if (trades_.size() < 10) return pi;

        // Build (signed_volume, price_change) pairs
        // Δp = mid_after_trade - mid_at_trade; x = signed qty
        std::vector<double> xs, ys;
        for (size_t i = 0; i + 1 < trades_.size(); ++i) {
            const double dp = static_cast<double>(
                trades_[i+1].mid_at_trade - trades_[i].mid_at_trade);
            const double x = (trades_[i].aggressor == Side::BUY ? 1.0 : -1.0)
                           * static_cast<double>(trades_[i].qty);
            xs.push_back(x);
            ys.push_back(dp);
        }

        // Linear OLS: Δp = λ·x → Kyle's lambda
        {
            auto [a, b, r2] = ols(xs, ys);
            pi.kyle_lambda = b;
            pi.r_squared   = r2;
        }

        // Log-log OLS: log|Δp| = δ·log|x| + const → power exponent
        {
            std::vector<double> lx, ly;
            for (size_t i = 0; i < xs.size(); ++i) {
                if (std::abs(xs[i]) > 0 && std::abs(ys[i]) > 0) {
                    lx.push_back(std::log(std::abs(xs[i])));
                    ly.push_back(std::log(std::abs(ys[i])));
                }
            }
            if (lx.size() >= 5) {
                auto [a, b, r2] = ols(lx, ly);
                pi.power_exponent = b;   // δ ≈ 0.5 empirically
            }
        }

        // Binned impact curve
        if (!xs.empty()) {
            double xmin = *std::min_element(xs.begin(), xs.end());
            double xmax = *std::max_element(xs.begin(), xs.end());
            double bw   = (xmax - xmin) / n_bins_;
            if (bw > 0) {
                std::vector<double> bin_sum(n_bins_, 0.0);
                std::vector<int>    bin_cnt(n_bins_, 0);
                for (size_t i = 0; i < xs.size(); ++i) {
                    int b = std::min(n_bins_-1, static_cast<int>((xs[i]-xmin)/bw));
                    bin_sum[b] += ys[i]; bin_cnt[b]++;
                }
                for (int b = 0; b < n_bins_; ++b) {
                    pi.bin_midpoints.push_back(xmin + (b + 0.5) * bw);
                    pi.bin_impact.push_back(bin_cnt[b] > 0 ? bin_sum[b]/bin_cnt[b] : 0.0);
                }
            }
        }
        return pi;
    }

    // ── Spread statistics ─────────────────────────────────────────────────────
    SpreadStats compute_spread_stats() const {
        SpreadStats ss{};
        if (tops_.size() < 2) return ss;

        // Time-weighted spread
        double total_spread_ns = 0.0;
        double total_ns = 0.0;
        for (size_t i = 1; i < tops_.size(); ++i) {
            const double dur = static_cast<double>(tops_[i].ts - tops_[i-1].ts);
            if (dur <= 0) continue;
            total_spread_ns += (tops_[i-1].ask - tops_[i-1].bid) * dur;
            total_ns += dur;
        }
        ss.time_weighted_spread = total_ns > 0 ? total_spread_ns / total_ns : 0.0;

        // Effective and realized spread from trades
        if (trades_.empty()) return ss;
        double eff_sum = 0.0, real_sum = 0.0, adv_sum = 0.0;
        int    count = 0;
        for (const auto& t : trades_) {
            if (t.mid_at_trade <= 0) continue;
            const double mid = static_cast<double>(t.mid_at_trade);
            const double p   = static_cast<double>(t.price);
            const double dir = (t.aggressor == Side::BUY) ? 1.0 : -1.0;
            eff_sum += 2.0 * dir * (p - mid);
            ++count;
        }
        ss.effective_spread_mean = count > 0 ? eff_sum / count : 0.0;
        // Realized spread: trade_price vs mid 5 trades later (proxy for 5s)
        for (size_t i = 0; i + 5 < trades_.size(); ++i) {
            const double mid_later = static_cast<double>(trades_[i+5].mid_at_trade);
            const double p   = static_cast<double>(trades_[i].price);
            const double dir = (trades_[i].aggressor == Side::BUY) ? 1.0 : -1.0;
            real_sum += 2.0 * dir * (p - mid_later);
            adv_sum  += 2.0 * dir * (mid_later - static_cast<double>(trades_[i].mid_at_trade));
        }
        const int n_real = std::max(1, static_cast<int>(trades_.size()) - 5);
        ss.realized_spread_mean    = real_sum / n_real;
        ss.adverse_selection_mean  = adv_sum  / n_real;
        return ss;
    }

    // ── Amihud illiquidity ────────────────────────────────────────────────────
    AmihudStats compute_amihud() const {
        AmihudStats as{};
        if (trades_.size() < 2) return as;
        const auto ret = log_returns();
        for (size_t i = 0; i < ret.size() && i < trades_.size(); ++i) {
            const double vol = static_cast<double>(trades_[i].qty);
            if (vol > 0)
                as.illiq_series.push_back(std::abs(ret[i]) / vol);
        }
        if (as.illiq_series.empty()) return as;
        as.mean_illiq = std::accumulate(as.illiq_series.begin(),
                                        as.illiq_series.end(), 0.0)
                       / as.illiq_series.size();
        double var = 0.0;
        for (double v : as.illiq_series) var += (v - as.mean_illiq) * (v - as.mean_illiq);
        as.std_illiq = std::sqrt(var / as.illiq_series.size());
        return as;
    }
};

} // namespace msim
