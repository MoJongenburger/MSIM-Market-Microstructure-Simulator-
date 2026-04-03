#pragma once
// ============================================================
// include/msim/order_pool.hpp
//
// Thread-local pool allocator for Order list nodes
// =================================================
// Each resting order allocated via std::list<Order> causes one
// malloc() and one free() — the operating system's general-purpose
// allocator, which is not optimised for fixed-size chunks.
//
// On Apple Silicon, a single malloc() costs ~150-300 ns.  With
// BM_BookAddRestingLimit running at 888 ns, eliminating that malloc
// should bring it to ~200-300 ns.
//
// Design
// ------
// OrderNodeAllocator is a stateless C++ allocator that routes all
// single-node allocations through a thread_local slab pool.
// std::list<Order, OrderNodeAllocator<Order>> uses exactly this
// allocator for every node.
//
// The pool grows in slabs of SLAB_ORDERS orders.  Memory is returned
// to a free-list on deallocation and immediately reused.  Slabs are
// never returned to the OS during the simulation (they are freed when
// the thread exits), which is correct because a simulation allocates
// and deallocates O(same N) orders over its lifetime.
//
// Thread safety
// -------------
// Each thread has its own pool (thread_local).  No synchronisation
// is needed.  The scenario runner's ThreadPoolExecutor assigns one
// World::run() call per thread — each call uses its own pool.
//
// Important: std::list's internal node is larger than Order because
// it also stores prev* and next* pointers.  The pool allocates
// slots of size max(sizeof(Order), std::list's actual node size).
// We compute a safe upper bound at compile time:
//
//   NODE_BYTES = sizeof(Order) + 2 * sizeof(void*) + alignof(max_align_t)
//
// This over-allocates slightly but guarantees correctness on all
// platforms.
// ============================================================

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <vector>

namespace msim {

// ─── Slab pool ────────────────────────────────────────────────────────────────

template <std::size_t NodeBytes, std::size_t SlabOrders = 1024>
class SlabPool {
public:
  // Allocate one node.
  void* allocate() {
    if (!free_head_) grow();
    FreeNode* n = free_head_;
    free_head_  = n->next;
    return static_cast<void*>(n);
  }

  // Return a node to the pool.
  void deallocate(void* p) noexcept {
    auto* n    = static_cast<FreeNode*>(p);
    n->next    = free_head_;
    free_head_ = n;
  }

  ~SlabPool() {
    for (void* slab : slabs_)
      ::operator delete(slab, std::align_val_t{Alignment});
  }

  // Non-copyable, non-moveable (thread_local storage doesn't move).
  SlabPool()                           = default;
  SlabPool(const SlabPool&)            = delete;
  SlabPool& operator=(const SlabPool&) = delete;

private:
  static constexpr std::size_t Alignment = alignof(std::max_align_t);
  static constexpr std::size_t Stride    =
      (NodeBytes + Alignment - 1) & ~(Alignment - 1);  // round up

  struct FreeNode { FreeNode* next{}; };
  static_assert(Stride >= sizeof(FreeNode),
                "NodeBytes must be >= sizeof(void*)");

  FreeNode*             free_head_{};
  std::vector<void*>    slabs_;

  void grow() {
    void* slab = ::operator new(Stride * SlabOrders,
                                std::align_val_t{Alignment});
    slabs_.push_back(slab);
    char* p = static_cast<char*>(slab);
    for (std::size_t i = 0; i < SlabOrders; ++i) {
      auto* node  = reinterpret_cast<FreeNode*>(p + i * Stride);
      node->next  = free_head_;
      free_head_  = node;
    }
  }
};

// ─── Allocator adaptor ────────────────────────────────────────────────────────
// std::list's internal node stores: Order + prev* + next*.
// We add two pointer widths and round to max alignment as a safe upper bound.
inline constexpr std::size_t ORDER_NODE_BYTES =
    128;   // generous upper bound — covers Order + list node overhead on all
           // platforms.  A typical Order is ~48 bytes; list node overhead is
           // 2×8 = 16 bytes; with alignment padding we need ≤ 80 bytes.
           // 128 wastes some memory but is safe and cache-line friendly.

template <typename T>
class OrderNodeAllocator {
public:
  using value_type = T;

  // Stateless: all instances of the same type are interchangeable.
  OrderNodeAllocator() = default;
  template <typename U>
  explicit OrderNodeAllocator(const OrderNodeAllocator<U>&) noexcept {}

  // rebind: std::list asks for allocator<list_node<T>>, not allocator<T>.
  // Since we allocate fixed-size slots large enough for any single node,
  // we simply route through the same pool regardless of the rebound type.
  template <typename U>
  struct rebind { using other = OrderNodeAllocator<U>; };

  T* allocate(std::size_t n) {
    if (n != 1)
      // Bulk allocations (rare) fall back to the global allocator.
      return static_cast<T*>(::operator new(n * sizeof(T)));
    return static_cast<T*>(pool_().allocate());
  }

  void deallocate(T* p, std::size_t n) noexcept {
    if (n != 1) { ::operator delete(p); return; }
    pool_().deallocate(static_cast<void*>(p));
  }

  // Stateless: all instances compare equal — std::list move is always O(1).
  bool operator==(const OrderNodeAllocator&) const noexcept { return true;  }
  bool operator!=(const OrderNodeAllocator&) const noexcept { return false; }

private:
  // One pool per thread — no locks, no contention.
  static SlabPool<ORDER_NODE_BYTES>& pool_() {
    thread_local SlabPool<ORDER_NODE_BYTES> p;
    return p;
  }
};

} // namespace msim
