#pragma once
// ============================================================
// include/msim/shared_fundamental.hpp
//
// Correlated fundamental value signal generator for multi-asset
// simulation (Glosten-Milgrom extended to N assets).
//
// Each asset i follows a discrete Ornstein-Uhlenbeck process:
//
//   V_i(t+1) = V_i(t) + κ_i·(μ_i − V_i(t)) + σ_i · ε_i(t)
//
// Innovations ε are correlated via the Cholesky factor of Σ:
//
//   ε = L · z,   z ~ N(0, I)
//
// For two assets with scalar correlation ρ:
//
//   ε_1 = z_1
//   ε_2 = ρ·z_1 + sqrt(1−ρ²)·z_2
//
// Design:
//   SharedFundamental::generate(seed, n_steps) pre-computes the
//   entire signal matrix [n_steps × n_assets] deterministically.
//   This allows multiple independent World instances (one per asset)
//   to run sequentially while sharing the same correlated signal
//   without any synchronisation — essential for MSIM's single-
//   threaded design.
//
// Usage:
//   auto sf = std::make_shared<SharedFundamental>(cfg);
//   sf->generate(seed, 2000);
//
//   // In each World:
//   world.add_agent(std::make_unique<MultiAssetFVAgent>(
//       owner_id, asset_index, sf));
// ============================================================

#include <cassert>
#include <cmath>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

#include "msim/types.hpp"   // Price

namespace msim {

struct AssetFundamentalConfig {
  double kappa  = 0.005;   // OU mean-reversion speed per step
  double sigma  = 1.5;     // innovation volatility (ticks/step)
  double mu     = 0.0;     // long-run mean (0 = initialised from first mid)
  double V0     = 0.0;     // initial value (0 = use mu, set before generate())
};

struct SharedFundamentalConfig {
  std::vector<AssetFundamentalConfig> assets;   // one entry per asset

  // Correlation matrix (lower-triangular Cholesky factor stored as flat vector
  // in row-major order).  For n assets, corr_chol has n*(n+1)/2 entries.
  // For 2 assets with scalar correlation ρ:
  //   corr_chol = {1.0,  ρ, sqrt(1−ρ²)}
  // Leave empty to use identity (zero correlation).
  std::vector<double> corr_chol;

  // Factory for two correlated assets
  static SharedFundamentalConfig two_asset(
      AssetFundamentalConfig a,
      AssetFundamentalConfig b,
      double rho)
  {
    if (std::abs(rho) > 1.0)
      throw std::invalid_argument("SharedFundamentalConfig: |rho| > 1");
    SharedFundamentalConfig cfg;
    cfg.assets     = {a, b};
    cfg.corr_chol  = {1.0, rho, std::sqrt(1.0 - rho * rho)};
    return cfg;
  }
};

// ─── SharedFundamental ───────────────────────────────────────────────────────
class SharedFundamental {
public:
  explicit SharedFundamental(SharedFundamentalConfig cfg)
    : cfg_(std::move(cfg)), n_(cfg_.assets.size())
  {
    if (n_ == 0) throw std::invalid_argument("SharedFundamental: no assets");
    validate_chol();
  }

  std::size_t n_assets()   const noexcept { return n_; }
  int         n_steps()    const noexcept { return static_cast<int>(data_.size() / n_); }
  bool        is_generated() const noexcept { return !data_.empty(); }

  // Pre-compute n_steps of signal for all assets.
  // Call once before the World::run() calls.
  void generate(uint64_t seed, int n_steps) {
    if (n_steps <= 0)
      throw std::invalid_argument("SharedFundamental::generate: n_steps <= 0");

    std::mt19937_64 rng(seed);
    std::normal_distribution<double> z;

    data_.resize(static_cast<std::size_t>(n_steps) * n_);

    // Initialise V from V0 (or mu if V0 == 0)
    std::vector<double> V(n_);
    for (std::size_t i = 0; i < n_; ++i)
      V[i] = (cfg_.assets[i].V0 != 0.0) ? cfg_.assets[i].V0
                                         : cfg_.assets[i].mu;

    for (int t = 0; t < n_steps; ++t) {
      // 1. Draw z ~ N(0, I)
      std::vector<double> zv(n_);
      for (std::size_t i = 0; i < n_; ++i) zv[i] = z(rng);

      // 2. Correlate: ε = L · z  (L = lower-triangular Cholesky)
      std::vector<double> eps = chol_multiply(zv);

      // 3. Advance each asset OU
      for (std::size_t i = 0; i < n_; ++i) {
        const auto& ac = cfg_.assets[i];
        V[i] += ac.kappa * (ac.mu - V[i]) + ac.sigma * eps[i];
        data_[static_cast<std::size_t>(t) * n_ + i] = V[i];
      }
    }
  }

  // Read V_i at a given step (clamped to [0, n_steps−1]).
  double get(std::size_t asset_idx, int step) const noexcept {
    assert(is_generated());
    assert(asset_idx < n_);
    const int s = std::max(0, std::min(step, n_steps() - 1));
    return data_[static_cast<std::size_t>(s) * n_ + asset_idx];
  }

  // Set initial prices for all assets (called after generate if mid-price
  // initialisation is needed).  Shifts the entire signal by (init_price − mu).
  void shift_to_price(std::size_t asset_idx, double init_price) {
    assert(asset_idx < n_);
    const double shift = init_price - cfg_.assets[asset_idx].mu;
    for (int t = 0; t < n_steps(); ++t)
      data_[static_cast<std::size_t>(t) * n_ + asset_idx] += shift;
    cfg_.assets[asset_idx].mu += shift;
  }

  const SharedFundamentalConfig& config() const noexcept { return cfg_; }

private:
  SharedFundamentalConfig cfg_;
  std::size_t             n_;
  std::vector<double>     data_;   // row-major [step, asset]

  // Validate Cholesky factor size  (or fill with identity)
  void validate_chol() {
    const std::size_t expected = n_ * (n_ + 1) / 2;
    if (cfg_.corr_chol.empty()) {
      // Identity: L = I
      cfg_.corr_chol.assign(expected, 0.0);
      for (std::size_t i = 0; i < n_; ++i)
        cfg_.corr_chol[i * (i + 1) / 2 + i] = 1.0;
    } else if (cfg_.corr_chol.size() != expected) {
      throw std::invalid_argument(
          "SharedFundamental: corr_chol size mismatch");
    }
  }

  // Lower-triangular Cholesky multiply: y = L · x
  // L stored in packed row-major lower-triangular form:
  //   L[i,j] = corr_chol[i*(i+1)/2 + j]  for j <= i
  std::vector<double> chol_multiply(const std::vector<double>& x) const {
    std::vector<double> y(n_, 0.0);
    for (std::size_t i = 0; i < n_; ++i)
      for (std::size_t j = 0; j <= i; ++j)
        y[i] += cfg_.corr_chol[i * (i + 1) / 2 + j] * x[j];
    return y;
  }
};

} // namespace msim
