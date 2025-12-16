#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "msim/matching_engine.hpp"
#include "msim/simulator.hpp"   // for BookTop
#include "msim/ledger.hpp"

namespace msim {

struct MarketView {
  Ts ts{};
  std::optional<Price> best_bid{};
  std::optional<Price> best_ask{};
  std::optional<Price> mid{};
  std::optional<Price> last_trade{};
};

struct AgentState {
  OwnerId owner{};
  int64_t cash_ticks{0};
  int64_t position{0};
};

enum class ActionType : uint8_t { Submit = 0, Cancel = 1, ModifyQty = 2 };

struct Action {
  ActionType type{ActionType::Submit};
  Order order{};
  OrderId id{};
  Qty new_qty{};

  static Action submit(const Order& o) {
    Action a{};
    a.type = ActionType::Submit;
    a.order = o;
    return a;
  }
  static Action cancel(OrderId oid) {
    Action a{};
    a.type = ActionType::Cancel;
    a.id = oid;
    return a;
  }
  static Action modify_qty(OrderId oid, Qty q) {
    Action a{};
    a.type = ActionType::ModifyQty;
    a.id = oid;
    a.new_qty = q;
    return a;
  }
};

class IAgent {
public:
  virtual ~IAgent() = default;
  virtual OwnerId owner() const noexcept = 0;
  virtual void seed(uint64_t s) = 0;
  virtual void step(Ts ts, const MarketView& view, const AgentState& self, std::vector<Action>& out) = 0;
};

struct WorldConfig {
  Ts dt_ns{1'000'000}; // 1ms
};

struct WorldResult {
  std::vector<Trade> trades;
  std::vector<BookTop> tops;

  // new: end-of-run account snapshots
  std::vector<AccountSnapshot> accounts;

  int64_t cancel_failures{0};
  int64_t modify_failures{0};
};

class World {
public:
  explicit World(MatchingEngine engine) : engine_(std::move(engine)) {}

  void add_agent(std::unique_ptr<IAgent> a) { agents_.push_back(std::move(a)); }

  WorldResult run(uint64_t seed, double horizon_seconds, WorldConfig cfg = {});

  MatchingEngine& engine_mut() noexcept { return engine_; }
  const MatchingEngine& engine() const noexcept { return engine_; }

private:
  static uint64_t splitmix64(uint64_t& x) noexcept;

  MatchingEngine engine_;
  std::vector<std::unique_ptr<IAgent>> agents_;

  std::unordered_map<OrderId, OrderMeta> order_meta_;
  std::unordered_map<OwnerId, Account> accounts_;
};

} // namespace msim
