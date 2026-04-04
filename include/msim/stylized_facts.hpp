#pragma once
// ============================================================
// include/msim/stylized_facts.hpp
//
// Stylized Facts Measurement Module.
// Computes the canonical microstructure statistics used to
// validate whether MSIM reproduces empirical market regularities.
//
// Fixes vs previous version:
//   - Side::BUY → Side::Buy  (matches msim::Side enum)
//   - All int loop indices cast to size_t to fix -Werror=sign-conversion
//   - illiq_series.size() cast to double for -Werror=conversion
// ============================================================

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "msim/types.hpp"   // Price, Qty, Ts, Side (Side::Buy / Side::Sell)

namespace msim {

// ─── Input records ────────────────────────────────────────────────────────────
struct TradeRecord {
  Ts    ts{};
  Price price{};
  Qty   qty{};
  Side  aggressor{};      // Side::Buy = buyer-initiated
  Price mid_at_trade{};   // mid-price at time of trade
};

struct TopRecord {
  Ts    ts{};
  Price bid{};
  Price ask{};
  Price mid{};
  Price bid_qty{};   // unused in computations; kept for future use
  Price ask_qty{};
};

// ─── Result structs ───────────────────────────────────────────────────────────
struct ReturnStats {
  double mean{};
  double variance{};
  double std_dev{};
  double skewness{};
  double excess_kurtosis{};   // > 0 = fat tails (leptokurtic)
  double min_return{};
  double max_return{};
  int    n_obs{};
};

struct AutocorrResult {
  std::vector<double> return_ac;       // AC of raw returns at lags 1..max_lag
  std::vector<double> abs_return_ac;   // AC of |returns| — volatility clustering
  std::vector<double> sign_flow_ac;    // AC of trade-sign series
  int                 max_lag{};
};

struct PriceImpactResult {
  double kyle_lambda{};       // linear price impact (OLS slope)
  double r_squared{};         // OLS R²
  double power_exponent{};    // δ in Δp = λ·|x|^δ (log-log OLS; theory ≈ 0.5)
  std::vector<double> bin_midpoints;
  std::vector<double> bin_impact;
};

struct SpreadStats {
  double time_weighted_spread{};
  double realized_spread_mean{};
  double adverse_selection_mean{};
  double effective_spread_mean{};
};

struct AmihudStats {
  double mean_illiq{};
  double std_illiq{};
  std::vector<double> illiq_series;
};

struct StyleFacts {
  ReturnStats       returns;
  AutocorrResult    autocorr;
  PriceImpactResult impact;
  SpreadStats       spreads;
  AmihudStats       amihud;

  // Validation flags (true = consistent with real market stylized facts)
  bool fat_tails_ok{};        // excess_kurtosis > 1.0
  bool vol_clustering_ok{};   // abs_return_ac[0] > 0.05
  bool flow_autocorr_ok{};    // |sign_flow_ac[0]| > 0.10
  bool positive_spread_ok{};  // time_weighted_spread > 0
  bool positive_impact_ok{};  // kyle_lambda > 0 or < 0 (non-zero)
};

// ─── Measurer ─────────────────────────────────────────────────────────────────
class StylizedFactsMeasurer {
public:
  explicit StylizedFactsMeasurer(int max_lag = 20, int n_impact_bins = 10)
    : max_lag_(max_lag), n_bins_(n_impact_bins) {}

  void add_trade(const TradeRecord& t) { trades_.push_back(t); }
  void add_top(const TopRecord& top)   { tops_.push_back(top); }
  void clear() { trades_.clear(); tops_.clear(); }

  // Pre-reserve internal accumulation vectors.
  // Call once after construction with expected n_tops (= n_steps) and
  // n_trades (= est_fills) so neither vector ever reallocates during run().
  void reserve(std::size_t n_tops, std::size_t n_trades) {
    tops_.reserve(n_tops);
    trades_.reserve(n_trades);
  }

  std::size_t n_trades() const noexcept { return trades_.size(); }
  std::size_t n_tops()   const noexcept { return tops_.size();   }

  // Compute all stylized facts from accumulated data.
  StyleFacts compute() const {
    StyleFacts sf;
    sf.returns  = compute_return_stats();
    sf.autocorr = compute_autocorr();
    sf.impact   = compute_price_impact();
    sf.spreads  = compute_spread_stats();
    sf.amihud   = compute_amihud();

    sf.fat_tails_ok      = sf.returns.excess_kurtosis > 1.0;
    sf.vol_clustering_ok = !sf.autocorr.abs_return_ac.empty()
                           && sf.autocorr.abs_return_ac[0] > 0.05;
    sf.flow_autocorr_ok  = !sf.autocorr.sign_flow_ac.empty()
                           && std::abs(sf.autocorr.sign_flow_ac[0]) > 0.10;
    sf.positive_spread_ok = sf.spreads.time_weighted_spread > 0.0;
    sf.positive_impact_ok = std::abs(sf.impact.kyle_lambda) > 1e-12;
    return sf;
  }

  // Human-readable report.
  static std::string summary(const StyleFacts& sf) {
    std::ostringstream os;
    os << "=== MSIM Stylized Facts Report ===\n\n";
    os << "Return Distribution (n=" << sf.returns.n_obs << "):\n";
    os << "  Mean:            " << sf.returns.mean            << " ticks\n";
    os << "  Std dev:         " << sf.returns.std_dev         << " ticks\n";
    os << "  Skewness:        " << sf.returns.skewness        << "\n";
    os << "  Excess kurtosis: " << sf.returns.excess_kurtosis
       << (sf.fat_tails_ok ? "  [OK — fat tails]" : "  [WARN — no fat tails]") << "\n\n";

    if (!sf.autocorr.return_ac.empty()) {
      os << "Autocorrelation (max_lag=" << sf.autocorr.max_lag << "):\n";
      os << "  Return AC lag-1:     " << sf.autocorr.return_ac[0]     << "\n";
      os << "  |Return| AC lag-1:   " << sf.autocorr.abs_return_ac[0]
         << (sf.vol_clustering_ok ? "  [OK — vol clustering]" : "  [WARN]") << "\n";
      os << "  Trade-sign AC lag-1: " << sf.autocorr.sign_flow_ac[0]
         << (sf.flow_autocorr_ok  ? "  [OK — flow autocorr]"  : "  [WARN]") << "\n\n";
    }

    os << "Price Impact:\n";
    os << "  Kyle's lambda:   " << sf.impact.kyle_lambda    << " ticks/lot\n";
    os << "  RÂ²:              " << sf.impact.r_squared      << "\n";
    os << "  Power exponent:  " << sf.impact.power_exponent << "  (theory ~0.5)\n\n";

    os << "Spread & Liquidity:\n";
    os << "  Time-wt spread:  " << sf.spreads.time_weighted_spread   << " ticks\n";
    os << "  Effective spread:" << sf.spreads.effective_spread_mean   << " ticks\n";
    os << "  Realized spread: " << sf.spreads.realized_spread_mean    << " ticks\n";
    os << "  Adverse select.: " << sf.spreads.adverse_selection_mean  << " ticks\n\n";

    os << "Amihud Illiquidity:\n";
    os << "  Mean ILLIQ:      " << sf.amihud.mean_illiq << "\n";
    os << "  Std  ILLIQ:      " << sf.amihud.std_illiq  << "\n\n";

    os << "Validation:\n";
    os << "  Fat tails:       " << (sf.fat_tails_ok       ? "PASS" : "FAIL") << "\n";
    os << "  Vol clustering:  " << (sf.vol_clustering_ok  ? "PASS" : "FAIL") << "\n";
    os << "  Flow autocorr:   " << (sf.flow_autocorr_ok   ? "PASS" : "FAIL") << "\n";
    os << "  Positive spread: " << (sf.positive_spread_ok ? "PASS" : "FAIL") << "\n";
    os << "  Nonzero impact:  " << (sf.positive_impact_ok ? "PASS" : "FAIL") << "\n";
    return os.str();
  }

  static std::string to_csv_header() {
    return "excess_kurtosis,return_ac_lag1,abs_return_ac_lag1,"
           "sign_flow_ac_lag1,kyle_lambda,r_squared,power_exp,"
           "tw_spread,eff_spread,realized_spread,adv_selection,"
           "amihud_mean,fat_tails,vol_clustering,flow_ac\n";
  }

  static std::string to_csv_row(const StyleFacts& sf) {
    auto ac1 = [](const std::vector<double>& v) {
      return v.empty() ? 0.0 : v[0];
    };
    std::ostringstream os;
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

  // Log-returns from trade price series
  std::vector<double> log_returns() const {
    std::vector<double> ret;
    if (trades_.size() < 2) return ret;
    ret.reserve(trades_.size() - 1);
    for (std::size_t i = 1; i < trades_.size(); ++i) {
      if (trades_[i-1].price <= 0) continue;
      ret.push_back(std::log(
          static_cast<double>(trades_[i].price) /
          static_cast<double>(trades_[i-1].price)));
    }
    return ret;
  }

  // Sample autocorrelation at lag k
  static double autocorr_at_lag(const std::vector<double>& x, int lag) {
    if (static_cast<int>(x.size()) <= lag) return 0.0;
    const std::size_t n   = x.size();
    const std::size_t lag_u = static_cast<std::size_t>(lag);
    const double mu = std::accumulate(x.begin(), x.end(), 0.0)
                      / static_cast<double>(n);
    double num = 0.0, denom = 0.0;
    for (std::size_t t = lag_u; t < n; ++t) {
      const double dt     = x[t]         - mu;
      const double dt_lag = x[t - lag_u] - mu;
      num   += dt * dt_lag;
      denom += dt * dt;
    }
    return denom < 1e-14 ? 0.0 : num / denom;
  }

  static void moments(const std::vector<double>& x,
                      double& mean, double& var,
                      double& skew, double& kurt)
  {
    const std::size_t n = x.size();
    if (n < 4) { mean = var = skew = kurt = 0.0; return; }
    mean = std::accumulate(x.begin(), x.end(), 0.0)
           / static_cast<double>(n);
    double m2 = 0.0, m3 = 0.0, m4 = 0.0;
    for (double v : x) {
      const double d = v - mean;
      m2 += d*d; m3 += d*d*d; m4 += d*d*d*d;
    }
    m2 /= static_cast<double>(n);
    m3 /= static_cast<double>(n);
    m4 /= static_cast<double>(n);
    var  = m2;
    skew = (m2 < 1e-14) ? 0.0 : m3 / std::pow(m2, 1.5);
    kurt = (m2 < 1e-14) ? 0.0 : m4 / (m2 * m2) - 3.0;
  }

  static std::tuple<double,double,double>
  ols(const std::vector<double>& x, const std::vector<double>& y)
  {
    assert(x.size() == y.size());
    const std::size_t n = x.size();
    if (n < 2) return {0.0, 0.0, 0.0};
    double sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0;
    for (std::size_t i = 0; i < n; ++i) {
      sx  += x[i]; sy  += y[i];
      sxx += x[i]*x[i]; sxy += x[i]*y[i]; syy += y[i]*y[i];
    }
    const double dn  = static_cast<double>(n);
    const double b   = (dn*sxy - sx*sy) / (dn*sxx - sx*sx + 1e-14);
    const double a   = (sy - b*sx) / dn;
    const double ss_res = syy - 2.0*b*sxy - 2.0*a*sy
                        + b*b*sxx + 2.0*a*b*sx + dn*a*a;
    const double ss_tot = syy - sy*sy/dn;
    const double r2 = (ss_tot < 1e-14) ? 0.0 : 1.0 - ss_res/ss_tot;
    return {a, b, std::max(0.0, r2)};
  }

  ReturnStats compute_return_stats() const {
    ReturnStats rs{};
    const auto ret = log_returns();
    if (ret.empty()) return rs;
    rs.n_obs = static_cast<int>(ret.size());
    rs.min_return = *std::min_element(ret.begin(), ret.end());
    rs.max_return = *std::max_element(ret.begin(), ret.end());
    double var, skew, kurt;
    moments(ret, rs.mean, var, skew, kurt);
    rs.variance        = var;
    rs.std_dev         = std::sqrt(var);
    rs.skewness        = skew;
    rs.excess_kurtosis = kurt;
    return rs;
  }

  AutocorrResult compute_autocorr() const {
    AutocorrResult ac;
    ac.max_lag = max_lag_;

    const auto ret = log_returns();
    std::vector<double> abs_ret(ret.size());
    std::transform(ret.begin(), ret.end(), abs_ret.begin(),
                   [](double r){ return std::abs(r); });

    std::vector<double> signs;
    signs.reserve(trades_.size());
    for (const auto& t : trades_)
      signs.push_back(t.aggressor == Side::Buy ? 1.0 : -1.0);

    for (int lag = 1; lag <= max_lag_; ++lag) {
      ac.return_ac.push_back(autocorr_at_lag(ret,     lag));
      ac.abs_return_ac.push_back(autocorr_at_lag(abs_ret, lag));
      ac.sign_flow_ac.push_back(autocorr_at_lag(signs,   lag));
    }
    return ac;
  }

  PriceImpactResult compute_price_impact() const {
    PriceImpactResult pi{};
    if (trades_.size() < 10) return pi;

    std::vector<double> xs, ys;
    for (std::size_t i = 0; i + 1 < trades_.size(); ++i) {
      const double dp = static_cast<double>(
          trades_[i+1].mid_at_trade - trades_[i].mid_at_trade);
      const double x = (trades_[i].aggressor == Side::Buy ? 1.0 : -1.0)
                       * static_cast<double>(trades_[i].qty);
      xs.push_back(x);
      ys.push_back(dp);
    }

    {
      auto [a, b, r2] = ols(xs, ys);
      pi.kyle_lambda = b;
      pi.r_squared   = r2;
    }

    {
      std::vector<double> lx, ly;
      for (std::size_t i = 0; i < xs.size(); ++i) {
        if (std::abs(xs[i]) > 0 && std::abs(ys[i]) > 0) {
          lx.push_back(std::log(std::abs(xs[i])));
          ly.push_back(std::log(std::abs(ys[i])));
        }
      }
      if (lx.size() >= 5) {
        auto [a, b, r2] = ols(lx, ly);
        pi.power_exponent = b;
      }
    }

    if (!xs.empty()) {
      const double xmin = *std::min_element(xs.begin(), xs.end());
      const double xmax = *std::max_element(xs.begin(), xs.end());
      const double bw   = (xmax - xmin) / static_cast<double>(n_bins_);
      if (bw > 0) {
        const auto nb = static_cast<std::size_t>(n_bins_);
        std::vector<double> bin_sum(nb, 0.0);
        std::vector<int>    bin_cnt(nb, 0);
        for (std::size_t i = 0; i < xs.size(); ++i) {
          auto b = static_cast<std::size_t>(
              std::min(n_bins_ - 1,
                       static_cast<int>((xs[i] - xmin) / bw)));
          bin_sum[b] += ys[i];
          bin_cnt[b]++;
        }
        for (std::size_t b = 0; b < nb; ++b) {
          pi.bin_midpoints.push_back(
              xmin + (static_cast<double>(b) + 0.5) * bw);
          pi.bin_impact.push_back(
              bin_cnt[b] > 0
                  ? bin_sum[b] / static_cast<double>(bin_cnt[b])
                  : 0.0);
        }
      }
    }
    return pi;
  }

  SpreadStats compute_spread_stats() const {
    SpreadStats ss{};
    if (tops_.size() < 2) return ss;

    double tw_ns = 0.0, total_ns = 0.0;
    for (std::size_t i = 1; i < tops_.size(); ++i) {
      const double dur = static_cast<double>(tops_[i].ts - tops_[i-1].ts);
      if (dur <= 0) continue;
      tw_ns    += static_cast<double>(tops_[i-1].ask - tops_[i-1].bid) * dur;
      total_ns += dur;
    }
    ss.time_weighted_spread = (total_ns > 0) ? tw_ns / total_ns : 0.0;

    if (trades_.empty()) return ss;

    double eff_sum = 0.0;
    int    eff_cnt = 0;
    for (const auto& t : trades_) {
      if (t.mid_at_trade <= 0) continue;
      const double dir = (t.aggressor == Side::Buy) ? 1.0 : -1.0;
      eff_sum += 2.0 * dir * (static_cast<double>(t.price)
                              - static_cast<double>(t.mid_at_trade));
      ++eff_cnt;
    }
    ss.effective_spread_mean = (eff_cnt > 0)
        ? eff_sum / static_cast<double>(eff_cnt) : 0.0;

    double real_sum = 0.0, adv_sum = 0.0;
    const std::size_t n = trades_.size();
    const std::size_t lookahead = 5;
    for (std::size_t i = 0; i + lookahead < n; ++i) {
      const double mid_later = static_cast<double>(
          trades_[i + lookahead].mid_at_trade);
      const double p   = static_cast<double>(trades_[i].price);
      const double mid = static_cast<double>(trades_[i].mid_at_trade);
      const double dir = (trades_[i].aggressor == Side::Buy) ? 1.0 : -1.0;
      real_sum += 2.0 * dir * (p - mid_later);
      adv_sum  += 2.0 * dir * (mid_later - mid);
    }
    const double n_real = static_cast<double>(
        n > lookahead ? n - lookahead : 1);
    ss.realized_spread_mean    = real_sum / n_real;
    ss.adverse_selection_mean  = adv_sum  / n_real;
    return ss;
  }

  AmihudStats compute_amihud() const {
    AmihudStats as{};
    if (trades_.size() < 2) return as;
    const auto ret = log_returns();
    for (std::size_t i = 0; i < ret.size() && i < trades_.size(); ++i) {
      const double vol = static_cast<double>(trades_[i].qty);
      if (vol > 0.0)
        as.illiq_series.push_back(std::abs(ret[i]) / vol);
    }
    if (as.illiq_series.empty()) return as;

    const double sz = static_cast<double>(as.illiq_series.size());
    as.mean_illiq = std::accumulate(as.illiq_series.begin(),
                                    as.illiq_series.end(), 0.0) / sz;
    double var = 0.0;
    for (double v : as.illiq_series)
      var += (v - as.mean_illiq) * (v - as.mean_illiq);
    as.std_illiq = std::sqrt(var / sz);
    return as;
  }
};

} // namespace msim
