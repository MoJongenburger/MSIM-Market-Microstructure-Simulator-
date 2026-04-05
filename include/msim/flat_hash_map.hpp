#pragma once
// ============================================================
// include/msim/flat_hash_map.hpp
//
// v2 — backward-shift deletion, no tombstones.
//
// What went wrong in v1:
//   Tombstone-based deletion left dead slots in the probe
//   chain.  After each erase_locator() call during matching,
//   tombstones accumulated.  find() had to skip them on every
//   subsequent probe, causing BM_ProcessMarketOrder to regress
//   +22% and BM_BookAddRestingLimit to regress +39%.
//
// What v2 does differently:
//
//   1. No tombstones — only EMPTY (0) and OCCUPIED (1).
//      find() stops at the first EMPTY slot, no skipping.
//
//   2. Backward-shift deletion.  After erasing slot i, scan
//      forward (j = i+1, i+2, …) until an EMPTY slot.  For
//      each occupied slot j, if its natural position h would
//      have been placed at i or earlier (dist(h→i) < dist(h→j)),
//      shift it back to i.  This restores the linear-probe
//      invariant without tombstones.  O(1) amortised.
//
//   3. Default capacity 256 slots (6 KB) — typical benchmark
//      runs never trigger grow().  At 75% load that handles
//      192 simultaneous resting orders before the first grow.
//
//   4. insert_new(key) — for known-new keys (add_resting_limit
//      always inserts a fresh OrderId).  Skips the equality
//      check during the probe → one branch fewer per slot.
//
//   5. extract(key, out) — find + erase in one probe pass.
//      Used by cancel() so we don't hash twice.
//
// Slot layout at Locator = {Side(1)+pad(3)+Price(4)+u32(4)}:
//   key(8) + value(12) + state(1) + pad(3) = 24 bytes/slot
//   256 slots = 6 KB  (L1 cache on all modern CPUs is ≥32 KB)
// ============================================================

#include <cstddef>
#include <cstdint>
#include <vector>

namespace msim {

template <typename K, typename V>
class FlatHashMap {
public:
    static constexpr uint8_t EMPTY    = 0;
    static constexpr uint8_t OCCUPIED = 1;

    struct Slot {
        K       key{};
        V       value{};
        uint8_t state{EMPTY};
    };

    // Default: 256 slots, 75% load → handles 192 orders without grow.
    FlatHashMap() { init(256, 8); }

    // Allocate power-of-2 capacity with at most 75% load for n elements.
    void reserve(std::size_t n) {
        uint32_t log2 = 8;
        std::size_t cap = 256;
        while (cap * 3 < n * 4) { cap <<= 1u; ++log2; }
        init(cap, log2);
    }

    void max_load_factor(float) noexcept {}  // managed internally at 75%

    // ── insert or access (handles duplicate keys) ─────────────────────────
    V& operator[](K key) {
        if (size_ * 4 >= slots_.size() * 3) grow();
        std::size_t i = hash_idx(key);
        while (slots_[i].state) {
            if (slots_[i].key == key) return slots_[i].value;
            i = (i + 1) & mask_;
        }
        slots_[i] = {key, V{}, OCCUPIED};
        ++size_;
        return slots_[i].value;
    }

    // ── insert known-new key (no equality check in probe) ─────────────────
    // Use only when the key is guaranteed not already present.
    // add_resting_limit() always inserts a fresh OrderId → use this.
    V& insert_new(K key) {
        if (size_ * 4 >= slots_.size() * 3) grow();
        std::size_t i = hash_idx(key);
        while (slots_[i].state)           // stop at first EMPTY
            i = (i + 1) & mask_;
        slots_[i] = {key, V{}, OCCUPIED};
        ++size_;
        return slots_[i].value;
    }

    // ── find (returns pointer or nullptr) ─────────────────────────────────
    V* find(K key) noexcept {
        std::size_t i = hash_idx(key);
        while (slots_[i].state) {
            if (slots_[i].key == key) return &slots_[i].value;
            i = (i + 1) & mask_;
        }
        return nullptr;
    }

    const V* find(K key) const noexcept {
        std::size_t i = hash_idx(key);
        while (slots_[i].state) {
            if (slots_[i].key == key) return &slots_[i].value;
            i = (i + 1) & mask_;
        }
        return nullptr;
    }

    // ── extract: find + erase in one probe pass ───────────────────────────
    bool extract(K key, V& out) noexcept {
        std::size_t i = hash_idx(key);
        while (slots_[i].state) {
            if (slots_[i].key == key) {
                out = std::move(slots_[i].value);
                backward_shift(i);
                --size_;
                return true;
            }
            i = (i + 1) & mask_;
        }
        return false;
    }

    // ── erase by key ──────────────────────────────────────────────────────
    bool erase(K key) noexcept {
        std::size_t i = hash_idx(key);
        while (slots_[i].state) {
            if (slots_[i].key == key) {
                backward_shift(i);
                --size_;
                return true;
            }
            i = (i + 1) & mask_;
        }
        return false;
    }

    // ── clear (keep capacity) ─────────────────────────────────────────────
    void clear() noexcept {
        for (auto& s : slots_) s.state = EMPTY;
        size_ = 0;
    }

    std::size_t size()  const noexcept { return size_; }
    bool        empty() const noexcept { return size_ == 0; }

private:
    // ── Fibonacci hashing ─────────────────────────────────────────────────
    // 2^64 / φ ≈ 11400714819323198485.  Gives uniform distribution for
    // sequential uint64_t keys (OrderId = owner<<32 | seq).
    std::size_t hash_idx(K key) const noexcept {
        return static_cast<std::size_t>(
            static_cast<uint64_t>(key) * 11400714819323198485ULL
        ) >> (64u - log2cap_);
    }

    // ── Backward-shift deletion ───────────────────────────────────────────
    // Mark slot i as EMPTY, then scan forward.  For each occupied slot j,
    // if its natural position h has dist(h→i) < dist(h→j) — meaning the
    // probe chain from h passes through i before reaching j — move j to i.
    // This restores the probe-chain invariant without tombstones.
    void backward_shift(std::size_t i) noexcept {
        slots_[i].state = EMPTY;
        std::size_t j = (i + 1) & mask_;
        while (slots_[j].state) {
            const std::size_t h = hash_idx(slots_[j].key);
            if (((i - h) & mask_) < ((j - h) & mask_)) {
                slots_[i] = std::move(slots_[j]);
                slots_[j].state = EMPTY;
                i = j;
            }
            j = (j + 1) & mask_;
        }
    }

    void init(std::size_t cap, uint32_t log2) {
        slots_.assign(cap, Slot{});
        mask_    = cap - 1;
        size_    = 0;
        log2cap_ = log2;
    }

    // Double capacity, rebuild (called at 75% load — rare).
    void grow() {
        const uint32_t new_log2 = log2cap_ + 1;
        std::vector<Slot> old = std::move(slots_);
        init(std::size_t{1} << new_log2, new_log2);
        for (auto& s : old)
            if (s.state == OCCUPIED)
                insert_new(s.key) = std::move(s.value);
    }

    std::vector<Slot> slots_;
    std::size_t       mask_{255};
    std::size_t       size_{0};
    uint32_t          log2cap_{8};
};

} // namespace msim
