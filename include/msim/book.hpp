#pragma once
// ============================================================
// include/msim/book.hpp
//
// Performance changes vs previous version:
//
//   Per-level queue: std::list → std::vector + front_offset
//   --------------------------------------------------------
//   std::list stores each Order in a separately-allocated heap
//   node.  Even with OrderNodeAllocator, iterating the list
//   during matching is pointer-chasing — each node's ->next
//   could be in a different slab page.
//
//   std::vector<Order> stores all orders at a price level
//   contiguously.  The matching inner loop becomes a sequential
//   scan the hardware prefetcher can run at full speed.
//
//   Locator: Queue::iterator → abs_idx (std::size_t)
//   -------------------------------------------------
//   A list iterator is a pointer to a heap node.  With the
//   vector-based queue we can't store an iterator (vector
//   iterators are invalidated on push_back).  Instead we store
//   abs_idx — the index into the vector at the time of insertion.
//   abs_idx is stable: push_back may grow the vector (moving
//   all elements) but indices do not change.
//
//   Cancel: tombstone instead of erase
//   ------------------------------------
//   Erasing from the middle of a vector is O(N).  Instead,
//   cancel() sets Order::qty = 0 at abs_idx (a tombstone).
//   match_buy / match_sell skip tombstones at the front of the
//   queue before each match step — O(k) where k is the number
//   of consecutive cancelled orders.  In practice k ~ 0-1.
//   The level is erased from the FlatPriceMap only when
//   total_qty reaches 0 (all live qty consumed or cancelled).
//
//   order_pool.hpp is no longer included — std::list and the
//   slab allocator are no longer used.
// ============================================================

#include <algorithm>
#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

#include "msim/flat_price_map.hpp"
#include "msim/order.hpp"
#include "msim/invariants.hpp"
#include "msim/types.hpp"

namespace msim {

struct LevelSummary {
  Price    price{};
  Qty      total_qty{};
  uint32_t order_count{};
};

struct QueueInfo {
  Qty  qty_ahead{};
  Qty  qty_behind{};
  Qty  level_total{};
  Qty  own_qty{};
  int  position_index{};
  bool found{false};
};

class MatchingEngine;

class OrderBook {
 public:
  bool add_resting_limit(Order o);
  bool cancel(OrderId id) noexcept;
  bool modify_qty(OrderId id, Qty new_qty) noexcept;
  bool modify(OrderId id, Qty new_qty) noexcept { return modify_qty(id, new_qty); }
  void erase_locator(OrderId id) noexcept { loc_.erase(id); }

  std::optional<Price> best_bid() const noexcept;
  std::optional<Price> best_ask() const noexcept;
  bool is_crossed() const noexcept;

  std::vector<LevelSummary> depth(Side side, std::size_t levels) const;
  QueueInfo queue_info(OrderId id) const noexcept;

  bool        empty(Side side)        const noexcept;
  std::size_t level_count(Side side)  const noexcept;

  // O(1) top-of-book quantity — used by World::compute_imbalance().
  Qty best_bid_qty() const noexcept {
    return bids_.empty() ? Qty{0} : bids_.begin()->second.total_qty;
  }
  Qty best_ask_qty() const noexcept {
    return asks_.empty() ? Qty{0} : asks_.begin()->second.total_qty;
  }

  void reserve(std::size_t expected_orders,
               std::size_t n_levels = 80) noexcept {
    loc_.max_load_factor(0.5f);
    loc_.reserve(expected_orders * 2);
    bids_.reserve(n_levels);
    asks_.reserve(n_levels);
  }

 private:
  friend class MatchingEngine;

  // ── Level ────────────────────────────────────────────────────────────────
  // q           — all Orders ever pushed at this price (including tombstones).
  // front_offset — index of the first entry that might be live.
  //               Advances when the front order is fully matched.
  // total_qty   — sum of live (non-tombstone) Order::qty values.
  //               When this reaches 0, the level is erased from the map.
  struct Level {
    std::vector<Order> q{};
    std::size_t        front_offset{0};
    Qty                total_qty{};
  };

  using BidMap = FlatPriceMap<Level, std::greater<Price>>;
  using AskMap = FlatPriceMap<Level, std::less<Price>>;

  BidMap bids_;
  AskMap asks_;

  // ── Locator ──────────────────────────────────────────────────────────────
  // abs_idx is the index into Level::q at insertion time.
  // It is stable across push_back-induced reallocations of q
  // because we never erase or insert in the middle.
  struct Locator {
    Side        side{};
    Price       price{};
    std::size_t abs_idx{};
  };

  std::unordered_map<OrderId, Locator> loc_;

  bool would_cross(const Order& o) const noexcept;
};

}  // namespace msim
