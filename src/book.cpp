#include "msim/book.hpp"
#include <algorithm>

namespace msim {

bool OrderBook::would_cross(const Order& o) const noexcept {
  if (o.side == Side::Buy) {
    auto ba = best_ask();
    return ba && o.price >= *ba;
  } else {
    auto bb = best_bid();
    return bb && o.price <= *bb;
  }
}

bool OrderBook::add_resting_limit(Order o) {
  if (o.type != OrderType::Limit) return false;
  if (o.qty <= 0) return false;
  if (would_cross(o)) return false;

  if (o.side == Side::Buy) {
    auto& lvl = bids_[o.price];
    lvl.q.push_back(o);
    auto it = std::prev(lvl.q.end());
    lvl.total_qty += o.qty;
    loc_[o.id] = Locator{Side::Buy, o.price, it};
  } else {
    auto& lvl = asks_[o.price];
    lvl.q.push_back(o);
    auto it = std::prev(lvl.q.end());
    lvl.total_qty += o.qty;
    loc_[o.id] = Locator{Side::Sell, o.price, it};
  }

  return true;
}

bool OrderBook::cancel(OrderId id) noexcept {
  auto it = loc_.find(id);
  if (it == loc_.end()) return false;

  const Locator loc = it->second;

  if (loc.side == Side::Buy) {
    auto lvl_it = bids_.find(loc.price);
    if (lvl_it == bids_.end()) { loc_.erase(it); return false; }
    auto& lvl = lvl_it->second;

    const Qty q = loc.it->qty;
    lvl.total_qty -= q;
    lvl.q.erase(loc.it);

    if (lvl.q.empty()) bids_.erase(lvl_it);
  } else {
    auto lvl_it = asks_.find(loc.price);
    if (lvl_it == asks_.end()) { loc_.erase(it); return false; }
    auto& lvl = lvl_it->second;

    const Qty q = loc.it->qty;
    lvl.total_qty -= q;
    lvl.q.erase(loc.it);

    if (lvl.q.empty()) asks_.erase(lvl_it);
  }

  loc_.erase(it);
  return true;
}

bool OrderBook::modify_qty(OrderId id, Qty new_qty) noexcept {
  // Reduce-only: does not lose time priority.
  if (new_qty <= 0) return cancel(id);

  auto it = loc_.find(id);
  if (it == loc_.end()) return false;

  Locator& loc = it->second;
  if (loc.it->qty <= 0) return false;

  const Qty old_qty = loc.it->qty;
  if (new_qty > old_qty) return false; // reduce-only

  const Qty delta = old_qty - new_qty;
  loc.it->qty = new_qty;

  if (loc.side == Side::Buy) {
    auto lvl_it = bids_.find(loc.price);
    if (lvl_it == bids_.end()) return false;
    lvl_it->second.total_qty -= delta;
  } else {
    auto lvl_it = asks_.find(loc.price);
    if (lvl_it == asks_.end()) return false;
    lvl_it->second.total_qty -= delta;
  }

  return true;
}

std::optional<Price> OrderBook::best_bid() const noexcept {
  if (bids_.empty()) return std::nullopt;
  return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const noexcept {
  if (asks_.empty()) return std::nullopt;
  return asks_.begin()->first;
}

bool OrderBook::is_crossed() const noexcept {
  return is_book_crossed(best_bid(), best_ask());
}

std::vector<LevelSummary> OrderBook::depth(Side side, std::size_t levels) const {
  std::vector<LevelSummary> out;
  out.reserve(levels);

  if (side == Side::Buy) {
    std::size_t n = 0;
    for (const auto& [px, lvl] : bids_) {
      if (n++ >= levels) break;
      out.push_back(LevelSummary{px, lvl.total_qty, static_cast<uint32_t>(lvl.q.size())});
    }
  } else {
    std::size_t n = 0;
    for (const auto& [px, lvl] : asks_) {
      if (n++ >= levels) break;
      out.push_back(LevelSummary{px, lvl.total_qty, static_cast<uint32_t>(lvl.q.size())});
    }
  }

  return out;
}

bool OrderBook::empty(Side side) const noexcept {
  return (side == Side::Buy) ? bids_.empty() : asks_.empty();
}

std::size_t OrderBook::level_count(Side side) const noexcept {
  return (side == Side::Buy) ? bids_.size() : asks_.size();
}


QueueInfo OrderBook::queue_info(OrderId id) const noexcept {
  // 1. Look up the order's locator
  const auto loc_it = loc_.find(id);
  if (loc_it == loc_.end())
    return QueueInfo{};   // not in book — fully filled or never rested

  const Locator& loc = loc_it->second;

  // 2. Find the level
  // We walk the list from begin() to loc.it, accumulating qty_ahead.
  // Complexity: O(k) where k = position_index.  For typical books with
  // O(10) orders per level this is negligible.
  const Queue* q_ptr = nullptr;
  Qty level_total    = 0;

  if (loc.side == Side::Buy) {
    const auto lvl_it = bids_.find(loc.price);
    if (lvl_it == bids_.end()) return QueueInfo{};
    q_ptr       = &lvl_it->second.q;
    level_total =  lvl_it->second.total_qty;
  } else {
    const auto lvl_it = asks_.find(loc.price);
    if (lvl_it == asks_.end()) return QueueInfo{};
    q_ptr       = &lvl_it->second.q;
    level_total =  lvl_it->second.total_qty;
  }

  const Queue& q = *q_ptr;

  // 3. Walk from front to the order's iterator
  QueueInfo info{};
  info.found       = true;
  info.level_total = level_total;

  int  idx         = 0;
  Qty  ahead       = 0;
  bool found_self  = false;

  for (auto it = q.begin(); it != q.end(); ++it) {
    if (it == loc.it) {
      // This is our order
      info.own_qty        = it->qty;
      info.qty_ahead      = ahead;
      info.position_index = idx;
      found_self          = true;
    } else if (!found_self) {
      ahead += it->qty;
      ++idx;
    } else {
      // Orders after ours contribute to qty_behind
      info.qty_behind += it->qty;
    }
  }

  if (!found_self) {
    // Iterator was stale — order is gone (shouldn't happen in normal use)
    return QueueInfo{};
  }

  return info;
}
} // namespace msim
