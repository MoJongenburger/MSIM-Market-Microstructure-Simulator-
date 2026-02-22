// ============================================================
// src/world.cpp  — COMPLETE REPLACEMENT
//
// Every change marked NEW.  Original logic untouched otherwise.
// ============================================================

#include "msim/world.hpp"
#include "msim/agents/fundamental_value_agent.hpp"  // NEW: FV signal logging

#include <algorithm>
#include <cmath>

namespace msim {

// ─────────────────────────────────────────────────────────────────────────────
// splitmix64 — unchanged
// ─────────────────────────────────────────────────────────────────────────────
uint64_t World::splitmix64(uint64_t& x) noexcept {
  uint64_t z = (x += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

// ─────────────────────────────────────────────────────────────────────────────
// process_action  (NEW helper — extracted from the inner loop)
//
// Handles one Submit / Cancel / ModifyQty, applies ledger updates, and
// records every resulting trade into the StylizedFactsMeasurer.
// Called from both the zero-latency and the latency-sorted path.
// ─────────────────────────────────────────────────────────────────────────────
void World::process_action(Ts ts,
                            OwnerId /*oid*/,
                            const Action& act,
                            WorldResult& out,
                            StylizedFactsMeasurer& sfm)
{
  if (act.type == ActionType::Submit) {
    Order o = act.order;
    o.ts = ts;

    // Record meta BEFORE processing (taker side must be known)
    order_meta_[o.id] = OrderMeta{o.owner, o.side};

    auto res = engine_.process(o);

    if (!res.trades.empty()) {
      out.trades.insert(out.trades.end(),
                        res.trades.begin(), res.trades.end());

      const auto bb2  = engine_.book().best_bid();
      const auto ba2  = engine_.book().best_ask();
      const auto mid2 = midprice(bb2, ba2);
      apply_trades_to_accounts(ts, res.trades, order_meta_, accounts_, mid2);

      // NEW: record to stylized facts measurer
      // Aggressor = taker; look up side from order_meta_.
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
    if (!engine_.book_mut().cancel(act.id)) out.cancel_failures++;

  } else { // ModifyQty
    if (!engine_.book_mut().modify_qty(act.id, act.new_qty))
      out.modify_failures++;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// run()
// ─────────────────────────────────────────────────────────────────────────────
WorldResult World::run(uint64_t seed,
                       double   horizon_seconds,
                       WorldConfig cfg)
{
  WorldResult out{};
  const Ts t0    = 0;
  const Ts t_end = static_cast<Ts>(
      std::llround(horizon_seconds * 1'000'000'000.0));

  // ── 1. Seed agents — unchanged ────────────────────────────────────────────
  uint64_t sm = seed;
  for (std::size_t i = 0; i < agents_.size(); ++i) {
    const uint64_t s = splitmix64(sm) ^ (static_cast<uint64_t>(i) + 1ull);
    agents_[i]->seed(s);
  }

  // ── 2. NEW: Build one LatencySampler per agent ────────────────────────────
  // Sampler seeds are derived from (seed XOR fixed constant), iterated, so
  // they are completely independent of the agent RNG streams above.
  // If latency is disabled, samplers are built with FIXED 0ns — they are
  // never called in that case, so there is zero runtime cost.
  latency_samplers_.clear();
  latency_samplers_.reserve(agents_.size());
  {
    uint64_t lat_sm = seed ^ 0xDEAD'BEEF'CAFE'BABEull;
    for (std::size_t i = 0; i < agents_.size(); ++i) {
      const uint64_t lat_seed = splitmix64(lat_sm);

      LatencyDistConfig ldc{LatencyDistType::FIXED, 0.0}; // zero-latency default
      if (cfg.latency_enabled && i < cfg.latency_configs.size())
        ldc = cfg.latency_configs[i];

      latency_samplers_.emplace_back(lat_seed, ldc);
    }
  }

  // ── 3. NEW: Stylized facts measurer ───────────────────────────────────────
  StylizedFactsMeasurer sfm(/*max_lag=*/20, /*n_impact_bins=*/10);

  // ── 4. NEW: Latency buffer (re-used each step, cleared at top of loop) ────
  LatencyActionBuffer lat_buf;

  // ─────────────────────────────────────────────────────────────────────────
  // Main loop
  // ─────────────────────────────────────────────────────────────────────────
  for (Ts ts = t0; ts <= t_end; ts += cfg.dt_ns) {

    // ── A. Flush timed phase transitions — original logic ─────────────────
    {
      auto flushed = engine_.flush(ts);
      if (!flushed.empty()) {
        out.trades.insert(out.trades.end(),
                          flushed.begin(), flushed.end());
        const auto bb  = engine_.book().best_bid();
        const auto ba  = engine_.book().best_ask();
        const auto mid = midprice(bb, ba);
        apply_trades_to_accounts(ts, flushed, order_meta_, accounts_, mid);

        // NEW: record flush trades to SF measurer
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

    // ── B. Build MarketView — unchanged ───────────────────────────────────
    const auto bb  = engine_.book().best_bid();
    const auto ba  = engine_.book().best_ask();
    const auto mid = midprice(bb, ba);

    MarketView view{};
    view.ts         = ts;
    view.best_bid   = bb;
    view.best_ask   = ba;
    view.mid        = mid;
    view.last_trade = engine_.rules().last_trade_price();

    // ── C. NEW: Record top-of-book for spread / illiquidity measurement ───
    if (bb && ba)
      sfm.add_top({ts, *bb, *ba, mid ? *mid : Price{0},
                   Price{0}, Price{0}});

    // ── D. Collect and process agent actions ──────────────────────────────
    //
    //  ZERO-LATENCY (default, latency_enabled = false):
    //    Agents act in registration order; each action processed immediately.
    //    Output is byte-identical to original world.cpp.
    //
    //  LATENCY (latency_enabled = true):
    //    Actions pushed into lat_buf with a sampled per-agent delay.
    //    After all agents act, lat_buf is sorted by effective arrival time
    //    and drained — lower-latency agents win ordering priority.

    lat_buf.clear();

    for (std::size_t i = 0; i < agents_.size(); ++i) {
      auto& ap       = agents_[i];
      const OwnerId oid = ap->owner();

      // Build AgentState — unchanged
      const auto acc_it = accounts_.find(oid);
      AgentState self{};
      self.owner = oid;
      if (acc_it != accounts_.end()) {
        self.cash_ticks = acc_it->second.cash_ticks;
        self.position   = acc_it->second.position;
      }

      // Ask agent for actions
      std::vector<Action> actions;
      actions.reserve(8);
      ap->step(ts, view, self, actions);

      // NEW: optional FV private-signal log
      if (cfg.record_fv_signals) {
        if (auto* fva = dynamic_cast<
                agents::FundamentalValueAgent*>(ap.get())) {
          out.fv_log.push_back({ts, oid, fva->fundamental_value()});
        }
      }

      if (!cfg.latency_enabled) {
        // Zero-latency: process immediately (original behaviour)
        for (const auto& act : actions)
          process_action(ts, oid, act, out, sfm);
      } else {
        // Latency: buffer with per-agent delay
        lat_buf.push(ts, oid, actions, latency_samplers_[i]);
      }
    }

    // ── E. NEW: Drain latency buffer in effective-arrival-time order ──────
    if (cfg.latency_enabled) {
      for (const auto& pa : lat_buf.drain())
        process_action(pa.effective_ts, pa.owner, pa.action, out, sfm);
    }

    // ── F. Record top-of-book snapshot — unchanged ────────────────────────
    BookTop top{};
    top.ts       = ts;
    top.best_bid = engine_.book().best_bid();
    top.best_ask = engine_.book().best_ask();
    top.mid      = midprice(top.best_bid, top.best_ask);
    out.tops.push_back(top);
  }

  // ── 5. Final account snapshots — unchanged ────────────────────────────────
  {
    const auto bb  = engine_.book().best_bid();
    const auto ba  = engine_.book().best_ask();
    const auto mid = midprice(bb, ba);
    out.accounts = make_account_snapshots(t_end, accounts_, mid);
  }

  // ── 6. NEW: Compute stylized facts ────────────────────────────────────────
  if (cfg.compute_stylized_facts && sfm.n_trades() >= 10)
    out.sf = sfm.compute();

  return out;
}

} // namespace msim
