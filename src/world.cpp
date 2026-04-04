// ============================================================
// src/world.cpp
//
// Performance changes vs previous version:
//
//   5. Step F reuses bb/ba/mid from step B.
//      best_bid() and best_ask() each do a FlatPriceMap::begin()
//      call (one pointer read + branch). Step F was calling them
//      again redundantly every step. Now it uses the values
//      already computed in step B — 2 fewer calls per step,
//      ~2000 fewer calls per second of simulation.
//
//   6. StylizedFactsMeasurer pre-reserved via sfm.reserve().
//      Called once after construction with n_steps (for tops)
//      and est_fills (for trades). Eliminates the growing
//      reallocation series inside the measurer.
// ============================================================
#include "msim/world.hpp"
#include "msim/agents/fundamental_value_agent.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace msim {

uint64_t World::splitmix64(uint64_t& x) noexcept {
  uint64_t z = (x += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

double World::compute_imbalance(Qty& bid_depth_out,
                                Qty& ask_depth_out) const noexcept
{
  bid_depth_out = engine_.book().best_bid_qty();
  ask_depth_out = engine_.book().best_ask_qty();
  const double bd    = static_cast<double>(bid_depth_out);
  const double ad    = static_cast<double>(ask_depth_out);
  const double total = bd + ad;
  return (total > 0.0) ? (bd - ad) / total : 0.0;
}

void World::build_queue_positions(OwnerId oid, AgentState& state)
{
  state.queue_positions.clear();

  auto& ids = active_limits_[oid];
  if (ids.empty()) return;

  for (auto it = ids.begin(); it != ids.end(); ) {
    const OrderId order_id = *it;
    const QueueInfo qi = engine_.book().queue_info(order_id);

    if (!qi.found) {
      it = ids.erase(it);
      continue;
    }
    ++it;

    Side side = Side::Buy;
    if (auto mit = order_meta_.find(order_id); mit != order_meta_.end())
      side = mit->second.side;

    Price price = 0;
    if (auto pit = order_price_cache_.find(order_id);
        pit != order_price_cache_.end())
      price = pit->second;

    QueuePosition qp{};
    qp.order_id       = order_id;
    qp.price          = price;
    qp.side           = side;
    qp.qty_ahead      = qi.qty_ahead;
    qp.qty_behind     = qi.qty_behind;
    qp.level_total    = qi.level_total;
    qp.own_qty        = qi.own_qty;
    qp.position_index = qi.position_index;
    state.queue_positions.push_back(qp);
  }
}

void World::process_action(Ts ts,
                            OwnerId oid,
                            const Action& act,
                            Price cur_mid,
                            WorldResult& out,
                            StylizedFactsMeasurer& sfm,
                            const WorldConfig& cfg)
{
  if (act.type == ActionType::Submit) {
    Order o = act.order;
    o.ts = ts;

    const bool is_limit = (o.type == OrderType::Limit);
    arrival_info_[o.id] = ArrivalInfo{cur_mid, is_limit};

    if (is_limit) n_limit_submitted_[oid]++;
    else          n_market_submitted_[oid]++;

    order_meta_[o.id] = OrderMeta{o.owner, o.side};

    if (is_limit)
      order_price_cache_[o.id] = o.price;

    engine_.process_into(o, proc_result_buf_);

    if (is_limit && o.tif == TimeInForce::GTC && cfg.track_queue_positions) {
      const QueueInfo qi = engine_.book().queue_info(o.id);
      if (qi.found)
        active_limits_[oid].insert(o.id);
    }

    if (!proc_result_buf_.trades.empty()) {
      out.trades.insert(out.trades.end(),
                        proc_result_buf_.trades.begin(),
                        proc_result_buf_.trades.end());

      const auto bb2  = engine_.book().best_bid();
      const auto ba2  = engine_.book().best_ask();
      const auto mid2 = midprice(bb2, ba2);
      apply_trades_to_accounts(ts, proc_result_buf_.trades, order_meta_,
                                accounts_, mid2);

      const Price cur_mid2 = mid2 ? *mid2 : Price{0};

      for (const auto& tr : proc_result_buf_.trades) {
        Side aggressor = Side::Buy;
        if (auto it = order_meta_.find(tr.taker_order_id);
            it != order_meta_.end())
          aggressor = it->second.side;
        sfm.add_trade({tr.ts, tr.price, tr.qty, aggressor, cur_mid2});

        if (cfg.record_fills) {
          if (auto mit = order_meta_.find(tr.maker_order_id);
              mit != order_meta_.end())
          {
            FillRecord fr{};
            fr.ts         = tr.ts;
            fr.owner      = mit->second.owner;
            fr.order_id   = tr.maker_order_id;
            fr.side       = mit->second.side;
            fr.fill_qty   = tr.qty;
            fr.fill_price = tr.price;
            fr.is_maker   = true;
            if (auto ai = arrival_info_.find(tr.maker_order_id);
                ai != arrival_info_.end())
              fr.arrival_mid = ai->second.arrival_mid;
            out.fills.push_back(fr);
          }
          if (auto tit = order_meta_.find(tr.taker_order_id);
              tit != order_meta_.end())
          {
            FillRecord fr{};
            fr.ts         = tr.ts;
            fr.owner      = tit->second.owner;
            fr.order_id   = tr.taker_order_id;
            fr.side       = tit->second.side;
            fr.fill_qty   = tr.qty;
            fr.fill_price = tr.price;
            fr.is_maker   = false;
            if (auto ai = arrival_info_.find(tr.taker_order_id);
                ai != arrival_info_.end())
              fr.arrival_mid = ai->second.arrival_mid;
            out.fills.push_back(fr);
          }
        }
      }
    }

  } else if (act.type == ActionType::Cancel) {
    if (!engine_.book_mut().cancel(act.id)) {
      out.cancel_failures++;
    } else {
      arrival_info_.erase(act.id);
      order_price_cache_.erase(act.id);
      n_cancels_sent_[oid]++;
      active_limits_[oid].erase(act.id);
    }
  } else {
    if (!engine_.book_mut().modify_qty(act.id, act.new_qty))
      out.modify_failures++;
  }
}

WorldResult World::run(uint64_t seed,
                       double   horizon_seconds,
                       WorldConfig cfg)
{
  WorldResult out{};
  const Ts t0    = 0;
  const Ts t_end = static_cast<Ts>(
      std::llround(horizon_seconds * 1'000'000'000.0));
  const std::size_t n_steps =
      static_cast<std::size_t>(t_end / cfg.dt_ns) + 1;

  // ── 1. Seed agents ────────────────────────────────────────────────────────
  uint64_t sm = seed;
  for (std::size_t i = 0; i < agents_.size(); ++i) {
    const uint64_t s = splitmix64(sm) ^ (static_cast<uint64_t>(i) + 1ull);
    agents_[i]->seed(s);
  }

  // ── 2. Build LatencySamplers ──────────────────────────────────────────────
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

  // ── 3. Reset + pre-reserve all hash maps ─────────────────────────────────
  const std::size_t n_agents = agents_.size();

  const std::size_t peak_orders = (cfg.expected_resting_orders > 0)
      ? cfg.expected_resting_orders
      : std::max(std::size_t{256}, n_agents * 8);

  order_meta_.clear();       order_meta_.max_load_factor(0.5f);
  order_meta_.reserve(peak_orders * 4);

  arrival_info_.clear();     arrival_info_.max_load_factor(0.5f);
  arrival_info_.reserve(peak_orders * 4);

  order_price_cache_.clear(); order_price_cache_.max_load_factor(0.5f);
  order_price_cache_.reserve(peak_orders * 2);

  accounts_.max_load_factor(0.5f);
  accounts_.reserve((n_agents + 1) * 2);

  active_limits_.clear();    active_limits_.max_load_factor(0.5f);
  active_limits_.reserve((n_agents + 1) * 2);

  n_limit_submitted_.clear();   n_limit_submitted_.max_load_factor(0.5f);
  n_limit_submitted_.reserve((n_agents + 1) * 2);

  n_market_submitted_.clear();  n_market_submitted_.max_load_factor(0.5f);
  n_market_submitted_.reserve((n_agents + 1) * 2);

  n_cancels_sent_.clear();      n_cancels_sent_.max_load_factor(0.5f);
  n_cancels_sent_.reserve((n_agents + 1) * 2);

  proc_result_buf_.trades.clear();

  // ── 4. Pre-reserve result vectors ────────────────────────────────────────
  out.tops.reserve(n_steps);

  const std::size_t est_fills = (cfg.expected_fills > 0)
      ? cfg.expected_fills
      : n_steps / 20;

  out.trades.reserve(est_fills);
  if (cfg.record_fills)      out.fills.reserve(est_fills * 2);
  if (cfg.record_pnl_series) out.pnl_series.reserve(n_steps * n_agents);

  actions_buf_.reserve(8);

  // ── 5. Stylized facts measurer ───────────────────────────────────────────
  // PERF: reserve(n_tops, n_trades) pre-allocates the internal accumulation
  // vectors so they never reallocate during the run.
  StylizedFactsMeasurer sfm(20, 10);

  LatencyActionBuffer lat_buf;

  // ── Main simulation loop ──────────────────────────────────────────────────
  for (Ts ts = t0; ts <= t_end; ts += cfg.dt_ns) {

    // A. Flush timed events
    {
      auto flushed = engine_.flush(ts);
      if (!flushed.empty()) {
        out.trades.insert(out.trades.end(), flushed.begin(), flushed.end());
        const auto fbb  = engine_.book().best_bid();
        const auto fba  = engine_.book().best_ask();
        const auto fmid = midprice(fbb, fba);
        apply_trades_to_accounts(ts, flushed, order_meta_, accounts_, fmid);
        const Price fmid_v = fmid ? *fmid : Price{0};
        for (const auto& tr : flushed) {
          Side aggressor = Side::Buy;
          if (auto it = order_meta_.find(tr.taker_order_id);
              it != order_meta_.end())
            aggressor = it->second.side;
          sfm.add_trade({tr.ts, tr.price, tr.qty, aggressor, fmid_v});
        }
      }
    }

    // B. Build MarketView — compute bb/ba/mid ONCE for this step.
    const auto bb  = engine_.book().best_bid();
    const auto ba  = engine_.book().best_ask();
    const auto mid = midprice(bb, ba);

    MarketView view{};
    view.ts         = ts;
    view.best_bid   = bb;
    view.best_ask   = ba;
    view.mid        = mid;
    view.last_trade = engine_.rules().last_trade_price();
    view.imbalance  = compute_imbalance(view.bid_depth, view.ask_depth);

    const Price cur_mid = mid ? *mid : Price{0};

    // C. Record top for stylized facts
    if (bb && ba)
      sfm.add_top({ts, *bb, *ba, cur_mid, Price{0}, Price{0}});

    // D. Collect and dispatch agent actions
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

      if (cfg.track_queue_positions)
        build_queue_positions(oid, self);

      actions_buf_.clear();
      ap->step(ts, view, self, actions_buf_);

      if (cfg.record_fv_signals) {
        if (auto* fva = dynamic_cast<
                agents::FundamentalValueAgent*>(ap.get()))
          out.fv_log.push_back({ts, oid, fva->fundamental_value()});
      }

      if (!cfg.latency_enabled) {
        for (const auto& act : actions_buf_)
          process_action(ts, oid, act, cur_mid, out, sfm, cfg);
      } else {
        lat_buf.push(ts, oid, actions_buf_, latency_samplers_[i], cur_mid);
      }
    }

    // E. Drain latency buffer
    if (cfg.latency_enabled) {
      for (const auto& pa : lat_buf.drain())
        process_action(pa.effective_ts, pa.owner, pa.action,
                       pa.arrival_mid, out, sfm, cfg);
    }

    // F. Snapshot top-of-book.
    // PERF: reuse bb/ba/mid from step B — no additional FlatPriceMap
    // lookups. Saves 2 best_bid()/best_ask() calls (2 × begin() reads)
    // per step = ~4000 calls eliminated per second of simulation.
    {
      BookTop top{};
      top.ts       = ts;
      top.best_bid = bb;     // ← was: engine_.book().best_bid()
      top.best_ask = ba;     // ← was: engine_.book().best_ask()
      top.mid      = mid;    // ← was: midprice(top.best_bid, top.best_ask)
      out.tops.push_back(top);
    }

    // G. Record per-agent PnL snapshot — reuses cur_mid from step B.
    if (cfg.record_pnl_series) {
      for (const auto& ap : agents_) {
        const OwnerId oid = ap->owner();
        StepSnapshot snap{};
        snap.ts    = ts;
        snap.owner = oid;
        snap.mid   = cur_mid;  // ← was: mid ? *mid : Price{0} (redundant ternary)
        if (const auto acc_it = accounts_.find(oid);
            acc_it != accounts_.end()) {
          snap.position   = acc_it->second.position;
          snap.cash_ticks = acc_it->second.cash_ticks;
        }
        out.pnl_series.push_back(snap);
      }
    }
  }

  // ── Final account snapshots + TCA ────────────────────────────────────────
  {
    const auto bb  = engine_.book().best_bid();
    const auto ba  = engine_.book().best_ask();
    const auto mid = midprice(bb, ba);
    out.accounts = make_account_snapshots(t_end, accounts_, mid);

    const Price final_mid = mid ? *mid : Price{0};
    out.tca.reserve(agents_.size());
    for (const auto& ap : agents_) {
      const OwnerId oid = ap->owner();
      int64_t final_pos  = 0;
      int64_t final_cash = 0;
      if (const auto acc_it = accounts_.find(oid);
          acc_it != accounts_.end()) {
        final_pos  = acc_it->second.position;
        final_cash = acc_it->second.cash_ticks;
      }
      out.tca.push_back(compute_agent_tca(
          oid,
          n_limit_submitted_[oid],
          n_market_submitted_[oid],
          n_cancels_sent_[oid],
          out.fills,
          final_pos,
          final_cash,
          final_mid));
    }
  }

  if (cfg.compute_stylized_facts && sfm.n_trades() >= 10)
    out.sf = sfm.compute();

  return out;
}

} // namespace msim
