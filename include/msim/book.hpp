#pragma once
// ============================================================
// include/msim/book.hpp
//
// Performance changes:
//   1. BidMap / AskMap: std::map → FlatPriceMap
//      Eliminates red-black tree pointer chasing on every
//      find, insert, and erase.  All level operations are now
//      O(log N) binary search over contiguous cache-resident memory.
//
//   2. Queue: std::list<Order> → std::list<Order, OrderNodeAllocator<Order>>
//      Eliminates per-node malloc/free.  All list nodes come from
//      a thread-local slab pool with O(1) alloc and dealloc.
//
//   3. OrderBook::reserve(n) pre-reserves:
//      - loc_ with max_load_factor 0.5 (no rehashing during run)
//      - bids_ and asks_ capacity for n_levels distinct price levels
//        (avoids FlatPriceMap vector reallocation mid-simulation)
// ============================================================

#include <algorithm>
#include <list>
#include <optional>
#include <unordered_map>
#include <vector>

#include "msim/flat_price_map.hpp"
#include "msim/order_pool.hpp"
#include "msim/order.hpp"
#include "msim/invariants.hpp"
#include "msim/types.hpp"

namespace msim {

// ─── LevelSummary ─────────────────────────────────────────────────────────────
struct LevelSummary {
  Price    price{};
  Qty      total_qty{};
  uint32_t order_count{};
};

// ─── QueueInfo ────────────────────────────────────────────────────────────────
struct QueueInfo {
  Qty  qty_ahead{};
  Qty  qty_behind{};
  Qty  level_total{};
  Qty  own_qty{};
  int  position_index{};
  bool found{false};
};

// ─── OrderBook ────────────────────────────────────────────────────────────────
class MatchingEngine; // forward

class OrderBook {
public:
  // ── Public interface (unchanged from original) ─────────────────────────────
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

  bool        empty(Side side)       const noexcept;
  std::size_t level_count(Side side) const noexcept;

  // ── Performance: pre-reserve all internal maps ─────────────────────────────
  // Call once before the simulation loop with:
  //   expected_orders  — peak simultaneous resting orders (e.g. 256)
  //   n_levels         — max distinct price levels (e.g. 80 for a 40-level LOB)
  //
  // Effect:
  //   loc_  → max_load_factor(0.5) + reserve(expected_orders * 2)
  //   bids_ → capacity for n_levels entries in FlatPriceMap
  //   asks_ → same
  void reserve(std::size_t expected_orders,
               std::size_t n_levels = 80) noexcept {
    loc_.max_load_factor(0.5f);
    loc_.reserve(expected_orders * 2);
    bids_.reserve(n_levels);
    asks_.reserve(n_levels);
  }

private:
  friend class MatchingEngine;

  // ── Queue: order list at one price level ──────────────────────────────────
  // OrderNodeAllocator routes all node allocations through a thread-local
  // slab pool — no malloc/free on the hot path.
  using Queue = std::list<Order, OrderNodeAllocator<Order>>;

  // ── Level: one price level in the book ────────────────────────────────────
  struct Level {
    Queue q{};          // resting orders at this price, FIFO
    Qty   total_qty{};  // sum of qty across all resting orders
  };

  // ── Price level maps ──────────────────────────────────────────────────────
  // FlatPriceMap replaces std::map.
  //   BidMap: sorted highest-first (best bid = front())
  //   AskMap: sorted lowest-first  (best ask = front())
  //
  // Level objects are stored by value inside the vector.  When the
  // vector grows, Levels are move-constructed in O(1) (std::list move).
  // Queue::iterators stored in loc_ remain valid — see flat_price_map.hpp.
  using BidMap = FlatPriceMap<Level, std::greater<Price>>;
  using AskMap = FlatPriceMap<Level, std::less<Price>>;

  BidMap bids_;
  AskMap asks_;

  // ── Order locator map ─────────────────────────────────────────────────────
  // Maps OrderId → (side, price, iterator into the level's Queue).
  // O(1) average lookup; pre-reserved with load_factor 0.5.
  struct Locator {
    Side            side{};
    Price           price{};
    Queue::iterator it{};
  };

  std::unordered_map<OrderId, Locator> loc_;

  bool would_cross(const Order& o) const noexcept;
};

} // namespace msim
