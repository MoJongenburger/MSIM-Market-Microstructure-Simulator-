#pragma once
// ============================================================
// include/msim/flat_price_map.hpp
// ============================================================

#include <algorithm>
#include <functional>
#include <vector>

#include "msim/types.hpp"

namespace msim {

template <typename Level,
          typename Cmp = std::less<Price>>
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

  iterator       begin()        noexcept { return entries_.begin(); }
  iterator       end()          noexcept { return entries_.end();   }
  const_iterator begin()  const noexcept { return entries_.begin(); }
  const_iterator end()    const noexcept { return entries_.end();   }

  bool        empty() const noexcept { return entries_.empty(); }
  std::size_t size()  const noexcept { return entries_.size();  }

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

  iterator erase(iterator it) { return entries_.erase(it); }

  void reserve(std::size_t n) { entries_.reserve(n); }

  // Called by MatchingEngine::maybe_trigger_circuit_breaker()
  void clear() noexcept { entries_.clear(); }

private:
  Cmp                cmp_{};
  std::vector<Entry> entries_;

  iterator lower_bound_(Price px) noexcept {
    return std::lower_bound(
        entries_.begin(), entries_.end(), px,
        [this](const Entry& e, Price p) { return cmp_(e.first, p); });
  }

  const_iterator lower_bound_(Price px) const noexcept {
    return std::lower_bound(
        entries_.begin(), entries_.end(), px,
        [this](const Entry& e, Price p) { return cmp_(e.first, p); });
  }
};

} // namespace msim
