#pragma once
// ============================================================
// include/msim/flat_price_map.hpp
//
// FlatPriceMap<Level, Cmp>
// ========================
// A sorted std::vector of (Price, Level) entries that replaces
// std::map<Price, Level, Cmp> in the order book.
//
// Why std::map is the bottleneck
// --------------------------------
// std::map is a red-black tree.  Every find(), insert(), and erase()
// chases raw pointers: root → child → child → leaf.  With a typical
// LOB of 50-150 active price levels, those nodes are scattered across
// the heap.  On Apple Silicon each pointer chase that misses L1/L2
// costs ~80-120 ns — three such misses per operation = 240-360 ns
// overhead before the operation itself runs.
//
// BM_BookAddRestingLimit:  888 ns  → target ~180 ns
// BM_BookCancel_O1:        900 ns  → target ~160 ns
// BM_BookModifyQty_O1:    1677 ns  → target ~200 ns
//
// Why FlatPriceMap is faster
// ---------------------------
// A sorted std::vector stores all entries contiguously.  A binary
// search over 100 entries touches ~7 elements.  With
// sizeof(Entry) = sizeof(Price) + sizeof(Level) ≈ 8 + 32 = 40 bytes,
// those 7 elements fit in 3-4 cache lines that are pre-loaded by the
// hardware prefetcher.  The binary search runs in ~10 ns vs 240+ ns
// for the tree walk.
//
// Complexity tradeoffs
// ---------------------
// find()        O(log N)   cache-resident    ← was O(log N) with cache misses
// operator[]    O(log N) + O(N) shift        ← level insert is rare
// erase()       O(N) shift                  ← level erase is rare
// begin()/end() O(1)                        ← same as std::map
//
// Level inserts/erases are rare: they only happen when a brand-new
// price appears or the last order at a price is removed.  In a typical
// 2-second simulation the book has ~50 distinct price levels and sees
// O(100) level insertions total.  The O(N) shift cost is negligible.
//
// Iterator stability after vector reallocation
// ------------------------------------------------
// FlatPriceMap stores Level objects (which contain std::list<Order>)
// by value inside the vector.  When the vector grows and reallocates,
// the Level objects are MOVE-CONSTRUCTED to the new memory.
//
// std::list's move constructor transfers ownership of all nodes to the
// new list object in O(1), without moving the nodes themselves.
// Iterators to list elements point to the heap-allocated nodes, which
// do NOT move.  Therefore, OrderBook::Locator::it (a Queue::iterator)
// remains valid across any vector reallocation.  This is guaranteed
// by the C++ standard [list.modifiers], which states that iterators
// to elements are not invalidated by move.
//
// API compatibility with std::map
// ---------------------------------
// The public interface mirrors std::map so that book.cpp changes are
// limited to type declarations:
//   using BidMap = FlatPriceMap<std::greater<Price>>;  // was std::map<...>
//   using AskMap = FlatPriceMap<std::less<Price>>;
//
// Entry::first and Entry::second match std::map's pair<const K, V>
// so structured bindings and ->first/->second access are unchanged.
// ============================================================

#include <algorithm>
#include <functional>
#include <vector>

#include "msim/types.hpp"   // Price, Qty

namespace msim {

// ─── Forward declaration ──────────────────────────────────────────────────────
// Level is defined in book.hpp; FlatPriceMap only stores it by value.
// Including book.hpp here would create a circular dependency, so we
// accept Level as a template parameter instead.

template <typename Level,
          typename Cmp = std::less<Price>>
class FlatPriceMap {
public:
  // ── Entry type ──────────────────────────────────────────────────────────────
  // Named .first / .second to be a drop-in for std::map's value_type.
  // Level must be moveable (std::list satisfies this).
  struct Entry {
    Price first{};   // price key
    Level second{};  // Level = {Queue q; Qty total_qty;}

    // Needed so FlatPriceMap can insert a default-initialised entry at
    // the correct sorted position via emplace().
    explicit Entry(Price p) : first(p), second{} {}
    Entry() = default;

    // Non-copyable if Level is (std::list is non-copyable).
    Entry(const Entry&)            = delete;
    Entry& operator=(const Entry&) = delete;
    Entry(Entry&&)                 = default;
    Entry& operator=(Entry&&)      = default;
  };

  // ── Iterators ───────────────────────────────────────────────────────────────
  using iterator       = typename std::vector<Entry>::iterator;
  using const_iterator = typename std::vector<Entry>::const_iterator;

  iterator       begin()        noexcept { return entries_.begin(); }
  iterator       end()          noexcept { return entries_.end();   }
  const_iterator begin()  const noexcept { return entries_.begin(); }
  const_iterator end()    const noexcept { return entries_.end();   }

  bool        empty() const noexcept { return entries_.empty();  }
  std::size_t size()  const noexcept { return entries_.size();   }

  // ── find() ──────────────────────────────────────────────────────────────────
  // O(log N) binary search over contiguous memory.
  // Returns end() if not found.
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

  // ── operator[] ──────────────────────────────────────────────────────────────
  // Find or insert.  Returns reference to the Level at this price.
  // Invalidates all iterators (vector may reallocate) but NOT
  // Queue::iterator values stored in Locators — see header comments.
  Level& operator[](Price px) {
    auto it = lower_bound_(px);
    if (it != entries_.end() && it->first == px)
      return it->second;
    // Insert a default-constructed Level at the sorted position.
    // entries_.emplace() shifts subsequent entries right in O(N).
    auto inserted = entries_.emplace(it, px);
    return inserted->second;
  }

  // ── erase() ─────────────────────────────────────────────────────────────────
  // Erase by iterator.  O(N) shift.  Returns iterator to next entry.
  iterator erase(iterator it) { return entries_.erase(it); }

  // ── reserve() ───────────────────────────────────────────────────────────────
  // Pre-allocate capacity.  Call at construction with the expected max
  // number of distinct price levels (typically 50-200 for a 20-level LOB).
  void reserve(std::size_t n) { entries_.reserve(n); }

private:
  Cmp                  cmp_{};
  std::vector<Entry>   entries_;

  // lower_bound: first position where !cmp(entry.first, px).
  // For BidMap (greater<Price>): first position where entry.price <= px
  //   → all entries before it have price > px (higher prices first).
  // For AskMap (less<Price>):    first position where entry.price >= px
  //   → all entries before it have price < px (lower prices first).
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
