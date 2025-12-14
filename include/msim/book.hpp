#pragma once
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "msim/order.hpp"
#include "msim/invariants.hpp"
#include "msim/types.hpp"

namespace msim {

// A lightweight “Level 2” view: price + total quantity + number of resting orders.
struct LevelSummary {
  Price    price{};
  Qty      total_qty{};
  uint32_t order_count{};
};

class MatchingEngine; // forward

class OrderBook {
public:
  // Insert a *resting* limit order. Returns false if it would cross the spread.
  bool add_resting_limit(Order o);

  // O(1) cancel/modify
  bool cancel(OrderId id) noexcept;

  // reduce-only modify
  bool modify_qty(OrderId id, Qty new_qty) noexcept;

  // Backwards-compatible alias (used by older simulator code)
  bool modify(OrderId id, Qty new_qty) noexcept { return modify_qty(id, new_qty); }

  void erase_locator(OrderId id) noexcept { loc_.erase(id); } // used by engine when it fully fills a maker

  // Top of book
  std::optional<Price> best_bid() const noexcept;
  std::optional<Price> best_ask() const noexcept;

  // Crossed-book check (should stay false if you only add resting orders correctly)
  bool is_crossed() const noexcept;

  // L2 depth snapshot: top N levels for a side
  std::vector<LevelSummary> depth(Side side, std::size_t levels) const;

  // Quick stats
  bool empty(Side side) const noexcept;
  std::size_t level_count(Side side) const noexcept;

private:
  friend class MatchingEngine; // engine matches directly against containers

  using Queue = std::list<Order>;

  struct Level {
    Queue q;
    Qty total_qty{0};
  };

  using BidMap = std::map<Price, Level, std::greater<Price>>;
  using AskMap = std::map<Price, Level, std::less<Price>>;

  BidMap bids_;
  AskMap asks_;

  struct Locator {
    Side side{};
    Price price{};
    Queue::iterator it{};
  };

  std::unordered_map<OrderId, Locator> loc_;

  bool would_cross(const Order& o) const noexcept;
};

} // namespace msim
