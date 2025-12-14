#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "msim/order.hpp"
#include "msim/trade.hpp"

namespace msim {

// Market phase is the state machine foundation for auctions/halts later.
enum class MarketPhase : uint8_t {
  Continuous = 0,
  Halted     = 1,
  Auction    = 2
};

// Structured rejection reasons (we’ll expand this later).
enum class RejectReason : uint8_t {
  None = 0,
  InvalidOrder,
  MarketHalted,
};

struct RuleDecision {
  bool accept{true};
  RejectReason reason{RejectReason::None};
};

// Config grows over time (tick size, bands, STP, rate limits, etc.)
struct RulesConfig {
  bool enforce_halt{true};
};

class RuleSet {
public:
  RuleSet() = default;
  explicit RuleSet(RulesConfig cfg) : cfg_(cfg) {}

  // Called BEFORE we attempt matching. If reject -> order is dropped.
  RuleDecision pre_accept(const Order& incoming) const;

  // Called AFTER matching to update reference data (last trade, etc.)
  void on_trades(std::span<const Trade> trades);

  // Market phase control (later: driven by session schedule & circuit breaker logic)
  void set_phase(MarketPhase p) noexcept { phase_ = p; }
  MarketPhase phase() const noexcept { return phase_; }

  std::optional<Price> last_trade_price() const noexcept { return last_trade_price_; }

private:
  RulesConfig cfg_{};
  MarketPhase phase_{MarketPhase::Continuous};
  std::optional<Price> last_trade_price_{};
};

} // namespace msim
