#pragma once
// ============================================================
// include/msim/flat_price_map.hpp
//
// Performance change vs previous version:
//
//   front_offset_ — O(1) front-erase for the sweep hot path
//   --------------------------------------------------------
//   BM_ProcessMarket_SweepKLevels was 4x slower on Windows
//   (103 ns) than macOS (27 ns) because erasing the best
//   price level after fully matching it called
//   std::vector::erase(begin()), which shifts all remaining
//   entries left — O(N).  For a K=1024 level sweep that is
//   O(K²) total shifts (~512K memmove operations).
//
//   With front_offset_: erasing the front element advances
//   a cursor instead of shifting — O(1).  A lazy compaction
//   runs only when the dead prefix exceeds half the vector,
//   so the amortised cost is O(K) for the full sweep.
//
//   All other operations (find, operator[], back-erase) are
//   unchanged.  Binary search uses begin()+front_offset_ so
//   dead prefix entries are never visited.
// ============================================================

#include <algorithm>
#include <functional>
#include <vector>

#include "msim/types.hpp"

namespace msim {

template <typename Level, typename Cmp = std::less<Price>>
class FlatPriceMap {
public:
  struct Entry {
    Price first{};
    Level second{};

    explicit Entry(Price p) : first(p), second{} {}
    Entry() = default;

    Entry(const Entry&)            = delete;
    Entry& operator=(const Entry&) = delete;
    Entry(Entry&&)                 = default;
    Entry& operator=(Entry&&)      = default;
  };

  using iterator       = typename std::vector<Entry>::iterator;
  using const_iterator = typename std::vector<Entry>::const_iterator;

  // begin/end skip over the dead prefix (consumed front entries)
  iterator       begin()        noexcept { return entries_.begin() + static_cast<std::ptrdiff_t>(front_offset_); }
  iterator       end()          noexcept { return entries_.end();   }
  const_iterator begin()  const noexcept { return entries_.begin() + static_cast<std::ptrdiff_t>(front_offset_); }
  const_iterator end()    const noexcept { return entries_.end();   }

  bool        empty() const noexcept { return front_offset_ >= entries_.size(); }
  std::size_t size()  const noexcept {
    return entries_.size() > front_offset_ ? entries_.size() - front_offset_ : 0;
  }

  iterator find(Price px) noexcept {
    auto it = lower_bound_(px);
    if (it != entries_.end() && it->first == px) return it;
    return entries_.end();
  }

  const_iterator find(Price px) const noexcept {
    auto it = lower_bound_(px);
    if (it != entries_.end() && it->first == px) return it;
    return entries_.end();
  }

  Level& operator[](Price px) {
    auto it = lower_bound_(px);
    if (it != entries_.end() && it->first == px)
      return it->second;
    auto inserted = entries_.emplace(it, px);
    return inserted->second;
  }

  // erase() — O(1) when erasing the front (common case during sweep).
  // O(N) otherwise (rare: cancel of a non-best-price level).
  iterator erase(iterator it) {
    const bool is_front = (it == entries_.begin() + static_cast<std::ptrdiff_t>(front_offset_));
    if (is_front) {
      ++front_offset_;
      // Compact when dead prefix exceeds half the vector to bound memory.
      // For a K-level sweep this fires ~log2(K) times, each copying ~K/2
      // entries — total O(K log K) vs the original O(K²).
      if (front_offset_ > entries_.size() / 2 && front_offset_ > 8) {
        entries_.erase(entries_.begin(),
                       entries_.begin() + static_cast<std::ptrdiff_t>(front_offset_));
        front_offset_ = 0;
      }
      return entries_.begin() + static_cast<std::ptrdiff_t>(front_offset_);
    }
    return entries_.erase(it);
  }

  void reserve(std::size_t n) { entries_.reserve(n); }

  void clear() noexcept {
    entries_.clear();
    front_offset_ = 0;
  }

private:
  Cmp                cmp_{};
  std::vector<Entry> entries_;
  std::size_t        front_offset_{0};

  iterator lower_bound_(Price px) noexcept {
    return std::lower_bound(
        entries_.begin() + static_cast<std::ptrdiff_t>(front_offset_),
        entries_.end(), px,
        [this](const Entry& e, Price p) { return cmp_(e.first, p); });
  }

  const_iterator lower_bound_(Price px) const noexcept {
    return std::lower_bound(
        entries_.begin() + static_cast<std::ptrdiff_t>(front_offset_),
        entries_.end(), px,
        [this](const Entry& e, Price p) { return cmp_(e.first, p); });
  }
};

}  // namespace msim
