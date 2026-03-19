// ============================================================
// src/world.cpp
// ============================================================
#include "msim/world.hpp"
#include "msim/agents/fundamental_value_agent.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace msim {

// ---------------------------------------------------------------------------
// splitmix64
// ---------------------------------------------------------------------------
uint64_t World::splitmix64(uint64_t& x) noexcept {
  uint64_t z = (x += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

// ---------------------------------------------------------------------------
// compute_imbalance
// Derives (V_bid - V_ask) / (V_bid + V_ask) from top-of-book L2.
// Uses book::depth(Side, levels) which returns a vector of BookLevel by
// value -- this is the existing path benchmarked by BM_BookDepth_TopN.
// If your book does not yet expose depth(), imbalance defaults to 0.
// ---------------------------------------------------------------------------
double World::compute_imbalance(Qty& bid_depth_out,
                                Qty& ask_depth_out) const noexcept
{
  bid_depth_out = 0;
  ask_depth_out = 0;

  // depth(side, n_levels) returns std::vector<BookLevel> by value.
  // Each BookLevel has .price and .qty.
  // We only need the top level (n=1).
  const auto bid_lvls = engine_.book().depth(Side::Buy,  1);
  const auto ask_lvls = engine_.book().depth(Side::Sell, 1);

  if (bid_lvls.empty() || ask_lvls.empty())
    return 0.0;

  bid_depth_out = bid_lvls[0].total_qty;
  ask_depth_out = ask_lvls[0].total_qty;

  const double bd    = static_cast<double>(bid_depth_out);
  const double ad    = static_cast<double>(ask_depth_out);
  const double total = bd + ad;
  return (total > 0.0) ? (bd - ad) / total : 0.0;
}

// ---------------------------------------------------------------------------
// process_action
// ---------------------------------------------------------------------------
void World::process_action(Ts ts,
                            OwnerId /*oid*/,
                            const Action& act,
                            WorldResult& out,
                            StylizedFactsMeasurer& sfm)
{
  if (act.type == ActionType::Submit) {
    Order o = act.order;
    o.ts = ts;

    order_meta_[o.id] = OrderMeta{o.owner, o.side};

    auto res = engine_.process(o);

    if (!res.trades.empty()) {
      out.trades.insert(out.trades.end(),
                        res.trades.begin(), res.trades.end());

      const auto bb2  = engine_.book().best_bid();
      const auto ba2  = engine_.book().best_ask();
      const auto mid2 = midprice(bb2, ba2);
      apply_trades_to_accounts(ts, res.trades, order_meta_,
                                accounts_, mid2);

      const Price cur_mid = mid2 ? *mid2 : Price{0};
      for (const auto& tr : res.trades) {
        Side aggressor = Side::Buy;
        if (auto it = order_meta_.find(tr.taker_order_id);
            it != order_meta_.end())
          aggressor = it->second.side;
        sfm.add_trade({tr.ts, tr.price, tr.qty, aggressor, cur_mid});
      }
    }

  } else if (act.type == ActionType::Cancel) {
    if (!engine_.book_mut().cancel(act.id))
      out.cancel_failures++;

  } else { // ModifyQty
    if (!engine_.book_mut().modify_qty(act.id, act.new_qty))
      out.modify_failures++;
  }
}

// ---------------------------------------------------------------------------
// run
// ---------------------------------------------------------------------------
WorldResult World::run(uint64_t seed,
                       double   horizon_seconds,
                       WorldConfig cfg)
{
  WorldResult out{};
  const Ts t0    = 0;
  const Ts t_end = static_cast<Ts>(
      std::llround(horizon_seconds * 1'000'000'000.0));

  // 1. Seed agents
  uint64_t sm = seed;
  for (std::size_t i = 0; i < agents_.size(); ++i) {
    const uint64_t s = splitmix64(sm)
                       ^ (static_cast<uint64_t>(i) + 1ull);
    agents_[i]->seed(s);
  }

  // 2. Build one LatencySampler per agent
  latency_samplers_.clear();
  latency_samplers_.reserve(agents_.size());
  {
    uint64_t lat_sm = seed ^ 0xDEAD'BEEF'CAFE'BABEull;
    for (std::size_t i = 0; i < agents_.size(); ++i) {
      const uint64_t lat_seed = splitmix64(lat_sm);
      LatencyDistConfig ldc{LatencyDistType::FIXED, 0.0};
      if (cfg.latency_enabled && i < cfg.latency_configs.size())
        ldc = cfg.latency_configs[i];
      latency_samplers_.emplace_back(lat_seed, ldc);
    }
  }

  // 3. Stylized facts measurer
  StylizedFactsMeasurer sfm(20, 10);

  // 4. Latency buffer
  LatencyActionBuffer lat_buf;

  // -------------------------------------------------------------------------
  for (Ts ts = t0; ts <= t_end; ts += cfg.dt_ns) {

    // A. Flush timed events (auction uncrossings, phase transitions)
    {
      auto flushed = engine_.flush(ts);
      if (!flushed.empty()) {
        out.trades.insert(out.trades.end(),
                          flushed.begin(), flushed.end());
        const auto bb  = engine_.book().best_bid();
        const auto ba  = engine_.book().best_ask();
        const auto mid = midprice(bb, ba);
        apply_trades_to_accounts(ts, flushed, order_meta_,
                                  accounts_, mid);
        const Price cur_mid = mid ? *mid : Price{0};
        for (const auto& tr : flushed) {
          Side aggressor = Side::Buy;
          if (auto it = order_meta_.find(tr.taker_order_id);
              it != order_meta_.end())
            aggressor = it->second.side;
          sfm.add_trade({tr.ts, tr.price, tr.qty, aggressor, cur_mid});
        }
      }
    }

    // B. Build MarketView
    const auto bb  = engine_.book().best_bid();
    const auto ba  = engine_.book().best_ask();
    const auto mid = midprice(bb, ba);

    MarketView view{};
    view.ts         = ts;
    view.best_bid   = bb;
    view.best_ask   = ba;
    view.mid        = mid;
    view.last_trade = engine_.rules().last_trade_price();

    // B1. LOB imbalance via book::depth(Side, 1).
    // If book::depth does not exist in your version, this will be a
    // compile error -- comment out the try block and the imbalance
    // fields will remain 0 (safe default: symmetric quotes).
    view.imbalance = compute_imbalance(view.bid_depth, view.ask_depth);

    // C. Record top for stylized facts
    if (bb && ba)
      sfm.add_top({ts, *bb, *ba, mid ? *mid : Price{0},
                   Price{0}, Price{0}});

    // D. Collect agent actions
    lat_buf.clear();

    for (std::size_t i = 0; i < agents_.size(); ++i) {
      auto& ap          = agents_[i];
      const OwnerId oid = ap->owner();

      const auto acc_it = accounts_.find(oid);
      AgentState self{};
      self.owner = oid;
      if (acc_it != accounts_.end()) {
        self.cash_ticks = acc_it->second.cash_ticks;
        self.position   = acc_it->second.position;
      }

      std::vector<Action> actions;
      actions.reserve(8);
      ap->step(ts, view, self, actions);

      if (cfg.record_fv_signals) {
        if (auto* fva = dynamic_cast<
                agents::FundamentalValueAgent*>(ap.get()))
          out.fv_log.push_back({ts, oid, fva->fundamental_value()});
      }

      if (!cfg.latency_enabled) {
        for (const auto& act : actions)
          process_action(ts, oid, act, out, sfm);
      } else {
        lat_buf.push(ts, oid, actions, latency_samplers_[i]);
      }
    }

    // E. Drain latency buffer
    if (cfg.latency_enabled) {
      for (const auto& pa : lat_buf.drain())
        process_action(pa.effective_ts, pa.owner, pa.action, out, sfm);
    }

    // F. Snapshot top-of-book
    BookTop top{};
    top.ts       = ts;
    top.best_bid = engine_.book().best_bid();
    top.best_ask = engine_.book().best_ask();
    top.mid      = midprice(top.best_bid, top.best_ask);
    out.tops.push_back(top);
  }

  // 5. Final account snapshots
  {
    const auto bb  = engine_.book().best_bid();
    const auto ba  = engine_.book().best_ask();
    const auto mid = midprice(bb, ba);
    out.accounts = make_account_snapshots(t_end, accounts_, mid);
  }

  // 6. Stylized facts
  if (cfg.compute_stylized_facts && sfm.n_trades() >= 10)
    out.sf = sfm.compute();

  return out;
}

} // namespace msim
