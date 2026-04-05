#pragma once
// ============================================================
// include/msim/book.hpp
//
// Performance change vs previous version:
//
//   live_count in Level — O(1) order_count for depth()
//   ---------------------------------------------------
//   depth() previously computed the live (non-tombstone) order
//   count by iterating from front_offset to q.size() — O(N)
//   per level, where N = total orders ever queued including
//   cancelled ones.  This caused BM_BookDepth_TopN to be
//   2107 ns on Windows (was 90 ns on macOS).
//
//   live_count tracks the number of non-tombstone orders at
//   a price level.  It is incremented on add and decremented
//   on cancel and on full consumption during matching.
//   depth() reads live_count directly — O(1) per level.
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

  struct Level {
    std::vector<Order> q{};
    std::size_t        front_offset{0};
    Qty                total_qty{};
    uint32_t           live_count{0};  // non-tombstone orders; used by depth()
  };

  using BidMap = FlatPriceMap<Level, std::greater<Price>>;
  using AskMap = FlatPriceMap<Level, std::less<Price>>;

  BidMap bids_;
  AskMap asks_;

  struct Locator {
    Side        side{};
    Price       price{};
    std::size_t abs_idx{};
  };

  std::unordered_map<OrderId, Locator> loc_;

  bool would_cross(const Order& o) const noexcept;
};

}  // namespace msim
