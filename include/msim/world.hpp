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
#include "msim/tca.hpp"           // FillRecord, StepSnapshot, AgentTCA, ArrivalInfo

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

// ─── AgentState ───────────────────────────────────────────────────────────────
struct AgentState {
  OwnerId owner{};
  int64_t cash_ticks{0};
  int64_t position{0};
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
  Price   arrival_mid{0};  // mid at agent step time (for slippage tracking)

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
  Ts dt_ns{1'000'000};  // step width: 1 ms default

  // Latency model
  bool latency_enabled{false};
  std::vector<LatencyDistConfig> latency_configs;

  // Stylized facts
  bool compute_stylized_facts{true};
  bool record_fv_signals{false};

  // TCA output
  // record_fills: store every FillRecord in WorldResult::fills.
  // Enables per-fill slippage analysis.  Small memory cost (one
  // struct per trade per side).  Default on.
  bool record_fills{true};

  // record_pnl_series: store one StepSnapshot per agent per step.
  // Enables full mark-to-market PnL series.  Memory = n_agents *
  // n_steps * sizeof(StepSnapshot).  Default on.
  // For very long runs with many agents, set to false and use only
  // WorldResult::tca for summary statistics.
  bool record_pnl_series{true};
};

// ─── FVLogEntry ───────────────────────────────────────────────────────────────
struct FVLogEntry {
  Ts      ts{};
  OwnerId owner{};
  double  V{};
};

// ─── WorldResult ──────────────────────────────────────────────────────────────
struct WorldResult {
  // ── Core outputs (always populated) ────────────────────────────────────
  std::vector<Trade>           trades;
  std::vector<BookTop>         tops;
  std::vector<AccountSnapshot> accounts;
  int64_t cancel_failures{0};
  int64_t modify_failures{0};

  // ── Optional outputs ────────────────────────────────────────────────────
  std::optional<StyleFacts> sf;       // when compute_stylized_facts=true
  std::vector<FVLogEntry>   fv_log;   // when record_fv_signals=true

  // ── TCA outputs ─────────────────────────────────────────────────────────
  // fills: every individual fill from every agent's perspective.
  // Each trade generates two FillRecords (one maker, one taker).
  // Populated when WorldConfig::record_fills=true.
  std::vector<FillRecord>    fills;

  // pnl_series: mark-to-market state per agent per step.
  // rows = n_agents * n_steps.  Filter by owner to get one agent's series.
  // Populated when WorldConfig::record_pnl_series=true.
  std::vector<StepSnapshot>  pnl_series;

  // tca: per-agent summary statistics.  Always populated.
  // One AgentTCA per registered agent, in registration order.
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

  // process_action now takes cur_mid (for arrival tracking) and cfg.
  void process_action(Ts ts,
                      OwnerId oid,
                      const Action& act,
                      Price cur_mid,
                      WorldResult& out,
                      StylizedFactsMeasurer& sfm,
                      const WorldConfig& cfg);

  MatchingEngine engine_;
  std::vector<std::unique_ptr<IAgent>>   agents_;
  std::unordered_map<OrderId, OrderMeta> order_meta_;
  std::unordered_map<OwnerId, Account>   accounts_;
  std::vector<LatencySampler>            latency_samplers_;

  // TCA tracking state (reset at start of each run())
  std::unordered_map<OrderId, ArrivalInfo> arrival_info_;
  std::unordered_map<OwnerId, int64_t>     n_limit_submitted_;
  std::unordered_map<OwnerId, int64_t>     n_market_submitted_;
  std::unordered_map<OwnerId, int64_t>     n_cancels_sent_;
};

} // namespace msim
