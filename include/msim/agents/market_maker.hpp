#pragma once
#include <cstdint>
#include <optional>

#include "msim/order.hpp"
#include "msim/rules.hpp"
#include "msim/world.hpp"

namespace msim {

struct MarketMakerParams {
  Qty   quote_qty{10};
  Price spread_ticks{4};          // total spread in ticks
  Ts    refresh_ns{50'000'000};   // 50ms
  Price max_skew_ticks{20};       // inventory skew clamp
  int64_t skew_per_unit{1};       // ticks per 1 inventory unit
};

class MarketMaker final : public IAgent {
public:
  MarketMaker(OwnerId owner, RulesConfig rules_cfg, MarketMakerParams p);

  OwnerId owner() const noexcept override { return owner_; }
  void seed(uint64_t s) override { seed_ = s; } // deterministic hook
  void step(Ts ts, const MarketView& view, const AgentState& self, std::vector<Action>& out) override;

private:
  OrderId next_id_() noexcept;

  OwnerId owner_{};
  RulesConfig rules_cfg_{};
  MarketMakerParams p_{};

  uint64_t seed_{0};

  Ts next_refresh_ts_{0};
  OrderId bid_id_{0};
  OrderId ask_id_{0};
  uint32_t local_seq_{1};
};

} // namespace msim
