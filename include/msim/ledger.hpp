#pragma once
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>
#include <algorithm>

#include "msim/types.hpp"
#include "msim/matching_engine.hpp"
#include "msim/invariants.hpp"

namespace msim {

struct OrderMeta {
  OwnerId owner{};
  Side side{};
};

struct Account {
  OwnerId owner{};
  int64_t cash_ticks{0};   // cash measured in "ticks * qty"
  int64_t position{0};     // inventory in shares/contracts

  int64_t traded_qty{0};
  int64_t notional_ticks{0};

  void apply_fill(Side side, Price px, Qty q) noexcept {
    const int64_t qq = static_cast<int64_t>(q);
    const int64_t pp = static_cast<int64_t>(px);
    const int64_t notion = pp * qq;

    traded_qty += qq;
    notional_ticks += notion;

    if (side == Side::Buy) {
      position += qq;
      cash_ticks -= notion;
    } else {
      position -= qq;
      cash_ticks += notion;
    }
  }

  int64_t mtm_ticks(std::optional<Price> mid) const noexcept {
    if (!mid) return cash_ticks;
    return cash_ticks + static_cast<int64_t>(*mid) * position;
  }
};

struct AccountSnapshot {
  Ts ts{};
  OwnerId owner{};
  int64_t cash_ticks{};
  int64_t position{};
  int64_t mtm_ticks{};
};

inline void apply_trades_to_accounts(
    Ts ts,
    const std::vector<Trade>& trades,
    const std::unordered_map<OrderId, OrderMeta>& meta,
    std::unordered_map<OwnerId, Account>& accounts,
    std::optional<Price> mid_for_mtm) {

  for (const auto& tr : trades) {
    auto it_m = meta.find(tr.maker_order_id);
    auto it_t = meta.find(tr.taker_order_id);
    if (it_m == meta.end() || it_t == meta.end()) continue;

    const auto& mm = it_m->second;
    const auto& tm = it_t->second;

    auto& am = accounts[mm.owner];
    am.owner = mm.owner;
    auto& at = accounts[tm.owner];
    at.owner = tm.owner;

    am.apply_fill(mm.side, tr.price, tr.qty);
    at.apply_fill(tm.side, tr.price, tr.qty);
  }

  (void)ts;
  (void)mid_for_mtm;
}

inline std::vector<AccountSnapshot> make_account_snapshots(
    Ts ts,
    const std::unordered_map<OwnerId, Account>& accounts,
    std::optional<Price> mid) {

  std::vector<OwnerId> owners;
  owners.reserve(accounts.size());
  for (const auto& kv : accounts) owners.push_back(kv.first);
  std::sort(owners.begin(), owners.end());

  std::vector<AccountSnapshot> out;
  out.reserve(owners.size());

  for (OwnerId oid : owners) {
    const auto& a = accounts.at(oid);
    AccountSnapshot s{};
    s.ts = ts;
    s.owner = oid;
    s.cash_ticks = a.cash_ticks;
    s.position = a.position;
    s.mtm_ticks = a.mtm_ticks(mid);
    out.push_back(s);
  }
  return out;
}

} // namespace msim
