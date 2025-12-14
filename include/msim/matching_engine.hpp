#pragma once
#include <optional>
#include <vector>

#include "msim/book.hpp"
#include "msim/order.hpp"
#include "msim/rules.hpp"
#include "msim/trade.hpp"

namespace msim {

enum class OrderStatus : uint8_t { Accepted = 0, Rejected = 1 };

struct MatchResult {
  std::vector<Trade> trades;
  std::optional<Order> resting;
  Qty filled_qty{0};

  OrderStatus status{OrderStatus::Accepted};
  RejectReason reject_reason{RejectReason::None};
};

class MatchingEngine {
public:
  MatchingEngine() = default;
  explicit MatchingEngine(RuleSet rules) : rules_(std::move(rules)) {}

  const OrderBook& book() const noexcept { return book_; }
  OrderBook& book_mut() noexcept { return book_; }

  const RuleSet& rules() const noexcept { return rules_; }
  RuleSet& rules_mut() noexcept { return rules_; }

  MatchResult process(Order incoming);

private:
  OrderBook book_{};
  RuleSet rules_{};
  TradeId next_trade_id_{1};

  // Auction state
  std::vector<Order> auction_queue_{};
  Ts auction_end_ts_{0};

  MatchResult process_market(Order incoming);
  MatchResult process_limit(Order incoming);

  Qty available_liquidity(const Order& taker) const noexcept;

  // Step 10 band helpers
  std::optional<Price> reference_price() const noexcept;
  std::optional<Price> first_execution_price(const Order& incoming) const noexcept;
  bool breaches_price_band(Price exec_px, Price ref_px) const noexcept;
  bool should_trigger_volatility_auction(const Order& incoming) const noexcept;

  // Step 11 auction helpers
  MatchResult queue_in_auction(Order incoming);
  std::vector<Trade> uncross_auction(Ts uncross_ts); // <-- replaces replay

  std::optional<Price> compute_clearing_price() const noexcept;
  Qty executable_volume_at(Price px) const noexcept;

  void match_buy(MatchResult& out, Order& taker);
  void match_sell(MatchResult& out, Order& taker);

  Trade make_trade(Ts ts, Price px, Qty q, OrderId maker, OrderId taker);
};

} // namespace msim
