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

class MatchingEngine; // forward

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

  // Allocating depth snapshot — kept for external callers / tests.
  std::vector<LevelSummary> depth(Side side, std::size_t levels) const;

  // ── Zero-allocation depth snapshot (performance-critical path) ───────────
  // Clears `out` then fills it.  Pass a long-lived buffer to avoid any
  // heap allocation on the simulation hot path.
  //
  // World::compute_imbalance() uses this to query top-1 every step with
  // two pre-allocated member buffers, eliminating ~2 allocs/ms/simulation.
  void depth_into(Side side, std::size_t levels,
                  std::vector<LevelSummary>& out) const;

  QueueInfo queue_info(OrderId id) const noexcept;

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
