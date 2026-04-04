#pragma once
// ============================================================
// include/msim/small_vector.hpp
//
// SmallVector<T, N>
// =================
// std::vector replacement with N slots of inline stack storage.
// Falls back to heap only when size exceeds N.
//
// Why this matters for MatchResult::trades
// -----------------------------------------
// Every call to engine_.process() constructs a MatchResult whose
// trades field is std::vector<Trade>.  std::vector always heap-
// allocates, even when 0 or 1 trades occur — which is true for
// > 95% of orders (resting limits, cancels, rejected orders,
// IOC partial fills).
//
// With SmallVector<Trade, 4>:
//   0–4 trades  → zero heap allocations, data lives on the stack
//                 inside the MatchResult (~160 bytes of inline storage)
//   5+ trades   → one heap allocation, same as std::vector
//                 (multi-level sweeps only)
//
// Trade is a trivially-copyable POD (Price, Qty, OrderId, Ts —
// all primitives).  SmallVector detects this at compile time and
// uses memcpy instead of placement-new, which the compiler
// optimises to register moves for the 1-trade case.
//
// API subset implemented
// -----------------------
// push_back, clear, empty, size, capacity,
// begin/end (raw T* — valid as InputIterator),
// front, back, operator[],
// insert(end(), InputIt, InputIt)  — append-only,
// move constructor + move assignment (no copy).
// ============================================================

#include <cstddef>
#include <cstring>
#include <limits>
#include <type_traits>

namespace msim {

template <typename T, std::size_t N>
class SmallVector {
  static_assert(N > 0, "SmallVector inline capacity must be > 0");

 public:
  // ── Construction / destruction ────────────────────────────────────────────
  SmallVector() noexcept : data_(inline_ptr()), size_(0), cap_(N), heap_(false) {}

  ~SmallVector() {
    destroy_range(data_, size_);
    if (heap_) ::operator delete(data_);
  }

  // Move constructor — steals heap buffer, or copies inline data.
  SmallVector(SmallVector&& o) noexcept
      : size_(o.size_), cap_(o.cap_), heap_(o.heap_) {
    if (o.heap_) {
      data_   = o.data_;
      o.data_ = o.inline_ptr();
      o.size_ = 0;
      o.cap_  = N;
      o.heap_ = false;
    } else {
      data_ = inline_ptr();
      copy_trivial(data_, o.data_, size_);
      o.size_ = 0;
    }
  }

  // Move assignment — handles self-assignment, steals or copies.
  SmallVector& operator=(SmallVector&& o) noexcept {
    if (this == &o) return *this;
    destroy_range(data_, size_);
    if (heap_) ::operator delete(data_);

    size_ = o.size_;
    cap_  = o.cap_;
    heap_ = o.heap_;

    if (o.heap_) {
      data_   = o.data_;
      o.data_ = o.inline_ptr();
      o.size_ = 0;
      o.cap_  = N;
      o.heap_ = false;
    } else {
      data_ = inline_ptr();
      copy_trivial(data_, o.data_, size_);
      o.size_ = 0;
    }
    return *this;
  }

  SmallVector(const SmallVector&)            = delete;
  SmallVector& operator=(const SmallVector&) = delete;

  // ── Capacity ──────────────────────────────────────────────────────────────
  bool        empty()    const noexcept { return size_ == 0; }
  std::size_t size()     const noexcept { return size_; }
  std::size_t capacity() const noexcept { return cap_; }

  // ── Element access ────────────────────────────────────────────────────────
  T*       begin()       noexcept { return data_; }
  T*       end()         noexcept { return data_ + size_; }
  const T* begin() const noexcept { return data_; }
  const T* end()   const noexcept { return data_ + size_; }

  T&       front()       noexcept { return data_[0]; }
  const T& front() const noexcept { return data_[0]; }
  T&       back()        noexcept { return data_[size_ - 1]; }
  const T& back()  const noexcept { return data_[size_ - 1]; }

  T&       operator[](std::size_t i)       noexcept { return data_[i]; }
  const T& operator[](std::size_t i) const noexcept { return data_[i]; }

  // ── Modifiers ─────────────────────────────────────────────────────────────

  // push_back — O(1) amortised.  No heap allocation while size <= N.
  void push_back(const T& v) {
    if (size_ == cap_) grow();
    construct_one(data_ + size_, v);
    ++size_;
  }

  void push_back(T&& v) {
    if (size_ == cap_) grow();
    construct_one(data_ + size_, std::move(v));
    ++size_;
  }

  // clear — destroys elements but retains heap capacity.
  // Called by process_into() between orders so the heap buffer
  // (once allocated on a sweep) is never freed mid-run.
  void clear() noexcept {
    destroy_range(data_, size_);
    size_ = 0;
  }

  // insert(end(), first, last) — append range from any input iterator.
  // pos must be end(); only append is supported.
  template <typename InputIt>
  void insert(T* /*pos*/, InputIt first, InputIt last) {
    for (; first != last; ++first) push_back(*first);
  }

 private:
  alignas(alignof(T)) std::byte storage_[N * sizeof(T)];
  T*          data_;
  std::size_t size_;
  std::size_t cap_;
  bool        heap_;

  T*       inline_ptr()       noexcept { return reinterpret_cast<T*>(storage_); }
  const T* inline_ptr() const noexcept { return reinterpret_cast<const T*>(storage_); }

  // For trivially-copyable T (Trade is POD): use memcpy.
  // The compiler optimises small N to register moves at -O2.
  static void copy_trivial(T* dst, const T* src, std::size_t n) noexcept {
    if constexpr (std::is_trivially_copyable_v<T>) {
      if (n) std::memcpy(dst, src, n * sizeof(T));
    } else {
      for (std::size_t i = 0; i < n; ++i)
        new (dst + i) T(std::move(const_cast<T&>(src[i])));
    }
  }

  static void construct_one(T* dst, const T& v) {
    if constexpr (std::is_trivially_copyable_v<T>)
      std::memcpy(dst, &v, sizeof(T));
    else
      new (dst) T(v);
  }

  static void construct_one(T* dst, T&& v) {
    if constexpr (std::is_trivially_copyable_v<T>)
      std::memcpy(dst, &v, sizeof(T));
    else
      new (dst) T(std::move(v));
  }

  static void destroy_range(T* p, std::size_t n) noexcept {
    if constexpr (!std::is_trivially_destructible_v<T>)
      for (std::size_t i = 0; i < n; ++i) p[i].~T();
  }

  // grow — double capacity, move to heap.
  void grow() {
    const std::size_t new_cap = cap_ * 2;
    T* new_data = static_cast<T*>(::operator new(new_cap * sizeof(T)));
    copy_trivial(new_data, data_, size_);
    destroy_range(data_, size_);
    if (heap_) ::operator delete(data_);
    data_ = new_data;
    cap_  = new_cap;
    heap_ = true;
  }
};

}  // namespace msim
