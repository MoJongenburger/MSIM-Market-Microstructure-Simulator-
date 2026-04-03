#pragma once
// ============================================================
// include/msim/world.hpp
//
// Performance changes vs previous version:
//   1. active_limits_: vector<OrderId> → unordered_set<OrderId>
//      Cancel cleanup: O(N) linear scan → O(1) set erase.
//      build_queue_positions: erase-in-place during iteration,
//      still_active_buf_ member removed (no longer needed).
//
//   2. proc_result_buf_: pre-allocated ProcessResult member.
//      engine_.process_into(o, proc_result_buf_) reuses the
//      internal trades vector across calls, eliminating one
//      heap allocation per order that generates trades.
//      Requires process_into() in matching_engine.hpp/.cpp.
//
//   3. depth_bid_buf_ / depth_ask_buf_ removed.
//      compute_imbalance() now uses best_bid_qty()/best_ask_qty()
//      which are O(1) — no vector needed at all.
// ============================================================

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
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

  Qty    bid_depth = 0;
  Qty    ask_depth = 0;
  double imbalance = 0.0;
};

// ─── QueuePosition ────────────────────────────────────────────────────────────
struct QueuePosition {
  OrderId order_id{};
  Price   price{};
  Side    side{};
  Qty     qty_ahead{};
  Qty     qty_behind{};
  Qty     level_total{};
  Qty     own_qty{};
  int     position_index{};
};

// ─── AgentState ───────────────────────────────────────────────────────────────
struct AgentState {
  OwnerId owner{};
  int64_t cash_ticks{0};
  int64_t position{0};
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

  void   clear()              noexcept { pending_.clear(); }
  size_t size() const noexcept { return pending_.size(); }

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

  bool latency_enabled{false};
  std::vector<LatencyDistConfig> latency_configs;

  bool compute_stylized_facts{true};
  bool record_fv_signals{false};

  bool record_fills{true};
  bool record_pnl_series{true};

  bool track_queue_positions{true};

  std::size_t expected_resting_orders{0};
  std::size_t expected_fills{0};
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

  void build_queue_positions(OwnerId oid, AgentState& state);

  MatchingEngine engine_;
  std::vector<std::unique_ptr<IAgent>>   agents_;
  std::unordered_map<OrderId, OrderMeta> order_meta_;
  std::unordered_map<OwnerId, Account>   accounts_;
  std::vector<LatencySampler>            latency_samplers_;

  std::unordered_map<OrderId, ArrivalInfo>          arrival_info_;
  std::unordered_map<OwnerId, int64_t>              n_limit_submitted_;
  std::unordered_map<OwnerId, int64_t>              n_market_submitted_;
  std::unordered_map<OwnerId, int64_t>              n_cancels_sent_;
  std::unordered_map<OrderId, Price>                order_price_cache_;

  // active_limits_: unordered_set (was vector) → O(1) erase on cancel,
  // O(1) erase-during-iteration in build_queue_positions.
  std::unordered_map<OwnerId, std::unordered_set<OrderId>> active_limits_;

  // proc_result_buf_: reused across every engine_.process_into() call.
  // The trades vector inside retains its capacity between calls, eliminating
  // the heap allocation that occurred on every order that generated trades.
  ProcessResult proc_result_buf_;

  // actions_buf_: reused across every agent step().
  std::vector<Action> actions_buf_;
  // still_active_buf_ removed — no longer needed with unordered_set.
};

} // namespace msim
