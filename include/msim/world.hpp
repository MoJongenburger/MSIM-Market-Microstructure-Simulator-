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

namespace msim {

// ─── MarketView ───────────────────────────────────────────────────────────────
struct MarketView {
  Ts ts{};
  std::optional<Price> best_bid{};
  std::optional<Price> best_ask{};
  std::optional<Price> mid{};
  std::optional<Price> last_trade{};

  // Top-of-book quantities and derived imbalance.
  // Populated each step by World::run.
  // imbalance = (bid_qty - ask_qty) / (bid_qty + ask_qty), in [-1, 1].
  // Defaults to 0 (neutral) when book is empty.
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

  bool operator<(const PendingAction& o) const noexcept {
    return effective_ts < o.effective_ts;
  }
};

// ─── LatencyActionBuffer ──────────────────────────────────────────────────────
class LatencyActionBuffer {
public:
  void push(Ts ts, OwnerId owner,
            const std::vector<Action>& actions,
            LatencySampler& sampler)
  {
    for (const auto& act : actions) {
      const Ts delta = sampler.sample();
      PendingAction pa{};
      pa.effective_ts = ts + delta;
      pa.owner        = owner;
      pa.action       = act;
      pending_.push_back(std::move(pa));
    }
  }

  void push_immediate(Ts ts, OwnerId owner,
                      const std::vector<Action>& actions)
  {
    for (const auto& act : actions) {
      PendingAction pa{};
      pa.effective_ts = ts;
      pa.owner        = owner;
      pa.action       = act;
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

  bool latency_enabled{false};
  std::vector<LatencyDistConfig> latency_configs;

  bool compute_stylized_facts{true};
  bool record_fv_signals{false};
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

  // Compute LOB imbalance from best-bid and best-ask quantities.
  // Uses the top-of-book L2 snapshot.  Returns 0 if book is empty.
  double compute_imbalance(Qty& bid_depth_out,
                           Qty& ask_depth_out) const noexcept;

  void process_action(Ts ts,
                      OwnerId oid,
                      const Action& act,
                      WorldResult& out,
                      StylizedFactsMeasurer& sfm);

  MatchingEngine engine_;
  std::vector<std::unique_ptr<IAgent>>   agents_;
  std::unordered_map<OrderId, OrderMeta> order_meta_;
  std::unordered_map<OwnerId, Account>   accounts_;
  std::vector<LatencySampler>            latency_samplers_;
};

} // namespace msim
