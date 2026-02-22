#pragma once
// ============================================================
// include/msim/world.hpp  — COMPLETE REPLACEMENT
//
// Changes vs original:
//   1. WorldConfig gains latency_enabled + latency_configs
//   2. WorldResult gains sf (StyleFacts) + fv_log
//   3. World gains latency_samplers_ member
//   4. No public API changes — add_agent() / run() unchanged
// ============================================================

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "msim/matching_engine.hpp"
#include "msim/simulator.hpp"       // BookTop
#include "msim/ledger.hpp"
#include "msim/latency_model.hpp"   // NEW
#include "msim/stylized_facts.hpp"  // NEW

namespace msim {

// ── MarketView ────────────────────────────────────────────────────────────────
struct MarketView {
  Ts ts{};
  std::optional<Price> best_bid{};
  std::optional<Price> best_ask{};
  std::optional<Price> mid{};
  std::optional<Price> last_trade{};
};

// ── AgentState ────────────────────────────────────────────────────────────────
struct AgentState {
  OwnerId owner{};
  int64_t cash_ticks{0};
  int64_t position{0};
};

// ── Action ────────────────────────────────────────────────────────────────────
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
    Action a{}; a.type = ActionType::ModifyQty; a.id = oid; a.new_qty = q; return a;
  }
};

// ── IAgent ────────────────────────────────────────────────────────────────────
class IAgent {
public:
  virtual ~IAgent() = default;
  virtual OwnerId owner() const noexcept = 0;
  virtual void seed(uint64_t s) = 0;
  virtual void step(Ts ts,
                    const MarketView& view,
                    const AgentState& self,
                    std::vector<Action>& out) = 0;
};

// ── WorldConfig ───────────────────────────────────────────────────────────────
struct WorldConfig {
  Ts dt_ns{1'000'000};  // 1 ms per step

  // NEW: latency model
  // Set latency_enabled = true and provide one LatencyDistConfig per agent
  // (in the same order as add_agent calls).
  // When false (default) behaviour is byte-identical to the original.
  bool latency_enabled{false};
  std::vector<LatencyDistConfig> latency_configs;

  // NEW: compute stylized facts at end of run, stored in WorldResult::sf
  bool compute_stylized_facts{true};

  // NEW: log every FV agent's private signal each step into WorldResult::fv_log
  bool record_fv_signals{false};
};

// ── FVLogEntry ────────────────────────────────────────────────────────────────
struct FVLogEntry {
  Ts      ts{};
  OwnerId owner{};
  double  V{};   // private fundamental value signal this step
};

// ── WorldResult ───────────────────────────────────────────────────────────────
struct WorldResult {
  std::vector<Trade>           trades;
  std::vector<BookTop>         tops;
  std::vector<AccountSnapshot> accounts;
  int64_t cancel_failures{0};
  int64_t modify_failures{0};

  // NEW
  std::optional<StyleFacts>  sf;       // populated when WorldConfig::compute_stylized_facts
  std::vector<FVLogEntry>    fv_log;   // populated when WorldConfig::record_fv_signals
};

// ── World ─────────────────────────────────────────────────────────────────────
class World {
public:
  explicit World(MatchingEngine engine) : engine_(std::move(engine)) {}

  void add_agent(std::unique_ptr<IAgent> a) { agents_.push_back(std::move(a)); }

  WorldResult run(uint64_t seed, double horizon_seconds, WorldConfig cfg = {});

  MatchingEngine&       engine_mut() noexcept { return engine_; }
  const MatchingEngine& engine()     const noexcept { return engine_; }

private:
  static uint64_t splitmix64(uint64_t& x) noexcept;

  MatchingEngine engine_;
  std::vector<std::unique_ptr<IAgent>>   agents_;
  std::unordered_map<OrderId, OrderMeta> order_meta_;
  std::unordered_map<OwnerId, Account>   accounts_;
  std::vector<LatencySampler>            latency_samplers_;  // NEW: one per agent

  // Process one action (Submit/Cancel/Modify) and record outcomes
  void process_action(Ts ts,
                      OwnerId oid,
                      const Action& act,
                      WorldResult& out,
                      StylizedFactsMeasurer& sfm);
};

} // namespace msim
