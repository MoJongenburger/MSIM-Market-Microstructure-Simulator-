#pragma once
#include <cstdint>
#include <optional>
#include <span>

#include "msim/order.hpp"
#include "msim/trade.hpp"
#include "msim/types.hpp"

namespace msim {

enum class MarketPhase : uint8_t {
  Continuous = 0,
  Halted     = 1,
  Auction    = 2
};

enum class RejectReason : uint8_t {
  None = 0,
  InvalidOrder,
  MarketHalted,

  // Step 7 additions
  PriceNotOnTick,
  QtyNotOnLot,
  QtyBelowMinimum
};

struct RuleDecision {
  bool accept{true};
  RejectReason reason{RejectReason::None};
};

struct RulesConfig {
  bool enforce_halt{true};

  // Step 7 config: ticks and lots are in "integer ticks" world.
  // Example: if 1 tick = $0.01, then price=12345 means $123.45.
  Price tick_size_ticks{1}; // e.g., 1 means any integer tick is fine
  Qty   lot_size{1};        // e.g., 10 means qty must be multiple of 10
  Qty   min_qty{1};         // e.g., 1 share minimum
};

class RuleSet {
public:
  RuleSet() = default;
  explicit RuleSet(RulesConfig cfg) : cfg_(cfg) {}

  RuleDecision pre_accept(const Order& incoming) const;
  void on_trades(std::span<const Trade> trades);

  void set_phase(MarketPhase p) noexcept { phase_ = p; }
  MarketPhase phase() const noexcept { return phase_; }

  const RulesConfig& config() const noexcept { return cfg_; }
  RulesConfig& config_mut() noexcept { return cfg_; }

  std::optional<Price> last_trade_price() const noexcept { return last_trade_price_; }

private:
  RulesConfig cfg_{};
  MarketPhase phase_{MarketPhase::Continuous};
  std::optional<Price> last_trade_price_{};
};

} // namespace msim
