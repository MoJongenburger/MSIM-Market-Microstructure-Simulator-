#pragma once
// ============================================================
// include/msim/flat_hash_map.hpp
//
// v3 — Robin Hood hashing.
//
// Why Robin Hood beats v2 (backward-shift linear probing):
//
//   v2 stored no metadata per slot.  find() had to probe until
//   an EMPTY slot; erase's backward-shift scanned until EMPTY.
//   At 39% load the average probe chain was ~1.6 slots, but
//   the worst-case chains caused high variance (65 ns p99 on
//   throughput, 145 ns on cancel).
//
//   Robin Hood stores the probe distance d in each slot (1 byte).
//   d = 0 → slot is EMPTY.  d ≥ 1 → occupant is d-1 hops from
//   its natural position.
//
//   find() early-termination: if slots_[i].dist < d, then no
//   element probing to this position would have reached here
//   (Robin Hood would have displaced it earlier), so the key
//   is absent.  Stop immediately instead of scanning to EMPTY.
//
//   erase backward-shift: only shifts slots with dist > 1.
//   A slot with dist = 1 is already at its natural position —
//   it cannot move back.  This terminates much sooner than v2.
//
//   insert (Robin Hood steal): when probing past a slot with
//   lower dist, swap the incoming element with the occupant
//   ("rich gives way to poor").  This keeps probe chains short
//   and uniform even at high load.
//
// Slot layout — 24 bytes (same as v2):
//   key(8) + Locator(12) + dist(1) + pad(3)
//   256 slots = 6 KB — fits in L1 (48 KB on test machine).
// ============================================================

#include <cstddef>
#include <cstdint>
#include <vector>

namespace msim {

template <typename K, typename V>
class FlatHashMap {
public:
    // dist = 0 → EMPTY.  dist ≥ 1 → probe distance from natural position.
    struct Slot {
        K       key{};
        V       value{};
        uint8_t dist{0};
    };

    // Default: 256 slots (50% load → 128 orders before grow).
    FlatHashMap() { init(256, 8); }

    // Allocate ≥ n*2 slots (keeps load ≤ 50%).
    void reserve(std::size_t n) {
        uint32_t log2 = 8;
        std::size_t cap = 256;
        while (cap < n * 2) { cap <<= 1u; ++log2; }
        init(cap, log2);
    }

    void max_load_factor(float) noexcept {}  // managed internally

    // ── insert known-new key (no duplicate check in probe) ────────────────
    // Use when the key is guaranteed fresh. add_resting_limit always
    // inserts a new OrderId — this skips the equality comparison per slot.
    void insert_new(K key, V value) {
        if ((size_ + 1) * 2 >= slots_.size()) grow();
        rh_insert(std::move(key), std::move(value));
    }

    // ── operator[] (insert-or-access, handles duplicates) ─────────────────
    V& operator[](K key) {
        if (auto* p = find(key)) return *p;
        if ((size_ + 1) * 2 >= slots_.size()) grow();
        rh_insert(key, V{});
        return *find(key);  // second find is safe — key is now present
    }

    // ── find: early termination via Robin Hood dist invariant ─────────────
    // If slots_[i].dist < d, key cannot exist further in the chain.
    V* find(K key) noexcept {
        uint8_t d = 1;
        std::size_t i = hash_idx(key);
        while (slots_[i].dist >= d) {
            if (slots_[i].key == key) return &slots_[i].value;
            i = (i + 1) & mask_;
            ++d;
        }
        return nullptr;
    }

    const V* find(K key) const noexcept {
        uint8_t d = 1;
        std::size_t i = hash_idx(key);
        while (slots_[i].dist >= d) {
            if (slots_[i].key == key) return &slots_[i].value;
            i = (i + 1) & mask_;
            ++d;
        }
        return nullptr;
    }

    // ── extract: find + erase in one probe pass ───────────────────────────
    bool extract(K key, V& out) noexcept {
        uint8_t d = 1;
        std::size_t i = hash_idx(key);
        while (slots_[i].dist >= d) {
            if (slots_[i].key == key) {
                out = std::move(slots_[i].value);
                erase_at(i);
                return true;
            }
            i = (i + 1) & mask_;
            ++d;
        }
        return false;
    }

    // ── erase by key ──────────────────────────────────────────────────────
    bool erase(K key) noexcept {
        uint8_t d = 1;
        std::size_t i = hash_idx(key);
        while (slots_[i].dist >= d) {
            if (slots_[i].key == key) {
                erase_at(i);
                return true;
            }
            i = (i + 1) & mask_;
            ++d;
        }
        return false;
    }

    void clear() noexcept {
        for (auto& s : slots_) s.dist = 0;
        size_ = 0;
    }

    std::size_t size()  const noexcept { return size_; }
    bool        empty() const noexcept { return size_ == 0; }

private:
    // Fibonacci hashing — uniform distribution for sequential uint64_t.
    std::size_t hash_idx(K key) const noexcept {
        return static_cast<std::size_t>(
            static_cast<uint64_t>(key) * 11400714819323198485ULL
        ) >> (64u - log2cap_);
    }

    // Robin Hood insertion: displace elements with shorter probe distance.
    // Keeps probe chains short and uniform ("steal from the rich").
    void rh_insert(K key, V value) noexcept {
        uint8_t d = 1;
        std::size_t i = hash_idx(key);
        while (slots_[i].dist != 0) {
            if (slots_[i].dist < d) {
                // Current occupant is closer to home — swap (Robin Hood steal).
                std::swap(slots_[i].key,   key);
                std::swap(slots_[i].value, value);
                std::swap(slots_[i].dist,  d);
            }
            i = (i + 1) & mask_;
            ++d;
        }
        slots_[i] = {std::move(key), std::move(value), d};
        ++size_;
    }

    // Robin Hood backward-shift deletion.
    // Shift back only slots with dist > 1 (already displaced from home).
    // A slot with dist == 1 is at its natural position — cannot move back.
    // This terminates much sooner than the general backward-shift in v2.
    void erase_at(std::size_t i) noexcept {
        slots_[i].dist = 0;
        --size_;
        std::size_t j = (i + 1) & mask_;
        while (slots_[j].dist > 1) {
            slots_[i] = std::move(slots_[j]);
            slots_[i].dist--;   // one hop closer to natural position
            slots_[j].dist = 0;
            i = j;
            j = (j + 1) & mask_;
        }
    }

    void init(std::size_t cap, uint32_t log2) {
        slots_.assign(cap, Slot{});
        mask_    = cap - 1;
        size_    = 0;
        log2cap_ = log2;
    }

    void grow() {
        const uint32_t new_log2 = log2cap_ + 1;
        std::vector<Slot> old = std::move(slots_);
        init(std::size_t{1} << new_log2, new_log2);
        for (auto& s : old)
            if (s.dist != 0)
                rh_insert(std::move(s.key), std::move(s.value));
    }

    std::vector<Slot> slots_;
    std::size_t       mask_{255};
    std::size_t       size_{0};
    uint32_t          log2cap_{8};
};

} // namespace msim
