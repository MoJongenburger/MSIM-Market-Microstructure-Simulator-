#pragma once
// ============================================================
// include/msim/latency_model.hpp
//
// Per-agent network latency model.
// Types match msim exactly (Ts = int64_t, OwnerId = uint64_t).
// ============================================================

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "msim/types.hpp"   // Ts, OwnerId

namespace msim {

// Forward-declared in world.hpp; defined here
struct Action;

// ─── Distribution types ───────────────────────────────────────────────────────
enum class LatencyDistType {
  FIXED,       // constant δ = mu ns
  GAUSSIAN,    // δ ~ N(mu, sigma²) clipped at 0
  LOG_NORMAL,  // δ ~ Lognormal(mu_ln, sigma_ln) — right-skewed, realistic
  UNIFORM,     // δ ~ U[lo, hi]
};

struct LatencyDistConfig {
  LatencyDistType type  = LatencyDistType::LOG_NORMAL;
  double mu             = 1'000.0;  // mean nanoseconds (1 µs)
  double sigma          = 200.0;    // std-dev (GAUSSIAN) or shape (LOG_NORMAL)
  double lo             = 500.0;    // lower bound for UNIFORM
  double hi             = 2'000.0;  // upper bound for UNIFORM
};

// ─── Per-agent sampler ────────────────────────────────────────────────────────
class LatencySampler {
public:
  LatencySampler(uint64_t seed, LatencyDistConfig cfg)
    : cfg_(cfg), rng_(seed), normal_(0.0, 1.0), uniform_(0.0, 1.0)
  {
    if (cfg_.type == LatencyDistType::LOG_NORMAL) {
      const double cv2 = (cfg_.sigma / cfg_.mu) * (cfg_.sigma / cfg_.mu);
      sigma_ln_ = std::sqrt(std::log(1.0 + cv2));
      mu_ln_    = std::log(cfg_.mu) - 0.5 * sigma_ln_ * sigma_ln_;
    }
  }

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

// ─── Pending action (latency-stamped) ────────────────────────────────────────
// Wraps an Action with effective arrival time and owner so world.cpp
// can process it after sorting.
struct PendingAction {
  Ts      effective_ts = 0;
  OwnerId owner        = 0;
  Action  action{};    // Action defined in world.hpp; included by world.cpp

  bool operator<(const PendingAction& o) const noexcept {
    return effective_ts < o.effective_ts;
  }
};

// ─── LatencyActionBuffer ─────────────────────────────────────────────────────
// Collects all actions from all agents in one step, then returns them
// sorted by effective arrival time on drain().
class LatencyActionBuffer {
public:
  // Push all actions from one agent with a sampled latency delay.
  void push(Ts ts, OwnerId owner,
            const std::vector<Action>& actions,
            LatencySampler& sampler)
  {
    for (const auto& act : actions) {
      const Ts delta = sampler.sample();
      pending_.push_back({ts + delta, owner, act});
    }
  }

  // Push with zero delay (used when latency_enabled = false;
  // preserves insertion / registration order for identical effective_ts).
  void push_immediate(Ts ts, OwnerId owner,
                      const std::vector<Action>& actions)
  {
    for (const auto& act : actions)
      pending_.push_back({ts, owner, act});
  }

  // Sort pending actions by effective_ts (stable = ties keep agent order).
  const std::vector<PendingAction>& drain() {
    std::stable_sort(pending_.begin(), pending_.end());
    return pending_;
  }

  void   clear()  { pending_.clear(); }
  size_t size()   const noexcept { return pending_.size(); }

private:
  std::vector<PendingAction> pending_;
};

// ─── WorldLatencyConfig helpers ───────────────────────────────────────────────
struct WorldLatencyConfig {
  bool                           enabled = false;
  std::vector<LatencyDistConfig> agent_configs;   // one per registered agent

  // All agents share one distribution
  static WorldLatencyConfig uniform_all(int n, LatencyDistConfig cfg) {
    WorldLatencyConfig wlc;
    wlc.enabled = true;
    wlc.agent_configs.assign(n, cfg);
    return wlc;
  }

  // First n_fast agents = HFT (low latency); remainder = retail (high latency)
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
