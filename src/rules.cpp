#include "msim/rules.hpp"

namespace msim {

static bool is_on_tick(Price price, Price tick) noexcept {
  if (tick <= 0) return false;
  // Prices are integers in ticks; enforce grid by modulo.
  return (price % tick) == 0;
}

static bool is_on_lot(Qty qty, Qty lot) noexcept {
  if (lot <= 0) return false;
  return (qty % lot) == 0;
}

RuleDecision RuleSet::pre_accept(const Order& incoming) const {
  // Basic validity
  if (!is_valid_order(incoming)) {
    return RuleDecision{false, RejectReason::InvalidOrder};
  }

  // Halt rule
  if (cfg_.enforce_halt && phase_ == MarketPhase::Halted) {
    return RuleDecision{false, RejectReason::MarketHalted};
  }

  // Step 7: quantity rules
  if (incoming.qty < cfg_.min_qty) {
    return RuleDecision{false, RejectReason::QtyBelowMinimum};
  }
  if (!is_on_lot(incoming.qty, cfg_.lot_size)) {
    return RuleDecision{false, RejectReason::QtyNotOnLot};
  }

  // Step 7: tick rule applies to limit orders
  if (incoming.type == OrderType::Limit) {
    if (!is_on_tick(incoming.price, cfg_.tick_size_ticks)) {
      return RuleDecision{false, RejectReason::PriceNotOnTick};
    }
  }

  return RuleDecision{true, RejectReason::None};
}

void RuleSet::on_trades(std::span<const Trade> trades) {
  for (const auto& t : trades) {
    last_trade_price_ = t.price;
  }
}

} // namespace msim
