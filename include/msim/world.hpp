#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

#include "msim/matching_engine.hpp"
#include "msim/agents/agent.hpp"
#include "msim/agents/market_event.hpp"
#include "msim/agents/market_view.hpp"
#include "msim/simulator.hpp" // for BookTop

namespace msim {

struct WorldStats {
  uint64_t steps{0};
  uint64_t actions_sent{0};
  uint64_t orders_sent{0};
  uint64_t cancels_sent{0};
  uint64_t modifies_sent{0};
  uint64_t rejects{0};
  uint64_t trades{0};
};

struct WorldResult {
  WorldStats stats{};
  std::vector<msim::Trade> trades{};
  std::vector<msim::BookTop> tops{};
};

class World {
public:
  explicit World(msim::MatchingEngine engine) : engine_(std::move(engine)) {}

  msim::MatchingEngine& engine_mut() noexcept { return engine_; }
  const msim::MatchingEngine& engine() const noexcept { return engine_; }

  void add_agent(std::unique_ptr<msim::agents::Agent> a);

  // Deterministic fixed-step simulation + logging
  // seed=0 => derived from start_ts
  WorldResult run(Ts start_ts,
                  Ts horizon_ns,
                  Ts step_ns,
                  std::size_t depth_levels = 0,
                  uint64_t seed = 0);

private:
  msim::MatchingEngine engine_;
  std::vector<std::unique_ptr<msim::agents::Agent>> agents_;
};

} // namespace msim
