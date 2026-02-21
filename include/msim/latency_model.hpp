#pragma once
// =============================================================================
// msim/latency_model.hpp
//
// Per-Agent Network Latency Model
//
// Motivation:
//   In real electronic markets, every participant experiences a different
//   round-trip latency to the exchange.  Co-located HFT firms operate at
//   ~1–10 µs; regional participants at 1–10 ms; retail brokers at 10–100 ms.
//   This heterogeneity is not cosmetic: it determines execution priority when
//   multiple agents respond to the same market event simultaneously.
//
//   Without a latency model, MSIM processes all agents at the same simulated
//   timestamp, giving every agent equal priority regardless of proximity to
//   the exchange — an unrealistic assumption that masks the competitive
//   advantage of low-latency participants.
//
// Implementation:
//   Each agent i is associated with a LatencyDistribution that draws a
//   per-message delay δ_i ~ F_i, where F_i is configurable:
//
//     FIXED:       δ_i = c_i  (constant latency)
//     GAUSSIAN:    δ_i = max(0, N(µ_i, σ_i²))
//     LOG_NORMAL:  δ_i = Lognormal(µ_i, σ_i²)  — right-skewed, realistic
//     UNIFORM:     δ_i = U(lo_i, hi_i)
//
//   When an agent submits an order at world time ts, the order is timestamped
//   as ts + δ_i and injected into a priority queue (LatencyQueue) ordered by
//   effective arrival time.  The World's step() function drains all orders
//   whose effective time ≤ current_ts before advancing the clock, ensuring
//   that lower-latency agents' orders are processed first even if submitted
//   at the same nominal step.
//
// Integration with World:
//   Replace the synchronous per-agent dispatch in World::run() with:
//     1. Collect agent actions into LatencyQueue::push(order, agent_latency)
//     2. Drain LatencyQueue::drain(ts) → process orders in arrival-time order
//   See world.hpp integration guide in this file's comments.
// =============================================================================

#include <cstdint>
#include <random>
#include <queue>
#include <vector>
#include <functional>
#include <algorithm>
#include "types.hpp"

namespace msim {

// ─── Latency distribution types ─────────────────────────────────────────────

enum class LatencyDistType {
    FIXED,       ///< Constant delay; fastest to compute
    GAUSSIAN,    ///< Normal distribution, clipped at 0; easy to calibrate
    LOG_NORMAL,  ///< Log-normal; realistic for network jitter (right-skewed)
    UNIFORM,     ///< Uniform [lo, hi]; simple worst-case model
};

struct LatencyDistConfig {
    LatencyDistType type  = LatencyDistType::LOG_NORMAL;
    double mu             = 1000.0;  ///< Mean delay in nanoseconds (1 µs default)
    double sigma          = 200.0;   ///< Std-dev for GAUSSIAN / shape for LOG_NORMAL
    double lo             = 500.0;   ///< Lower bound for UNIFORM (ns)
    double hi             = 2000.0;  ///< Upper bound for UNIFORM (ns)
};

// ─── Per-agent latency sampler ────────────────────────────────────────────────

class LatencySampler {
public:
    LatencySampler(uint64_t seed, LatencyDistConfig cfg)
        : cfg_(cfg), rng_(seed), normal_(0.0, 1.0), uniform_(0.0, 1.0)
    {
        if (cfg_.type == LatencyDistType::LOG_NORMAL) {
            // Convert (mean_ns, sigma_ns) to lognormal (µ_ln, σ_ln) parameters:
            //   µ_ln = ln(µ) - σ_ln²/2
            //   σ_ln = sqrt(ln(1 + (σ/µ)²))
            const double cv2 = (cfg_.sigma / cfg_.mu) * (cfg_.sigma / cfg_.mu);
            sigma_ln_ = std::sqrt(std::log(1.0 + cv2));
            mu_ln_    = std::log(cfg_.mu) - 0.5 * sigma_ln_ * sigma_ln_;
        }
    }

    /// Draw one latency sample in nanoseconds, returned as integer Ts
    Ts sample() {
        double ns = 0.0;
        switch (cfg_.type) {
            case LatencyDistType::FIXED:
                ns = cfg_.mu;
                break;
            case LatencyDistType::GAUSSIAN:
                ns = std::max(0.0, cfg_.mu + cfg_.sigma * normal_(rng_));
                break;
            case LatencyDistType::LOG_NORMAL:
                ns = std::exp(mu_ln_ + sigma_ln_ * normal_(rng_));
                break;
            case LatencyDistType::UNIFORM:
                ns = cfg_.lo + (cfg_.hi - cfg_.lo) * uniform_(rng_);
                break;
        }
        return static_cast<Ts>(std::round(ns));
    }

    LatencyDistConfig config() const { return cfg_; }

private:
    LatencyDistConfig           cfg_;
    std::mt19937_64             rng_;
    std::normal_distribution<double>  normal_;
    std::uniform_real_distribution<double> uniform_;
    double                      mu_ln_ = 0.0, sigma_ln_ = 0.0;
};

// ─── Latency-stamped order wrapper ───────────────────────────────────────────

// This template wraps any Order type (your existing msim::Order or the
// agent-returned Order struct) with an effective arrival timestamp.
template <typename OrderT>
struct LatencyOrder {
    Ts      effective_ts;   ///< ts_submitted + latency_delta
    int     agent_idx;      ///< Which agent submitted this order
    OrderT  order;

    // Priority queue uses min-heap on effective_ts (earliest first)
    bool operator>(const LatencyOrder& o) const {
        return effective_ts > o.effective_ts;
    }
};

// ─── Latency-aware priority queue ────────────────────────────────────────────

template <typename OrderT>
class LatencyQueue {
public:
    using LO = LatencyOrder<OrderT>;

    /// Push an order submitted at time ts_submitted by agent agent_idx,
    /// using the sampler for that agent to determine delivery delay.
    void push(OrderT order, int agent_idx, Ts ts_submitted,
              LatencySampler& sampler) {
        const Ts delta = sampler.sample();
        pq_.push(LO{ts_submitted + delta, agent_idx, std::move(order)});
    }

    /// Drain all orders whose effective_ts <= current_ts, in arrival order.
    /// Returns a vector of (agent_idx, order) pairs for processing.
    std::vector<std::pair<int, OrderT>> drain(Ts current_ts) {
        std::vector<std::pair<int, OrderT>> out;
        while (!pq_.empty() && pq_.top().effective_ts <= current_ts) {
            out.emplace_back(pq_.top().agent_idx, pq_.top().order);
            pq_.pop();
        }
        return out;
    }

    bool  empty() const { return pq_.empty(); }
    size_t size() const { return pq_.size(); }

    /// Peek at next effective timestamp without popping
    Ts next_ts() const {
        return pq_.empty() ? std::numeric_limits<Ts>::max() : pq_.top().effective_ts;
    }

private:
    std::priority_queue<LO, std::vector<LO>, std::greater<LO>> pq_;
};

// ─── WorldLatencyConfig — attach one LatencyDistConfig per agent ─────────────
// Usage:
//   WorldLatencyConfig lc;
//   lc.agent_configs.push_back({LatencyDistType::FIXED,   500.0});  // HFT
//   lc.agent_configs.push_back({LatencyDistType::LOG_NORMAL, 5000.0, 1000.0}); // retail

struct WorldLatencyConfig {
    std::vector<LatencyDistConfig> agent_configs;  ///< One entry per agent
    bool enabled = true;  ///< If false, zero latency (original MSIM behaviour)

    /// Convenience: set all agents to the same distribution
    static WorldLatencyConfig uniform_all(int n_agents, LatencyDistConfig cfg) {
        WorldLatencyConfig wlc;
        wlc.agent_configs.assign(n_agents, cfg);
        return wlc;
    }

    /// Convenience: HFT tier (low latency) vs retail tier (high latency)
    static WorldLatencyConfig two_tier(int n_hft, int n_retail,
                                       double hft_ns  = 500.0,
                                       double ret_ns  = 50000.0,
                                       double ret_sig = 10000.0) {
        WorldLatencyConfig wlc;
        for (int i = 0; i < n_hft;    ++i)
            wlc.agent_configs.push_back({LatencyDistType::FIXED, hft_ns});
        for (int i = 0; i < n_retail; ++i)
            wlc.agent_configs.push_back({LatencyDistType::LOG_NORMAL, ret_ns, ret_sig});
        return wlc;
    }
};

// ─── Integration guide ───────────────────────────────────────────────────────
//
// In world.cpp, replace the synchronous dispatch loop:
//
//   BEFORE (original):
//     for (auto& agent : agents_) {
//         auto order = agent->act(view, ts);
//         if (order) engine_.process(*order, ts);
//     }
//
//   AFTER (with latency model):
//     LatencyQueue<AgentOrder> lq;
//     for (int i = 0; i < agents_.size(); ++i) {
//         auto order = agents_[i]->act(view, ts);
//         if (order) lq.push(*order, i, ts, latency_samplers_[i]);
//     }
//     // Drain: process orders in effective-arrival-time order
//     for (auto& [agent_idx, order] : lq.drain(ts + dt_ns_)) {
//         engine_.process(order, order.effective_ts);
//     }
//
// Initialise latency_samplers_ in World's constructor:
//     for (int i = 0; i < n_agents; ++i) {
//         uint64_t seed_i = splitmix64(global_seed ^ (i * 0x9e3779b97f4a7c15ULL));
//         latency_samplers_.emplace_back(seed_i, latency_cfg_.agent_configs[i]);
//     }

} // namespace msim
