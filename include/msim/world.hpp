#pragma once
// ============================================================
// include/msim/world.hpp
// ============================================================

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "msim/matching_engine.hpp"
#include "msim/simulator.hpp"
#include "msim/ledger.hpp"
#include "msim/latency_model.hpp"
#include "msim/stylized_facts.hpp"
#include "msim/tca.hpp"

namespace msim {

// ─── MarketView ───────────────────────────────────────────────────────────────
struct MarketView {
  Ts ts{};
  std::optional<Price> best_bid{};
  std::optional<Price> best_ask{};
  std::optional<Price> mid{};
  std::optional<Price> last_trade{};

  // Top-of-book quantities and derived imbalance.
  // imbalance = (bid_qty - ask_qty) / (bid_qty + ask_qty), in [-1, 1].
  Qty    bid_depth = 0;
  Qty    ask_depth = 0;
  double imbalance = 0.0;
};

// ─── QueuePosition ────────────────────────────────────────────────────────────
// Describes where one of an agent's resting limit orders sits in the book.
// Delivered inside AgentState::queue_positions every step.
//
// The key fields for strategy decisions:
//
//   qty_ahead       — lots that must fill before this order is reached.
//                     0 = this order is at the front of the queue (next to fill).
//
//   qty_behind      — lots resting behind this order at the same price.
//                     A large qty_behind means the level has staying power;
//                     a small one means the level will disappear if hit.
//
//   level_total     — total qty at this price = qty_ahead + own_qty + qty_behind.
//
//   own_qty         — remaining quantity of this order.
//
//   position_index  — 0-based FIFO index.  0 = next to fill.
//
// Example decision rule for a market maker:
//   if (qp.qty_ahead == 0 && spread_narrowing)
//       cancel_and_requote_further();   // at front, about to be adversely selected
//   if (qp.qty_ahead > 2 * avg_trade_size)
//       relax();                        // deep in queue, unlikely to fill soon
struct QueuePosition {
  OrderId order_id{};
  Price   price{};
  Side    side{};
  Qty     qty_ahead{};        // lots ahead in queue
  Qty     qty_behind{};       // lots behind in queue
  Qty     level_total{};      // total qty at this price level
  Qty     own_qty{};          // remaining qty of this order
  int     position_index{};   // 0-based FIFO position
};

// ─── AgentState ───────────────────────────────────────────────────────────────
// Extended with queue_positions: one entry per resting GTC limit order
// this agent currently has in the book.  Empty if the agent has no resting
// orders, or if WorldConfig::track_queue_positions = false.
struct AgentState {
  OwnerId owner{};
  int64_t cash_ticks{0};
  int64_t position{0};

  // Queue positions for all resting GTC limit orders owned by this agent.
  // Populated each step before agent.step() is called.
  // Empty when track_queue_positions = false (default: true).
  std::vector<QueuePosition> queue_positions;
};

// ─── Action ───────────────────────────────────────────────────────────────────
enum class ActionType : uint8_t { Submit = 0, Cancel = 1, ModifyQty = 2 };

struct Action {
  ActionType type{ActionType::Submit};
  Order      order{};
  OrderId    id{};
  Qty        new_qty{};

  static Action submit(const Order& o) {
    Action a{}; a.type = ActionType::Submit; a.order = o; return a;
  }
  static Action cancel(OrderId oid) {
    Action a{}; a.type = ActionType::Cancel; a.id = oid; return a;
  }
  static Action modify_qty(OrderId oid, Qty q) {
    Action a{}; a.type = ActionType::ModifyQty;
    a.id = oid; a.new_qty = q; return a;
  }
};

// ─── PendingAction ────────────────────────────────────────────────────────────
struct PendingAction {
  Ts      effective_ts{0};
  OwnerId owner{0};
  Action  action{};
  Price   arrival_mid{0};

  bool operator<(const PendingAction& o) const noexcept {
    return effective_ts < o.effective_ts;
  }
};

// ─── LatencyActionBuffer ──────────────────────────────────────────────────────
class LatencyActionBuffer {
public:
  void push(Ts ts, OwnerId owner,
            const std::vector<Action>& actions,
            LatencySampler& sampler,
            Price arrival_mid)
  {
    for (const auto& act : actions) {
      const Ts delta = sampler.sample();
      PendingAction pa{};
      pa.effective_ts = ts + delta;
      pa.owner        = owner;
      pa.action       = act;
      pa.arrival_mid  = arrival_mid;
      pending_.push_back(std::move(pa));
    }
  }

  void push_immediate(Ts ts, OwnerId owner,
                      const std::vector<Action>& actions,
                      Price arrival_mid)
  {
    for (const auto& act : actions) {
      PendingAction pa{};
      pa.effective_ts = ts;
      pa.owner        = owner;
      pa.action       = act;
      pa.arrival_mid  = arrival_mid;
      pending_.push_back(std::move(pa));
    }
  }

  const std::vector<PendingAction>& drain() {
    std::stable_sort(pending_.begin(), pending_.end());
    return pending_;
  }

  void   clear()               noexcept { pending_.clear(); }
  size_t size()  const noexcept { return pending_.size(); }

private:
  std::vector<PendingAction> pending_;
};

// ─── IAgent ───────────────────────────────────────────────────────────────────
class IAgent {
public:
  virtual ~IAgent() = default;
  virtual OwnerId owner() const noexcept = 0;
  virtual void seed(uint64_t s) = 0;
  virtual void step(Ts ts,
                    const MarketView&    view,
                    const AgentState&    self,
                    std::vector<Action>& out) = 0;
};

// ─── WorldConfig ──────────────────────────────────────────────────────────────
struct WorldConfig {
  Ts dt_ns{1'000'000};

  // Latency model
  bool latency_enabled{false};
  std::vector<LatencyDistConfig> latency_configs;

  // Stylized facts
  bool compute_stylized_facts{true};
  bool record_fv_signals{false};

  // TCA output
  bool record_fills{true};
  bool record_pnl_series{true};

  // Queue position tracking.
  // When true, AgentState::queue_positions is populated each step for
  // every agent that has resting GTC limit orders in the book.
  // Cost: O(k) per resting limit order per step, where k = position_index.
  // For strategies with O(2-4) resting orders this is negligible.
  // Set to false for scenarios where you never inspect queue positions
  // (e.g. pure market-order strategies) to avoid the overhead.
  bool track_queue_positions{true};
};

// ─── FVLogEntry ───────────────────────────────────────────────────────────────
struct FVLogEntry {
  Ts      ts{};
  OwnerId owner{};
  double  V{};
};

// ─── WorldResult ──────────────────────────────────────────────────────────────
struct WorldResult {
  std::vector<Trade>           trades;
  std::vector<BookTop>         tops;
  std::vector<AccountSnapshot> accounts;
  int64_t cancel_failures{0};
  int64_t modify_failures{0};

  std::optional<StyleFacts> sf;
  std::vector<FVLogEntry>   fv_log;

  std::vector<FillRecord>    fills;
  std::vector<StepSnapshot>  pnl_series;
  std::vector<AgentTCA>      tca;
};

// ─── World ────────────────────────────────────────────────────────────────────
class World {
public:
  explicit World(MatchingEngine engine) : engine_(std::move(engine)) {}

  void add_agent(std::unique_ptr<IAgent> a) {
    agents_.push_back(std::move(a));
  }

  WorldResult run(uint64_t seed,
                  double   horizon_seconds,
                  WorldConfig cfg = {});

  MatchingEngine&       engine_mut() noexcept       { return engine_; }
  const MatchingEngine& engine()     const noexcept { return engine_; }

private:
  static uint64_t splitmix64(uint64_t& x) noexcept;

  double compute_imbalance(Qty& bid_depth_out,
                           Qty& ask_depth_out) const noexcept;

  void process_action(Ts ts,
                      OwnerId oid,
                      const Action& act,
                      Price cur_mid,
                      WorldResult& out,
                      StylizedFactsMeasurer& sfm,
                      const WorldConfig& cfg);

  // Build queue positions for one agent from active_limits_.
  // Fills AgentState::queue_positions and prunes stale entries from
  // active_limits_[oid] (orders that have been fully filled since last step).
  void build_queue_positions(OwnerId oid,
                             AgentState& state);

  MatchingEngine engine_;
  std::vector<std::unique_ptr<IAgent>>   agents_;
  std::unordered_map<OrderId, OrderMeta> order_meta_;
  std::unordered_map<OwnerId, Account>   accounts_;
  std::vector<LatencySampler>            latency_samplers_;

  // TCA tracking state
  std::unordered_map<OrderId, ArrivalInfo> arrival_info_;
  std::unordered_map<OwnerId, int64_t>     n_limit_submitted_;
  std::unordered_map<OwnerId, int64_t>     n_market_submitted_;
  std::unordered_map<OwnerId, int64_t>     n_cancels_sent_;

  // Queue position tracking: active GTC limit order IDs per owner.
  // Entries are added when a GTC limit is accepted into the book and
  // lazily pruned when queue_info() returns found=false (order filled).
  std::unordered_map<OwnerId, std::vector<OrderId>> active_limits_;

  // Cache of limit order prices (OrderId → Price) so build_queue_positions()
  // can populate QueuePosition::price without re-querying the book.
  // Entries are erased on cancel and lazily on fill (via active_limits_ pruning).
  std::unordered_map<OrderId, Price> order_price_cache_;
};

} // namespace msim
