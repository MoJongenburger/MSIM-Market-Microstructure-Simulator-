#pragma once
// ============================================================
// include/msim/matching_engine.hpp
//
// Performance changes vs previous version:
//
//   MatchResult::trades: std::vector<Trade> → SmallVector<Trade,4>
//   ---------------------------------------------------------------
//   Every engine_.process() call constructed a std::vector<Trade>
//   that heap-allocated even when 0 or 1 trades occurred (>95%
//   of orders).  SmallVector<Trade, 4> stores up to 4 trades in
//   inline stack storage — zero heap allocation for the common
//   case.  Multi-level sweeps (>4 fills) spill to heap exactly
//   as before.
//
//   next_event_ts_ cache
//   ---------------------
//   process() called flush() on every single order to check
//   whether a phase transition was due.  In a continuous-
//   trading simulation none ever fire.  next_event_ts_ tracks
//   the earliest pending event (TAL end, halt end, auction
//   end).  flush() returns immediately when ts < next_event_ts_:
//   one comparison instead of three condition checks.
//
//   process_into() simplified
//   --------------------------
//   With SmallVector, process_into() no longer needs the
//   saved-capacity dance — the inline storage makes the common
//   case (0–4 trades) allocation-free by construction.
// ============================================================

#include <limits>
#include <optional>
#include <span>
#include <vector>

#include "msim/book.hpp"
#include "msim/order.hpp"
#include "msim/rules.hpp"
#include "msim/small_vector.hpp"
#include "msim/trade.hpp"

namespace msim {

enum class OrderStatus : uint8_t { Accepted = 0, Rejected = 1 };

struct MatchResult {
  // SmallVector<Trade, 4>: inline storage for 0–4 trades (zero heap alloc).
  // For orders that generate > 4 fills, spills to heap exactly like vector.
  SmallVector<Trade, 4> trades;
  std::optional<Order>  resting;
  Qty                   filled_qty{0};
  OrderStatus           status{OrderStatus::Accepted};
  RejectReason          reject_reason{RejectReason::None};
};

class MatchingEngine {
 public:
  MatchingEngine() = default;
  explicit MatchingEngine(RuleSet rules) : rules_(std::move(rules)) {}

  const OrderBook& book()     const noexcept { return book_; }
  OrderBook&       book_mut()       noexcept { return book_; }
  const RuleSet&   rules()    const noexcept { return rules_; }
  RuleSet&         rules_mut()      noexcept { return rules_; }

  void start_trading_at_last(Ts end_ts) noexcept;
  void start_closing_auction(Ts end_ts) noexcept;

  // Returns empty vector (no alloc) in fast path when ts < next_event_ts_.
  std::vector<Trade> flush(Ts ts);

  // Standard process — returns a MatchResult by value (NRVO).
  MatchResult process(Order incoming);

  // Reuse caller's MatchResult to preserve any heap capacity already
  // allocated in out.trades for runs with large sweeps.
  void process_into(Order incoming, MatchResult& out);

 private:
  OrderBook book_{};
  RuleSet   rules_{};
  TradeId   next_trade_id_{1};

  std::vector<Order> auction_queue_{};
  Ts auction_end_ts_{0};
  Ts tal_end_ts_{0};

  std::optional<Price> cb_ref_price_{};
  Ts halt_end_ts_{0};
  Ts reopen_auction_end_ts_{0};

  // Earliest timestamp at which flush() must actually run.
  // Initialised to max — no pending events.
  // Updated whenever a phase-end timestamp is set or cleared.
  Ts next_event_ts_{std::numeric_limits<Ts>::max()};

  MatchResult        process_market(Order incoming);
  MatchResult        process_limit(Order incoming);
  Qty                available_liquidity(const Order& taker) const noexcept;

  std::optional<Price> reference_price()                            const noexcept;
  std::optional<Price> first_execution_price(const Order& incoming) const noexcept;
  bool breaches_price_band(Price exec_px, Price ref_px)             const noexcept;
  bool should_trigger_volatility_auction(const Order& incoming)     const noexcept;

  MatchResult          queue_in_auction(Order incoming);
  std::vector<Trade>   uncross_auction(Ts uncross_ts);
  std::optional<Price> compute_clearing_price()                     const noexcept;
  Qty                  executable_volume_at(Price px)               const noexcept;

  void maybe_trigger_circuit_breaker(std::span<const Trade> trades);

  // Recompute next_event_ts_ from the three phase-end timestamps.
  // Called whenever any of them change.
  void update_next_event_ts_() noexcept;

  void  match_buy (MatchResult& out, Order& taker);
  void  match_sell(MatchResult& out, Order& taker);
  Trade make_trade(Ts ts, Price px, Qty q, OrderId maker, OrderId taker, Side aggressor);
};

}  // namespace msim
