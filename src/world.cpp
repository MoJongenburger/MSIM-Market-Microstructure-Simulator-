#include "msim/world.hpp"

#include "msim/invariants.hpp"

namespace msim {

void World::add_agent(std::unique_ptr<msim::agents::Agent> a) {
  agents_.push_back(std::move(a));
}

WorldResult World::run(Ts start_ts, Ts horizon_ns, Ts step_ns, std::size_t depth_levels, uint64_t seed) {
  WorldResult out{};
  if (step_ns <= 0) step_ns = 1;

  // deterministic RNG per run
  const uint64_t default_seed = static_cast<uint64_t>(start_ts) ^ 0x9E3779B97F4A7C15ULL;
  std::mt19937_64 rng(seed == 0 ? default_seed : seed);

  const Ts end_ts = start_ts + horizon_ns;

  for (Ts ts = start_ts; ts <= end_ts; ts += step_ns) {
    out.stats.steps++;

    const auto view = msim::agents::make_view(engine_.book(), ts, depth_levels);

    // Agents generate actions at this timestamp (deterministic ordering by registration)
    for (auto& ap : agents_) {
      auto actions = ap->generate_actions(view, rng);
      out.stats.actions_sent += static_cast<uint64_t>(actions.size());

      for (auto& act : actions) {
        if (act.type == msim::agents::ActionType::Place) {
          out.stats.orders_sent++;

          act.place.ts = ts;
          act.place.owner = ap->owner_id();

          auto res = engine_.process(act.place);
          if (res.status == msim::OrderStatus::Rejected) out.stats.rejects++;

          out.stats.trades += static_cast<uint64_t>(res.trades.size());
          out.trades.insert(out.trades.end(), res.trades.begin(), res.trades.end());

          // broadcast trades to agents
          msim::agents::MarketEvent ev{};
          ev.ts = ts;
          ev.phase = engine_.rules().phase();
          ev.trades = std::move(res.trades);
          for (auto& bp : agents_) bp->on_market_event(ev);

          // record top-of-book snapshot after each message (like an exchange feed)
          msim::BookTop top{};
          top.ts = ts;
          top.best_bid = engine_.book().best_bid();
          top.best_ask = engine_.book().best_ask();
          top.mid = msim::midprice(top.best_bid, top.best_ask);
          out.tops.push_back(top);
        }
        else if (act.type == msim::agents::ActionType::Cancel) {
          out.stats.cancels_sent++;
          (void)engine_.book_mut().cancel(act.cancel_id);
        }
        else if (act.type == msim::agents::ActionType::ModifyQty) {
          out.stats.modifies_sent++;
          (void)engine_.book_mut().modify_qty(act.cancel_id, act.new_qty);
        }
      }
    }
  }

  return out;
}

} // namespace msim
