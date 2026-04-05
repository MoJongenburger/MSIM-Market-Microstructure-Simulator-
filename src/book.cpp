// ============================================================
// src/book.cpp
//
// Performance change vs previous version:
//
//   live_count maintained in add / cancel / depth.
//   depth() uses live_count directly — O(1) per level.
//   Previously iterated all queue entries — O(N total orders).
// ============================================================
#include "msim/book.hpp"

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
  if (o.qty  <= 0)                return false;
  if (would_cross(o))             return false;

  if (o.side == Side::Buy) {
    auto& lvl             = bids_[o.price];
    const auto idx = static_cast<uint32_t>(lvl.q.size());
    lvl.q.push_back(o);
    lvl.total_qty        += o.qty;
    ++lvl.live_count;
    loc_.insert_new(o.id) = Locator{Side::Buy, o.price, idx};
  } else {
    auto& lvl             = asks_[o.price];
    const auto idx = static_cast<uint32_t>(lvl.q.size());
    lvl.q.push_back(o);
    lvl.total_qty        += o.qty;
    ++lvl.live_count;
    loc_.insert_new(o.id) = Locator{Side::Sell, o.price, idx};
  }
  return true;
}

bool OrderBook::cancel(OrderId id) noexcept {
  Locator loc{};
  if (!loc_.extract(id, loc)) return false;

  if (loc.side == Side::Buy) {
    auto lvl_it = bids_.find(loc.price);
    if (lvl_it == bids_.end()) return false;
    auto& lvl = lvl_it->second;

    Order& o = lvl.q[loc.abs_idx];
    lvl.total_qty -= o.qty;
    --lvl.live_count;
    o.qty = 0;

    if (lvl.total_qty == 0) bids_.erase(lvl_it);
  } else {
    auto lvl_it = asks_.find(loc.price);
    if (lvl_it == asks_.end()) return false;
    auto& lvl = lvl_it->second;

    Order& o = lvl.q[loc.abs_idx];
    lvl.total_qty -= o.qty;
    --lvl.live_count;
    o.qty = 0;

    if (lvl.total_qty == 0) asks_.erase(lvl_it);
  }
  return true;
}

bool OrderBook::modify_qty(OrderId id, Qty new_qty) noexcept {
  if (new_qty <= 0) return cancel(id);

  const Locator* lp = loc_.find(id);
  if (!lp) return false;
  const Locator loc = *lp;

  if (loc.side == Side::Buy) {
    auto lvl_it = bids_.find(loc.price);
    if (lvl_it == bids_.end()) return false;
    Order& o = lvl_it->second.q[loc.abs_idx];
    if (o.qty <= 0 || new_qty > o.qty) return false;
    lvl_it->second.total_qty -= (o.qty - new_qty);
    o.qty = new_qty;
  } else {
    auto lvl_it = asks_.find(loc.price);
    if (lvl_it == asks_.end()) return false;
    Order& o = lvl_it->second.q[loc.abs_idx];
    if (o.qty <= 0 || new_qty > o.qty) return false;
    lvl_it->second.total_qty -= (o.qty - new_qty);
    o.qty = new_qty;
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

std::vector<LevelSummary> OrderBook::depth(Side side,
                                            std::size_t levels) const {
  std::vector<LevelSummary> out;
  out.reserve(levels);

  auto emit = [&](const auto& map) {
    std::size_t n = 0;
    for (const auto& [px, lvl] : map) {
      if (n++ >= levels) break;
      if (lvl.total_qty == 0) continue;
      // O(1): live_count is maintained incrementally
      out.push_back({px, lvl.total_qty, lvl.live_count});
    }
  };

  if (side == Side::Buy) emit(bids_);
  else                   emit(asks_);

  return out;
}

bool OrderBook::empty(Side side) const noexcept {
  return (side == Side::Buy) ? bids_.empty() : asks_.empty();
}

std::size_t OrderBook::level_count(Side side) const noexcept {
  return (side == Side::Buy) ? bids_.size() : asks_.size();
}

QueueInfo OrderBook::queue_info(OrderId id) const noexcept {
  const Locator* loc_ptr = loc_.find(id);
  if (!loc_ptr) return QueueInfo{};
  const Locator& loc = *loc_ptr;

  const Level* lvl_ptr = nullptr;
  if (loc.side == Side::Buy) {
    auto it = bids_.find(loc.price);
    if (it == bids_.end()) return QueueInfo{};
    lvl_ptr = &it->second;
  } else {
    auto it = asks_.find(loc.price);
    if (it == asks_.end()) return QueueInfo{};
    lvl_ptr = &it->second;
  }

  const Level& lvl = *lvl_ptr;

  if (loc.abs_idx >= lvl.q.size())  return QueueInfo{};
  if (lvl.q[loc.abs_idx].qty == 0)  return QueueInfo{};

  QueueInfo info{};
  info.found       = true;
  info.level_total = lvl.total_qty;
  info.own_qty     = lvl.q[loc.abs_idx].qty;

  int  pos       = 0;
  Qty  ahead     = 0;
  bool past_self = false;

  for (std::size_t i = lvl.front_offset; i < lvl.q.size(); ++i) {
    if (lvl.q[i].qty == 0) continue;

    if (i == loc.abs_idx) {
      info.position_index = pos;
      info.qty_ahead      = ahead;
      past_self           = true;
    } else if (!past_self) {
      ahead += lvl.q[i].qty;
      ++pos;
    } else {
      info.qty_behind += lvl.q[i].qty;
    }
  }

  if (!past_self) return QueueInfo{};
  return info;
}

}  // namespace msim
