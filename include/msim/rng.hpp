#pragma once
#include <cstdint>
#include <random>

namespace msim {

// Simple reproducible RNG wrapper (Mersenne Twister 64)
class Rng {
public:
  explicit Rng(uint64_t seed) : eng_(seed) {}

  // Uniform [0,1)
  double uniform01() { return uni_(eng_); }

  // Uniform integer in [lo, hi]
  int32_t uniform_int(int32_t lo, int32_t hi) {
    std::uniform_int_distribution<int32_t> d(lo, hi);
    return d(eng_);
  }

  // Exponential with rate lambda (mean 1/lambda)
  double exp(double lambda) {
    std::exponential_distribution<double> d(lambda);
    return d(eng_);
  }

private:
  std::mt19937_64 eng_;
  std::uniform_real_distribution<double> uni_{0.0, 1.0};
};

} // namespace msim
