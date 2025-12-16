#include "msim/world.hpp"
#include <algorithm>
#include <cmath>

namespace msim {

uint64_t World::splitmix64(uint64_t& x) noexcept {
  uint64_t z = (x += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

WorldResult World::run(uint64_t seed, double horizon_seconds, WorldConfig cfg) {
  WorldResult out{};

  const Ts t0 = 0;
  const Ts t_end = static_cast<Ts>(std::llround(horizon_seconds * 1'000'000'000.0));

  // deterministic per-agent seeding
  uint64_t sm = seed;
  for (std::size_t i = 0; i < agents_.size(); ++i) {
    const uint64_t s = splitmix64(sm) ^ (static_cast<uint64_t>(i) + 1ull);
    agents_[i]->seed(s);
  }

  for (Ts ts = t0; ts <= t_end; ts += cfg.dt_ns) {
    // flush timed phase transitions / auctions etc
    {
      auto flushed = engine_.flush(ts);
      if (!flushed.empty()) {
        out.trades.insert(out.trades.end(), flushed.begin(), flushed.end());
        auto bb = engine_.book().best_bid();
        auto ba = engine_.book().best_ask();
        auto mid = midprice(bb, ba);
        apply_trades_to_accounts(ts, flushed, order_meta_, accounts_, mid);
      }
    }

    const auto bb = engine_.book().best_bid();
    const auto ba = engine_.book().best_ask();
    const auto mid = midprice(bb, ba);

    MarketView view{};
    view.ts = ts;
    view.best_bid = bb;
    view.best_ask = ba;
    view.mid = mid;
    view.last_trade = engine_.rules().last_trade_price();

    // per-agent actions in insertion order (deterministic)
    for (auto& ap : agents_) {
      const OwnerId oid = ap->owner();

      const auto it = accounts_.find(oid);
      AgentState self{};
      self.owner = oid;
      if (it != accounts_.end()) {
        self.cash_ticks = it->second.cash_ticks;
        self.position = it->second.position;
      }

      std::vector<Action> actions;
      actions.reserve(8);
      ap->step(ts, view, self, actions);

      for (const auto& act : actions) {
        if (act.type == ActionType::Submit) {
          Order o = act.order;
          o.ts = ts;

          // record meta BEFORE processing (so taker side/owner is known)
          order_meta_[o.id] = OrderMeta{o.owner, o.side};

          auto res = engine_.process(o);
          if (!res.trades.empty()) {
            out.trades.insert(out.trades.end(), res.trades.begin(), res.trades.end());
            const auto bb2 = engine_.book().best_bid();
            const auto ba2 = engine_.book().best_ask();
            const auto mid2 = midprice(bb2, ba2);
            apply_trades_to_accounts(ts, res.trades, order_meta_, accounts_, mid2);
          }
        } else if (act.type == ActionType::Cancel) {
          if (!engine_.book_mut().cancel(act.id)) out.cancel_failures++;
        } else {
          if (!engine_.book_mut().modify_qty(act.id, act.new_qty)) out.modify_failures++;
        }
      }
    }

    // record top-of-book
    BookTop top{};
    top.ts = ts;
    top.best_bid = engine_.book().best_bid();
    top.best_ask = engine_.book().best_ask();
    top.mid = midprice(top.best_bid, top.best_ask);
    out.tops.push_back(top);
  }

  // final account snapshots at end
  {
    const auto bb = engine_.book().best_bid();
    const auto ba = engine_.book().best_ask();
    const auto mid = midprice(bb, ba);
    out.accounts = make_account_snapshots(t_end, accounts_, mid);
  }

  return out;
}

} // namespace msim
