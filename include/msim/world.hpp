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

  bool latency_enabled{false};
  std::vector<LatencyDistConfig> latency_configs;

  bool compute_stylized_facts{true};
  bool record_fv_signals{false};

  bool record_fills{true};
  bool record_pnl_series{true};

  bool track_queue_positions{true};

  // ── Performance hints ─────────────────────────────────────────────────────
  // Expected peak number of simultaneously resting orders.
  // Used to pre-reserve the order_meta_ and arrival_info_ hashmaps at the
  // start of run(), eliminating rehash spikes in p99 latency.
  // 0 = use a conservative default (256).
  std::size_t expected_resting_orders{0};

  // Expected number of fills (trades) for the whole run.
  // Used to pre-reserve out.trades and out.fills.
  // 0 = estimate from horizon / dt_ns.
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

  std::unordered_map<OrderId, ArrivalInfo>           arrival_info_;
  std::unordered_map<OwnerId, int64_t>               n_limit_submitted_;
  std::unordered_map<OwnerId, int64_t>               n_market_submitted_;
  std::unordered_map<OwnerId, int64_t>               n_cancels_sent_;
  std::unordered_map<OwnerId, std::vector<OrderId>>  active_limits_;
  std::unordered_map<OrderId, Price>                 order_price_cache_;

  // ── Reusable buffers (allocated once, reused across every step) ───────────
  // These eliminate per-step heap allocations on the hot path.

  // depth_bid_buf_ / depth_ask_buf_:
  //   Used by compute_imbalance() instead of allocating a new vector each call.
  //   Saves ~2 heap allocs per step (one per side) = ~2M allocs in a 1000-seed
  //   sweep of 1000 steps each.
  mutable std::vector<LevelSummary> depth_bid_buf_;
  mutable std::vector<LevelSummary> depth_ask_buf_;

  // actions_buf_:
  //   Reused for each agent's step() call instead of constructing a new
  //   vector per agent per step.  Capacity grows to the max ever seen and
  //   then never allocates again.
  std::vector<Action> actions_buf_;

  // still_active_buf_:
  //   Reused inside build_queue_positions() to avoid allocating the
  //   "still-active" working set each step for agents with resting orders.
  std::vector<OrderId> still_active_buf_;
};

} // namespace msim
