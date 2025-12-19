#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "msim/ledger.hpp"
#include "msim/matching_engine.hpp"
#include "msim/simulator.hpp" // BookTop + midprice()
#include "msim/world.hpp"

// agents (used internally for live simulation)
#include "msim/agents/noise_trader.hpp"
#include "msim/agents/market_maker.hpp"

namespace msim {

class LiveWorld {
public:
  struct DepthLevel {
    Price px{};
    Qty qty{};
  };

  struct BookDepth {
    std::vector<DepthLevel> bids; // best -> worse
    std::vector<DepthLevel> asks; // best -> worse
  };

  struct Snapshot {
    Ts ts{};
    std::optional<Price> best_bid{};
    std::optional<Price> best_ask{};
    std::optional<Price> mid{};
    std::optional<Price> last_trade{};
  };

  LiveWorld(MatchingEngine engine, WorldConfig cfg, uint64_t seed, double horizon_seconds);
  ~LiveWorld();

  void start();
  void stop();

  // Lightweight “current market” snapshot
  Snapshot snapshot() const;

  // Recent trades (newest-first)
  std::vector<Trade> snapshot_recent_trades(std::size_t limit) const;

  // Mid series as BookTop points (oldest-first, last N points)
  std::vector<BookTop> snapshot_top_points(std::size_t points) const;

  // L2 depth (top-N levels)
  BookDepth snapshot_depth(std::size_t levels) const;

  // Manual trading API (used by HTTP gateway)
  OrderAck submit_order(Order o);
  bool cancel_order(OrderId id);
  bool modify_qty(OrderId id, Qty new_qty);

private:
  static uint64_t splitmix64(uint64_t& x) noexcept;

  void loop_();

  OrderId make_scoped_id_(OwnerId owner);

  // depth extraction that tolerates different book APIs
  template <class X>
  static DepthLevel to_level_(const X& x) {
    if constexpr (requires { x.price; x.qty; }) {
      return DepthLevel{static_cast<Price>(x.price), static_cast<Qty>(x.qty)};
    } else if constexpr (requires { x.px; x.qty; }) {
      return DepthLevel{static_cast<Price>(x.px), static_cast<Qty>(x.qty)};
    } else {
      return DepthLevel{static_cast<Price>(x.first), static_cast<Qty>(x.second)};
    }
  }

  template <class Book>
  static std::vector<DepthLevel> extract_depth_(const Book& book, Side side, std::size_t n) {
    std::vector<DepthLevel> out;
    out.reserve(n);

    // Try a few common APIs via C++20 requires (only the valid one compiles in)
    if constexpr (requires { book.depth(side, n); }) {
      auto d = book.depth(side, n);
      for (const auto& x : d) {
        out.push_back(to_level_(x));
        if (out.size() >= n) break;
      }
      return out;
    } else if constexpr (requires { book.depth(side); }) {
      auto d = book.depth(side);
      for (const auto& x : d) {
        out.push_back(to_level_(x));
        if (out.size() >= n) break;
      }
      return out;
    } else if constexpr (requires { book.depth_top_n(side, n); }) {
      auto d = book.depth_top_n(side, n);
      for (const auto& x : d) {
        out.push_back(to_level_(x));
        if (out.size() >= n) break;
      }
      return out;
    } else if constexpr (requires { book.top_levels(side, n); }) {
      auto d = book.top_levels(side, n);
      for (const auto& x : d) {
        out.push_back(to_level_(x));
        if (out.size() >= n) break;
      }
      return out;
    } else {
      // If none exists, return empty (UI will just show blanks).
      return out;
    }
  }

private:
  // config
  WorldConfig cfg_{};
  uint64_t seed_{1};
  Ts t_end_{0};

  // engine + sim state (guarded by mu_)
  mutable std::mutex mu_;
  MatchingEngine engine_;
  Ts ts_{0};

  std::deque<Trade> trades_;   // newest-first
  std::deque<BookTop> tops_;   // oldest-first

  std::unordered_map<OrderId, OrderMeta> order_meta_;
  std::unordered_map<OwnerId, Account> accounts_;

  // agents
  std::vector<std::unique_ptr<IAgent>> agents_;
  uint32_t local_seq_{1};

  // thread
  std::atomic<bool> running_{false};
  std::thread worker_;
};

} // namespace msim
