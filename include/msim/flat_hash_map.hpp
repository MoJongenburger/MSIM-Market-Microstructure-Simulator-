#pragma once
// ============================================================
// include/msim/flat_hash_map.hpp
//
// Open-addressing flat hash map with linear probing.
//
// Why this beats std::unordered_map for loc_ in OrderBook:
//
//   std::unordered_map uses separate chaining (bucket array of
//   linked lists).  Every lookup chases a pointer from the
//   bucket array into a heap-allocated node — 1–2 cache misses
//   per operation.  For Cancel/Add/Modify (which each do one
//   loc_ lookup) this is 40–80 ns of pure memory latency.
//
//   FlatHashMap stores keys, values, and state bytes together
//   in one contiguous std::vector.  At 512 slots × 24 bytes
//   = 12 KB — the entire map fits in L1 cache.  Lookup probes
//   at most a handful of consecutive cache lines.  Zero pointer
//   indirection.
//
// Design:
//   - Fibonacci hashing  (key × 2^64/φ >> (64 - log2_cap))
//     gives uniform distribution for sequential uint64_t keys.
//   - Linear probing     (simple, cache-friendly, fast in L1).
//   - Tombstone deletion (state byte: EMPTY / OCCUPIED / TOMB).
//   - extract(key, out)  — find + erase in one pass (used by
//     cancel so we don't hash twice).
//   - grow()             — doubles capacity, rebuilds without
//     tombstones; called automatically at 50% load.
//   - max_load_factor()  — no-op stub so drop-in API matches
//     std::unordered_map call sites.
// ============================================================

#include <cstddef>
#include <cstdint>
#include <vector>

namespace msim {

template <typename K, typename V>
class FlatHashMap {
public:
    // ── Slot ─────────────────────────────────────────────────────────────
    // Packed to 24 bytes for OrderId(8) + Locator(12) + state(1) + pad(3).
    // 512 slots = 12 KB — fits in L1 on all modern CPUs.
    static constexpr uint8_t EMPTY     = 0;
    static constexpr uint8_t OCCUPIED  = 1;
    static constexpr uint8_t TOMBSTONE = 2;

    struct Slot {
        K       key{};
        V       value{};
        uint8_t state{EMPTY};
    };

    // ── Capacity hint ─────────────────────────────────────────────────────
    // Allocate power-of-2 capacity >= n * 2 (50% load factor).
    void reserve(std::size_t n) {
        log2cap_ = 4u;
        std::size_t cap = 16;
        while (cap < n * 2) { cap <<= 1u; ++log2cap_; }
        slots_.assign(cap, Slot{});
        mask_       = cap - 1;
        size_       = 0;
        tombstones_ = 0;
    }

    // No-op stub — we manage load factor internally.
    void max_load_factor(float) noexcept {}

    // ── operator[] ────────────────────────────────────────────────────────
    // Insert-or-access.  Reuses tombstone slot if one was encountered first.
    V& operator[](K key) {
        if ((size_ + tombstones_ + 1) * 2 > slots_.size()) grow();
        const std::size_t start = hash_idx(key);
        std::size_t i           = start;
        std::size_t first_tomb  = npos;
        while (slots_[i].state != EMPTY) {
            if (slots_[i].state == OCCUPIED && slots_[i].key == key)
                return slots_[i].value;
            if (slots_[i].state == TOMBSTONE && first_tomb == npos)
                first_tomb = i;
            i = (i + 1) & mask_;
        }
        const std::size_t pos = (first_tomb != npos) ? first_tomb : i;
        if (slots_[pos].state == TOMBSTONE) --tombstones_;
        slots_[pos].key   = key;
        slots_[pos].value = V{};
        slots_[pos].state = OCCUPIED;
        ++size_;
        return slots_[pos].value;
    }

    // ── find ──────────────────────────────────────────────────────────────
    // Returns pointer to value, or nullptr.  Skips tombstones.
    V* find(K key) noexcept {
        std::size_t i = hash_idx(key);
        while (slots_[i].state != EMPTY) {
            if (slots_[i].state == OCCUPIED && slots_[i].key == key)
                return &slots_[i].value;
            i = (i + 1) & mask_;
        }
        return nullptr;
    }

    const V* find(K key) const noexcept {
        std::size_t i = hash_idx(key);
        while (slots_[i].state != EMPTY) {
            if (slots_[i].state == OCCUPIED && slots_[i].key == key)
                return &slots_[i].value;
            i = (i + 1) & mask_;
        }
        return nullptr;
    }

    // ── extract ───────────────────────────────────────────────────────────
    // Find + erase in a single pass.  Used by cancel() so we don't hash
    // twice.  Returns true and moves the value into `out` on success.
    bool extract(K key, V& out) noexcept {
        std::size_t i = hash_idx(key);
        while (slots_[i].state != EMPTY) {
            if (slots_[i].state == OCCUPIED && slots_[i].key == key) {
                out             = std::move(slots_[i].value);
                slots_[i].state = TOMBSTONE;
                --size_;
                ++tombstones_;
                return true;
            }
            i = (i + 1) & mask_;
        }
        return false;
    }

    // ── erase ─────────────────────────────────────────────────────────────
    bool erase(K key) noexcept {
        std::size_t i = hash_idx(key);
        while (slots_[i].state != EMPTY) {
            if (slots_[i].state == OCCUPIED && slots_[i].key == key) {
                slots_[i].state = TOMBSTONE;
                --size_;
                ++tombstones_;
                return true;
            }
            i = (i + 1) & mask_;
        }
        return false;
    }

    // ── clear ─────────────────────────────────────────────────────────────
    // Resets all slots to EMPTY without releasing memory.
    void clear() noexcept {
        for (auto& s : slots_) s.state = EMPTY;
        size_       = 0;
        tombstones_ = 0;
    }

    std::size_t size()  const noexcept { return size_; }
    bool        empty() const noexcept { return size_ == 0; }

private:
    static constexpr std::size_t npos = std::size_t(-1);

    // Fibonacci hashing — excellent distribution for sequential uint64_t.
    // 11400714819323198485 ≈ 2^64 / φ
    std::size_t hash_idx(K key) const noexcept {
        return static_cast<std::size_t>(
            static_cast<uint64_t>(key) * 11400714819323198485ULL
        ) >> (64u - log2cap_);
    }

    // Double capacity, rebuild without tombstones.
    void grow() {
        ++log2cap_;
        std::vector<Slot> old = std::move(slots_);
        slots_.assign(std::size_t{1} << log2cap_, Slot{});
        mask_       = slots_.size() - 1;
        size_       = 0;
        tombstones_ = 0;
        for (auto& s : old)
            if (s.state == OCCUPIED)
                (*this)[s.key] = std::move(s.value);
    }

    std::vector<Slot> slots_;
    std::size_t       mask_{0};
    std::size_t       size_{0};
    std::size_t       tombstones_{0};
    uint32_t          log2cap_{4};
};

} // namespace msim
