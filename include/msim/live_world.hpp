#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "msim/matching_engine.hpp"
#include "msim/order.hpp"
#include "msim/rules.hpp"
#include "msim/simulator.hpp" // BookTop
#include "msim/types.hpp"
#include "msim/world.hpp"     // IAgent, MarketView, AgentState, Action

namespace msim {

// -------- Live snapshots returned to HTTP layer --------
struct LiveMidPoint {
  Ts ts{};
  std::optional<Price> mid{};
};

struct LiveSnapshot {
  Ts ts{};
  std::optional<Price> best_bid{};
  std::optional<Price> best_ask{};
  std::optional<Price> mid{};
  std::optional<Price> last_trade{};
  std::vector<Trade> recent_trades{};
};

struct OrderAck {
  OrderId id{};
  OrderStatus status{OrderStatus::Rejected};
  RejectReason reject_reason{RejectReason::InvalidOrder};
};

struct LiveBookDepth {
  struct DepthLevel {
    Price price{};
    Qty qty{};
  };
  std::vector<DepthLevel> bids{};
  std::vector<DepthLevel> asks{};
};

class LiveWorld {
public:
  explicit LiveWorld(MatchingEngine engine);
  ~LiveWorld();

  LiveWorld(const LiveWorld&) = delete;
  LiveWorld& operator=(const LiveWorld&) = delete;

  void add_agent(std::unique_ptr<IAgent> a);

  void start(uint64_t seed, double horizon_seconds, WorldConfig cfg = {});
  void stop();

  // Read-only views used by the gateway
  LiveSnapshot snapshot(std::size_t max_trades) const;
  std::vector<LiveMidPoint> mid_series(Ts window_ns) const;
  LiveBookDepth book_depth(std::size_t levels) const;

  // Manual interaction
  OrderAck submit_order(Order o);
  bool cancel_order(OrderId id);
  bool modify_qty(OrderId id, Qty new_qty);

private:
  static std::optional<Price> compute_mid_(std::optional<Price> bb, std::optional<Price> ba) noexcept {
    if (!bb || !ba) return std::nullopt;
    return static_cast<Price>((*bb + *ba) / 2);
  }

  // ---- depth extraction helpers (robust to LevelSummary layout) ----
  template <class X>
  static LiveBookDepth::DepthLevel to_level_(const X& x) {
    // Your OrderBook::LevelSummary: { price, total_qty, order_count }
    if constexpr (requires { x.price; x.total_qty; }) {
      return LiveBookDepth::DepthLevel{
          static_cast<Price>(x.price),
          static_cast<Qty>(x.total_qty)
      };
    }
    // Struct with .price and .qty
    else if constexpr (requires { x.price; x.qty; }) {
      return LiveBookDepth::DepthLevel{static_cast<Price>(x.price), static_cast<Qty>(x.qty)};
    }
    // Struct with .px and .qty
    else if constexpr (requires { x.px; x.qty; }) {
      return LiveBookDepth::DepthLevel{static_cast<Price>(x.px), static_cast<Qty>(x.qty)};
    }
    // Struct with .price and .volume
    else if constexpr (requires { x.price; x.volume; }) {
      return LiveBookDepth::DepthLevel{static_cast<Price>(x.price), static_cast<Qty>(x.volume)};
    }
    // Pair-like
    else if constexpr (requires { x.first; x.second; }) {
      return LiveBookDepth::DepthLevel{static_cast<Price>(x.first), static_cast<Qty>(x.second)};
    }
    // Getter methods
    else if constexpr (requires { x.price(); x.qty(); }) {
      return LiveBookDepth::DepthLevel{static_cast<Price>(x.price()), static_cast<Qty>(x.qty())};
    } else {
      static_assert(sizeof(X) == 0, "Unsupported LevelSummary layout: add a to_level_ branch");
    }
  }

  template <class Book>
  static auto depth_levels_(const Book& book, Side side, std::size_t levels) {
    // Your OrderBook has: depth(Side, size_t)
    if constexpr (requires { book.depth(side, levels); }) {
      return book.depth(side, levels);
    } else if constexpr (requires { book.depth_levels(side, levels); }) {
      return book.depth_levels(side, levels);
    } else if constexpr (requires { book.levels(side, levels); }) {
      return book.levels(side, levels);
    } else {
      static_assert(sizeof(Book) == 0, "OrderBook depth API not found (expected depth/depth_levels/levels)");
    }
  }

  template <class Book>
  static std::vector<LiveBookDepth::DepthLevel> extract_depth_(const Book& book, Side side, std::size_t levels) {
    std::vector<LiveBookDepth::DepthLevel> out;
    auto lvls = depth_levels_(book, side, levels);
    out.reserve(lvls.size());
    for (const auto& x : lvls) out.push_back(to_level_(x));
    return out;
  }

  void worker_();

  void update_cache_with_engine_locked_(Ts ts, const std::vector<Trade>& new_trades);

  static OrderId next_order_id_(OwnerId owner, uint32_t seq) noexcept {
    const uint64_t hi = (static_cast<uint64_t>(owner) & 0xFFFF'FFFFull) << 32;
    const uint64_t lo = static_cast<uint64_t>(seq);
    return static_cast<OrderId>(hi | lo);
  }

private:
  mutable std::mutex engine_mtx_;
  MatchingEngine engine_;

  std::vector<std::unique_ptr<IAgent>> agents_;

  // worker lifecycle
  std::thread worker_thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_{false};
  std::atomic<Ts> cur_ts_{0};

  // config / horizon
  uint64_t seed_{1};
  double horizon_s_{3600.0};
  WorldConfig cfg_{};

  // manual order ids
  std::atomic<uint32_t> manual_seq_{1};

  // cached data for HTTP reads
  struct Cache {
    Ts ts{};
    std::optional<Price> best_bid{};
    std::optional<Price> best_ask{};
    std::optional<Price> mid{};
    std::optional<Price> last_trade{};

    std::deque<Trade> trades;   // recent trades
    std::deque<BookTop> tops;   // mid-series points
    LiveBookDepth depth;        // cached L2 depth
  };

  mutable std::mutex cache_mtx_;
  Cache cache_{};

  // caps (keep memory bounded)
  std::size_t max_cache_trades_{5000};
  std::size_t max_cache_tops_{200000};
  std::size_t depth_cache_levels_{20};
};

} // namespace msim
