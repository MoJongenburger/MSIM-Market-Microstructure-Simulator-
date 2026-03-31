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

// A lightweight "Level 2" view: price + total quantity + number of resting orders.
struct LevelSummary {
  Price    price{};
  Qty      total_qty{};
  uint32_t order_count{};
};

// ─── QueueInfo ────────────────────────────────────────────────────────────────
// Returned by OrderBook::queue_info(id).
// Tells an agent exactly where its resting limit order sits in the queue
// at its price level — the key data needed for realistic limit-order strategies.
//
// qty_ahead:      lots ahead of this order (must drain before this order fills)
// qty_behind:     lots behind this order at the same price
// level_total:    total qty resting at this price level (= qty_ahead + own_qty + qty_behind)
// own_qty:        remaining quantity of this order itself
// position_index: 0-based FIFO position (0 = front of queue, next to fill)
// found:          false if the order is not in the book (fully filled or never submitted)
struct QueueInfo {
  Qty  qty_ahead{};
  Qty  qty_behind{};
  Qty  level_total{};
  Qty  own_qty{};
  int  position_index{};
  bool found{false};
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

  void erase_locator(OrderId id) noexcept { loc_.erase(id); }

  // Top of book
  std::optional<Price> best_bid() const noexcept;
  std::optional<Price> best_ask() const noexcept;

  // Crossed-book check
  bool is_crossed() const noexcept;

  // L2 depth snapshot: top N levels for a side
  std::vector<LevelSummary> depth(Side side, std::size_t levels) const;

  // ── Queue position query ──────────────────────────────────────────────────
  // Returns QueueInfo for the given order.  O(k) where k = position_index.
  // If the order is not in the book (filled, cancelled, never submitted),
  // returns QueueInfo{.found = false}.
  QueueInfo queue_info(OrderId id) const noexcept;

  // Quick stats
  bool        empty(Side side)       const noexcept;
  std::size_t level_count(Side side) const noexcept;

private:
  friend class MatchingEngine;

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
    Side            side{};
    Price           price{};
    Queue::iterator it{};
  };

  std::unordered_map<OrderId, Locator> loc_;

  bool would_cross(const Order& o) const noexcept;
};

} // namespace msim
