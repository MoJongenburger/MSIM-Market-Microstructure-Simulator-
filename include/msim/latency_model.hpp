#pragma once
// ============================================================
// include/msim/latency_model.hpp
//
// Per-agent network latency sampler.
//
// NOTE: PendingAction and LatencyActionBuffer live in world.hpp
// (after the Action struct is fully defined) because they need
// the complete Action type.  This file contains only the
// distribution config and sampler, which have no such dependency.
// ============================================================

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "msim/types.hpp"   // Ts (int64_t), OwnerId (uint64_t)

namespace msim {

// ─── Distribution types ──────────────────────────────────────────────────────
enum class LatencyDistType {
  FIXED,       // constant δ = mu ns
  GAUSSIAN,    // δ ~ N(mu, sigma²) clipped at 0
  LOG_NORMAL,  // right-skewed; best model for real network jitter
  UNIFORM,     // δ ~ U[lo, hi]
};

struct LatencyDistConfig {
  LatencyDistType type  = LatencyDistType::LOG_NORMAL;
  double mu             = 1'000.0;  // mean nanoseconds (1 µs default)
  double sigma          = 200.0;    // std-dev (GAUSSIAN) or shape (LOG_NORMAL)
  double lo             = 500.0;    // lower bound (UNIFORM only)
  double hi             = 2'000.0;  // upper bound (UNIFORM only)
};

// ─── Per-agent latency sampler ────────────────────────────────────────────────
class LatencySampler {
public:
  LatencySampler(uint64_t seed, LatencyDistConfig cfg)
    : cfg_(cfg), rng_(seed), normal_(0.0, 1.0), uniform_(0.0, 1.0)
  {
    if (cfg_.type == LatencyDistType::LOG_NORMAL) {
      const double cv2  = (cfg_.sigma / cfg_.mu) * (cfg_.sigma / cfg_.mu);
      sigma_ln_ = std::sqrt(std::log(1.0 + cv2));
      mu_ln_    = std::log(cfg_.mu) - 0.5 * sigma_ln_ * sigma_ln_;
    }
  }

  // Returns one delay sample in nanoseconds as Ts (int64_t).
  Ts sample() {
    double ns = 0.0;
    switch (cfg_.type) {
      case LatencyDistType::FIXED:
        ns = cfg_.mu; break;
      case LatencyDistType::GAUSSIAN:
        ns = std::max(0.0, cfg_.mu + cfg_.sigma * normal_(rng_)); break;
      case LatencyDistType::LOG_NORMAL:
        ns = std::exp(mu_ln_ + sigma_ln_ * normal_(rng_)); break;
      case LatencyDistType::UNIFORM:
        ns = cfg_.lo + (cfg_.hi - cfg_.lo) * uniform_(rng_); break;
    }
    return static_cast<Ts>(std::round(ns));
  }

private:
  LatencyDistConfig                      cfg_;
  std::mt19937_64                        rng_;
  std::normal_distribution<double>       normal_;
  std::uniform_real_distribution<double> uniform_;
  double mu_ln_    = 0.0;
  double sigma_ln_ = 0.0;
};

// ─── WorldLatencyConfig ───────────────────────────────────────────────────────
struct WorldLatencyConfig {
  bool                           enabled = false;
  std::vector<LatencyDistConfig> agent_configs;   // one entry per agent

  static WorldLatencyConfig uniform_all(std::size_t n, LatencyDistConfig cfg) {
    WorldLatencyConfig wlc;
    wlc.enabled = true;
    wlc.agent_configs.assign(n, cfg);
    return wlc;
  }

  // n_fast first agents = HFT tier; remaining n_slow = retail tier
  static WorldLatencyConfig two_tier(int n_fast, int n_slow,
                                     double fast_ns  = 500.0,
                                     double slow_ns  = 50'000.0,
                                     double slow_sig = 10'000.0)
  {
    WorldLatencyConfig wlc;
    wlc.enabled = true;
    for (int i = 0; i < n_fast; ++i)
      wlc.agent_configs.push_back({LatencyDistType::FIXED, fast_ns});
    for (int i = 0; i < n_slow; ++i)
      wlc.agent_configs.push_back(
          {LatencyDistType::LOG_NORMAL, slow_ns, slow_sig});
    return wlc;
  }
};

} // namespace msim
