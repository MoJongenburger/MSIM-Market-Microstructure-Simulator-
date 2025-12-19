#include "msim/live_world.hpp"

#include <algorithm>
#include <cmath>

namespace msim {

LiveWorld::LiveWorld(MatchingEngine engine)
  : engine_(std::move(engine)) {}

LiveWorld::~LiveWorld() {
  stop();
}

void LiveWorld::add_agent(std::unique_ptr<IAgent> a) {
  agents_.push_back(std::move(a));
}

void LiveWorld::start(uint64_t seed, double horizon_seconds, WorldConfig cfg) {
  if (running_.load()) return;

  seed_ = seed;
  horizon_s_ = horizon_seconds;
  cfg_ = cfg;

  stop_.store(false);
  running_.store(true);

  // deterministic per-agent seeding (like World)
  {
    std::lock_guard<std::mutex> lk(engine_mtx_);
    uint64_t sm = seed_;
    auto splitmix64 = [](uint64_t& x) noexcept {
      uint64_t z = (x += 0x9e3779b97f4a7c15ull);
      z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
      z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
      return z ^ (z >> 31);
    };

    for (std::size_t i = 0; i < agents_.size(); ++i) {
      const uint64_t s = splitmix64(sm) ^ (static_cast<uint64_t>(i) + 1ull);
      agents_[i]->seed(s);
    }
  }

  worker_thread_ = std::thread([this]() { worker_(); });
}

void LiveWorld::stop() {
  if (!running_.load()) return;
  stop_.store(true);
  if (worker_thread_.joinable()) worker_thread_.join();
  running_.store(false);
}

LiveSnapshot LiveWorld::snapshot(std::size_t max_trades) const {
  std::lock_guard<std::mutex> lk(cache_mtx_);

  LiveSnapshot s{};
  s.ts = cache_.ts;
  s.best_bid = cache_.best_bid;
  s.best_ask = cache_.best_ask;
  s.mid = cache_.mid;
  s.last_trade = cache_.last_trade;

  const std::size_t n = std::min<std::size_t>(max_trades, cache_.trades.size());
  s.recent_trades.reserve(n);

  // newest-first
  for (std::size_t i = 0; i < n; ++i) {
    const auto& t = cache_.trades[cache_.trades.size() - 1 - i];
    s.recent_trades.push_back(t);
  }
  return s;
}

std::vector<LiveMidPoint> LiveWorld::mid_series(Ts window_ns) const {
  std::lock_guard<std::mutex> lk(cache_mtx_);

  std::vector<LiveMidPoint> out;
  if (cache_.tops.empty()) return out;

  const Ts t1 = cache_.tops.back().ts;
  const Ts t0 = (t1 > window_ns) ? (t1 - window_ns) : 0;

  for (const auto& x : cache_.tops) {
    if (x.ts < t0) continue;
    LiveMidPoint p{};
    p.ts = x.ts;
    p.mid = x.mid;
    out.push_back(p);
  }
  return out;
}

LiveBookDepth LiveWorld::book_depth(std::size_t levels) const {
  std::lock_guard<std::mutex> lk(cache_mtx_);
  LiveBookDepth out{};

  const std::size_t nb = std::min<std::size_t>(levels, cache_.depth.bids.size());
  const std::size_t na = std::min<std::size_t>(levels, cache_.depth.asks.size());

  // Avoid iterator arithmetic with size_t (fixes -Wsign-conversion under -Werror)
  out.bids.reserve(nb);
  for (std::size_t i = 0; i < nb; ++i) out.bids.push_back(cache_.depth.bids[i]);

  out.asks.reserve(na);
  for (std::size_t i = 0; i < na; ++i) out.asks.push_back(cache_.depth.asks[i]);

  return out;
}

OrderAck LiveWorld::submit_order(Order o) {
  if (o.owner == 0) o.owner = OwnerId{999};

  if (o.id == 0) {
    const uint32_t seq = manual_seq_.fetch_add(1);
    o.id = next_order_id_(o.owner, seq);
  }

  o.ts = cur_ts_.load();

  std::lock_guard<std::mutex> lk(engine_mtx_);
  auto res = engine_.process(o);

  OrderAck ack{};
  ack.id = o.id;
  ack.status = res.status;
  ack.reject_reason = res.reject_reason;

  update_cache_with_engine_locked_(o.ts, res.trades);
  return ack;
}

bool LiveWorld::cancel_order(OrderId id) {
  std::lock_guard<std::mutex> lk(engine_mtx_);
  const bool ok = engine_.book_mut().cancel(id);
  update_cache_with_engine_locked_(cur_ts_.load(), {});
  return ok;
}

bool LiveWorld::modify_qty(OrderId id, Qty new_qty) {
  std::lock_guard<std::mutex> lk(engine_mtx_);
  const bool ok = engine_.book_mut().modify_qty(id, new_qty);
  update_cache_with_engine_locked_(cur_ts_.load(), {});
  return ok;
}

void LiveWorld::update_cache_with_engine_locked_(Ts ts, const std::vector<Trade>& new_trades) {
  std::lock_guard<std::mutex> lk(cache_mtx_);

  cache_.ts = ts;
  cache_.best_bid = engine_.book().best_bid();
  cache_.best_ask = engine_.book().best_ask();
  cache_.mid = compute_mid_(cache_.best_bid, cache_.best_ask);
  cache_.last_trade = engine_.rules().last_trade_price();

  for (const auto& t : new_trades) {
    cache_.trades.push_back(t);
    if (cache_.trades.size() > max_cache_trades_) cache_.trades.pop_front();
  }

  BookTop top{};
  top.ts = ts;
  top.best_bid = cache_.best_bid;
  top.best_ask = cache_.best_ask;
  top.mid = cache_.mid;
  cache_.tops.push_back(top);
  if (cache_.tops.size() > max_cache_tops_) cache_.tops.pop_front();

  LiveBookDepth bd{};
  bd.bids = extract_depth_(engine_.book(), Side::Buy, depth_cache_levels_);
  bd.asks = extract_depth_(engine_.book(), Side::Sell, depth_cache_levels_);
  cache_.depth = std::move(bd);
}

void LiveWorld::worker_() {
  const Ts t_end = static_cast<Ts>(std::llround(horizon_s_ * 1'000'000'000.0));
  const Ts dt = std::max<Ts>(1, cfg_.dt_ns);

  for (Ts ts = 0; ts <= t_end; ts += dt) {
    if (stop_.load()) break;

    cur_ts_.store(ts);

    std::lock_guard<std::mutex> lk(engine_mtx_);

    auto flushed = engine_.flush(ts);
    if (!flushed.empty()) update_cache_with_engine_locked_(ts, flushed);

    const auto bb = engine_.book().best_bid();
    const auto ba = engine_.book().best_ask();
    const auto mid = compute_mid_(bb, ba);

    MarketView view{};
    view.ts = ts;
    view.best_bid = bb;
    view.best_ask = ba;
    view.mid = mid;
    view.last_trade = engine_.rules().last_trade_price();

    for (auto& ap : agents_) {
      AgentState self{};
      self.owner = ap->owner();

      std::vector<Action> actions;
      actions.reserve(8);

      ap->step(ts, view, self, actions);

      for (auto act : actions) {
        if (act.type == ActionType::Submit) {
          Order o = act.order;
          o.ts = ts;
          auto res = engine_.process(o);
          if (!res.trades.empty()) update_cache_with_engine_locked_(ts, res.trades);
        } else if (act.type == ActionType::Cancel) {
          engine_.book_mut().cancel(act.id);
          update_cache_with_engine_locked_(ts, {});
        } else {
          engine_.book_mut().modify_qty(act.id, act.new_qty);
          update_cache_with_engine_locked_(ts, {});
        }
      }
    }

    // keep chart fed even on "quiet" steps
    update_cache_with_engine_locked_(ts, {});
  }

  running_.store(false);
}

} // namespace msim
