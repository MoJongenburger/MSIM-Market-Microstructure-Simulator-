#pragma once
#include <random>
#include <vector>

#include "msim/agents/actions.hpp"
#include "msim/agents/market_event.hpp"
#include "msim/agents/market_view.hpp"
#include "msim/types.hpp"

namespace msim::agents {

class Agent {
public:
  virtual ~Agent() = default;

  virtual OwnerId owner_id() const noexcept = 0;

  // Called after exchange processes something and produces an event
  virtual void on_market_event(const MarketEvent& ev) { (void)ev; }

  // Called at each timestep (deterministic schedule)
  virtual std::vector<Action> generate_actions(
      const MarketView& view,
      std::mt19937_64& rng) = 0;
};

} // namespace msim::agents
