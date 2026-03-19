#pragma once
// ============================================================
// include/msim/hawkes_process.hpp
//
// Univariate Hawkes (self-exciting) process for discrete-time
// order arrival modelling.
//
// Intensity:
//   λ(t) = μ + ψ(t)
//
// Recursive update between steps of width dt:
//   ψ_{t+1} = ψ_t · exp(−β · dt)  +  α · N_t
//
// where N_t = number of events at step t (usually 0 or 1).
//
// Stationary condition (required for ergodicity):
//   α / β  <  1   (branching ratio < 1)
//
// Reference:
//   Hawkes (1971), "Spectra of Some Self-Exciting and Mutually
//   Exciting Point Processes", Biometrika 58(1):83-90.
//   Bacry, Mastromatteo & Muzy (2015), "Hawkes Processes in
//   Finance", Market Microstructure and Liquidity 1(1).
// ============================================================

#include <cmath>
#include <stdexcept>

#include "msim/types.hpp"   // Ts

namespace msim {

struct HawkesConfig {
  // Baseline arrival rate (orders per second)
  double mu        = 10.0;

  // Excitation magnitude per event (events per second added per order)
  double alpha     = 5.0;

  // Decay rate (per second). Half-life = ln(2)/beta.
  // alpha/beta < 1 is required for stationarity.
  double beta      = 20.0;

  void validate() const {
    if (mu   <= 0) throw std::invalid_argument("HawkesConfig: mu must be > 0");
    if (alpha <  0) throw std::invalid_argument("HawkesConfig: alpha must be >= 0");
    if (beta  <= 0) throw std::invalid_argument("HawkesConfig: beta must be > 0");
    if (alpha >= beta)
      throw std::invalid_argument(
          "HawkesConfig: branching ratio alpha/beta >= 1 — process non-stationary");
  }
};

// ─── HawkesProcess ───────────────────────────────────────────────────────────
// Maintains the excitation state ψ.  Call:
//   arrival_prob(dt_ns) → probability of ≥ 1 arrival in this step
//   record_event(n)     → update ψ after n events occurred
//   reset()             → set ψ = 0 (after seed())
//
// The arrival probability uses a Poisson approximation:
//   P(N>0) = 1 − exp(−λ(t) · dt)
//
// For small λ·dt this equals λ·dt (linear regime); for large
// intensities it saturates at 1, preventing super-Poisson blowup.
class HawkesProcess {
public:
  explicit HawkesProcess(HawkesConfig cfg = {}) : cfg_(cfg) {
    cfg_.validate();
  }

  void reset() noexcept { psi_ = 0.0; }

  // Current intensity (events per second)
  double intensity() const noexcept { return cfg_.mu + psi_; }

  // Probability of at least one arrival in a step of width dt_ns nanoseconds
  double arrival_prob(Ts dt_ns) const noexcept {
    const double dt_s  = static_cast<double>(dt_ns) * 1e-9;
    const double lam   = intensity() * dt_s;
    return 1.0 - std::exp(-lam);        // exact Poisson P(N≥1)
  }

  // Call once per step: dt_ns = step width, n_events = orders placed this step
  void advance(Ts dt_ns, int n_events = 0) noexcept {
    const double dt_s  = static_cast<double>(dt_ns) * 1e-9;
    psi_  = psi_ * std::exp(-cfg_.beta * dt_s)   // exponential decay
            + cfg_.alpha * static_cast<double>(n_events);
  }

  // Stationary mean intensity  μ / (1 − α/β)
  double mean_intensity() const noexcept {
    return cfg_.mu / (1.0 - cfg_.alpha / cfg_.beta);
  }

  const HawkesConfig& config() const noexcept { return cfg_; }

private:
  HawkesConfig cfg_;
  double       psi_ = 0.0;   // current excitation
};

} // namespace msim
