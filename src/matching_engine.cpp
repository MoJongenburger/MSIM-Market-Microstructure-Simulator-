// ============================================================
// src/matching_engine.cpp
//
// Performance changes vs previous version:
//
//   flush() fast path
//     Returns {} immediately when ts < next_event_ts_.
//     One comparison replaces three condition checks.
//
//   match_buy / match_sell: tombstone-skip loop
//     Before each match step, advance front_offset past any
//     orders with qty == 0 (cancelled tombstones).
//     In the typical case (no cancels at front) the while loop
//     executes zero iterations.
//
//   process_into() simplified
//     SmallVector makes the common case (0-4 trades) inline,
//     so process_into() is now just out = process(incoming).
//
//   update_next_event_ts_()
//     Recomputes next_event_ts_ after any phase-end timestamp
//     changes.  Called from start_trading_at_last(),
//     start_closing_auction(), maybe_trigger_circuit_breaker(),
//     flush(), and the volatility-auction trigger in process().
// ============================================================
#include "msim/matching_engine.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <cstdlib>
#include <span>

namespace msim {

static Price mul_div_bps(Price x, int32_t num_bps, int32_t den_bps) noexcept {
  return static_cast<Price>((static_cast<int64_t>(x) * num_bps) / den_bps);
}

// ── update_next_event_ts_ ────────────────────────────────────────────────────
// Scans the three phase-end timestamps and sets next_event_ts_ to the
// earliest non-zero one.  Zero means "not set".
void MatchingEngine::update_next_event_ts_() noexcept {
  Ts next = std::numeric_limits<Ts>::max();
  if (tal_end_ts_     > 0) next = std::min(next, tal_end_ts_);
  if (halt_end_ts_    > 0) next = std::min(next, halt_end_ts_);
  if (auction_end_ts_ > 0) next = std::min(next, auction_end_ts_);
  next_event_ts_ = next;
}

// ── start_trading_at_last ────────────────────────────────────────────────────
void MatchingEngine::start_trading_at_last(Ts end_ts) noexcept {
  tal_end_ts_ = end_ts;
  rules_.set_phase(MarketPhase::TradingAtLast);
  update_next_event_ts_();
}

// ── start_closing_auction ────────────────────────────────────────────────────
void MatchingEngine::start_closing_auction(Ts end_ts) noexcept {
  auction_end_ts_ = end_ts;
  rules_.set_phase(MarketPhase::ClosingAuction);
  update_next_event_ts_();
}

// ── maybe_trigger_circuit_breaker ────────────────────────────────────────────
void MatchingEngine::maybe_trigger_circuit_breaker(std::span<const Trade> trades) {
  const auto& cfg = rules_.config();
  if (!cfg.enable_circuit_breaker) return;
  if (trades.empty()) return;
  if (rules_.phase() != MarketPhase::Continuous) return;

  if (!cb_ref_price_) cb_ref_price_ = trades.front().price;

  const Price ref   = *cb_ref_price_;
  const Price lower = mul_div_bps(ref, 10000 - cfg.cb_drop_bps, 10000);

  const Trade& last = trades.back();
  if (last.price > lower) return;

  rules_.set_phase(MarketPhase::Halted);
  halt_end_ts_           = last.ts + cfg.cb_halt_duration_ns;
  reopen_auction_end_ts_ = halt_end_ts_ + cfg.cb_reopen_auction_duration_ns;

  for (auto& [px, lvl] : book_.bids_)
    for (auto& o : lvl.q) if (o.qty > 0) auction_queue_.push_back(o);
  for (auto& [px, lvl] : book_.asks_)
    for (auto& o : lvl.q) if (o.qty > 0) auction_queue_.push_back(o);

  book_.bids_.clear();
  book_.asks_.clear();
  book_.loc_.clear();

  auction_end_ts_ = reopen_auction_end_ts_;
  update_next_event_ts_();   // halt_end_ts_ and auction_end_ts_ are now set
}

// ── flush ─────────────────────────────────────────────────────────────────────
// Fast path: no pending events → return empty, zero allocation.
std::vector<Trade> MatchingEngine::flush(Ts ts) {
  if (ts < next_event_ts_) return {};   // ← hot path in continuous trading

  std::vector<Trade> out;

  if (rules_.phase() == MarketPhase::TradingAtLast
      && tal_end_ts_ > 0 && ts >= tal_end_ts_) {
    rules_.set_phase(MarketPhase::Continuous);
    tal_end_ts_ = 0;
  }

  if (rules_.phase() == MarketPhase::Halted
      && halt_end_ts_ > 0 && ts >= halt_end_ts_) {
    rules_.set_phase(MarketPhase::Auction);
    halt_end_ts_ = 0;
  }

  if ((rules_.phase() == MarketPhase::Auction
       || rules_.phase() == MarketPhase::ClosingAuction)
      && auction_end_ts_ > 0 && ts >= auction_end_ts_) {

    out = uncross_auction(auction_end_ts_);

    if (rules_.phase() == MarketPhase::ClosingAuction)
      rules_.set_phase(MarketPhase::Closed);
    else
      rules_.set_phase(MarketPhase::Continuous);

    auction_end_ts_ = 0;
    rules_.on_trades(out);
    maybe_trigger_circuit_breaker(out);   // updates next_event_ts_ internally
  }

  update_next_event_ts_();   // recompute after all changes above
  return out;
}

// ── make_trade ───────────────────────────────────────────────────────────────
Trade MatchingEngine::make_trade(Ts ts, Price px, Qty q,
                                  OrderId maker, OrderId taker) {
  Trade t{};
  t.id             = next_trade_id_++;
  t.ts             = ts;
  t.price          = px;
  t.qty            = q;
  t.maker_order_id = maker;
  t.taker_order_id = taker;
  return t;
}

// ── available_liquidity ───────────────────────────────────────────────────────
Qty MatchingEngine::available_liquidity(const Order& taker) const noexcept {
  Qty avail = 0;
  if (taker.side == Side::Buy) {
    for (auto it = book_.asks_.begin(); it != book_.asks_.end(); ++it) {
      const Price px = it->first;
      if (taker.type == OrderType::Limit && px > taker.price) break;
      for (const auto& o : it->second.q) {
        avail += o.qty;
        if (avail >= taker.qty) return avail;
      }
    }
  } else {
    for (auto it = book_.bids_.begin(); it != book_.bids_.end(); ++it) {
      const Price px = it->first;
      if (taker.type == OrderType::Limit && px < taker.price) break;
      for (const auto& o : it->second.q) {
        avail += o.qty;
        if (avail >= taker.qty) return avail;
      }
    }
  }
  return avail;
}

// ── reference_price / first_execution_price / band helpers ───────────────────
std::optional<Price> MatchingEngine::reference_price() const noexcept {
  if (auto lt = rules_.last_trade_price(); lt) return lt;
  return midprice(book_.best_bid(), book_.best_ask());
}

std::optional<Price> MatchingEngine::first_execution_price(
    const Order& incoming) const noexcept {
  if (incoming.side == Side::Buy) {
    auto ba = book_.best_ask();
    if (!ba) return std::nullopt;
    if (incoming.type == OrderType::Market) return ba;
    if (incoming.price >= *ba) return ba;
    return std::nullopt;
  } else {
    auto bb = book_.best_bid();
    if (!bb) return std::nullopt;
    if (incoming.type == OrderType::Market) return bb;
    if (incoming.price <= *bb) return bb;
    return std::nullopt;
  }
}

bool MatchingEngine::breaches_price_band(Price exec_px,
                                          Price ref_px) const noexcept {
  const auto& cfg = rules_.config();
  if (!cfg.enable_price_bands) return false;
  const int32_t B   = cfg.band_bps;
  const Price lower = mul_div_bps(ref_px, 10000 - B, 10000);
  const Price upper = mul_div_bps(ref_px, 10000 + B, 10000);
  return (exec_px < lower) || (exec_px > upper);
}

bool MatchingEngine::should_trigger_volatility_auction(
    const Order& incoming) const noexcept {
  const auto& cfg = rules_.config();
  if (!cfg.enable_volatility_interruption)      return false;
  if (rules_.phase() != MarketPhase::Continuous) return false;
  auto exec_px = first_execution_price(incoming);
  if (!exec_px) return false;
  auto ref_px  = reference_price();
  if (!ref_px)  return false;
  return breaches_price_band(*exec_px, *ref_px);
}

// ── queue_in_auction ──────────────────────────────────────────────────────────
MatchResult MatchingEngine::queue_in_auction(Order incoming) {
  MatchResult out{};
  auction_queue_.push_back(std::move(incoming));
  return out;
}

// ── executable_volume_at / compute_clearing_price ────────────────────────────
Qty MatchingEngine::executable_volume_at(Price px) const noexcept {
  Qty buy = 0, sell = 0;
  for (const auto& o : auction_queue_) {
    if (o.type == OrderType::Market) {
      if (o.side == Side::Buy) buy  += o.qty;
      else                     sell += o.qty;
      continue;
    }
    if (o.side == Side::Buy)  { if (o.price >= px) buy  += o.qty; }
    else                      { if (o.price <= px) sell += o.qty; }
  }
  return std::min(buy, sell);
}

std::optional<Price> MatchingEngine::compute_clearing_price() const noexcept {
  if (auction_queue_.empty()) return std::nullopt;
  std::set<Price> candidates;
  for (const auto& o : auction_queue_)
    if (o.type == OrderType::Limit) candidates.insert(o.price);
  if (candidates.empty()) return std::nullopt;

  const auto ref    = reference_price();
  Qty        best_v = -1;
  Price      best_p = *candidates.begin();

  for (Price px : candidates) {
    const Qty v = executable_volume_at(px);
    if (v > best_v) { best_v = v; best_p = px; }
    else if (v == best_v && ref) {
      if (std::abs(px - *ref) < std::abs(best_p - *ref)) best_p = px;
    } else if (v == best_v && px < best_p) {
      best_p = px;
    }
  }
  if (best_v <= 0) return std::nullopt;
  return best_p;
}

// ── uncross_auction ───────────────────────────────────────────────────────────
std::vector<Trade> MatchingEngine::uncross_auction(Ts uncross_ts) {
  std::vector<Trade> trades;
  if (auction_queue_.empty()) return trades;

  const auto px_opt = compute_clearing_price();
  if (!px_opt) {
    for (auto& o : auction_queue_)
      if (o.type == OrderType::Limit && o.qty > 0) {
        o.ts = uncross_ts;
        (void)book_.add_resting_limit(o);
      }
    auction_queue_.clear();
    return trades;
  }

  const Price clr = *px_opt;
  std::vector<Order> buys, sells;

  for (const auto& o : auction_queue_) {
    Order c = o; c.ts = uncross_ts;
    if (c.side == Side::Buy  && (c.type == OrderType::Market || c.price >= clr)) buys.push_back(c);
    if (c.side == Side::Sell && (c.type == OrderType::Market || c.price <= clr)) sells.push_back(c);
  }

  auto pri = [](const Order& a, const Order& b) {
    return a.ts != b.ts ? a.ts < b.ts : a.id < b.id;
  };
  std::sort(buys.begin(),  buys.end(),  pri);
  std::sort(sells.begin(), sells.end(), pri);

  std::size_t i = 0, j = 0;
  while (i < buys.size() && j < sells.size()) {
    auto& b = buys[i]; auto& s = sells[j];
    if (b.qty <= 0) { ++i; continue; }
    if (s.qty <= 0) { ++j; continue; }
    const Qty q = std::min(b.qty, s.qty);
    trades.push_back(make_trade(uncross_ts, clr, q, s.id, b.id));
    b.qty -= q; s.qty -= q;
  }

  for (auto& b : buys)
    if (b.qty > 0 && b.type == OrderType::Limit) (void)book_.add_resting_limit(b);
  for (auto& s : sells)
    if (s.qty > 0 && s.type == OrderType::Limit) (void)book_.add_resting_limit(s);

  for (auto& o : auction_queue_) {
    if (o.type != OrderType::Limit || o.qty <= 0) continue;
    const bool elig = (o.side == Side::Buy) ? (o.price >= clr) : (o.price <= clr);
    if (!elig) { o.ts = uncross_ts; (void)book_.add_resting_limit(o); }
  }

  auction_queue_.clear();
  return trades;
}

// ── match_buy ─────────────────────────────────────────────────────────────────
// Key changes vs previous version:
//   1. Access best ask level via q[front_offset] not q.front().
//   2. Tombstone-skip loop before each match step: advance front_offset
//      past any orders with qty==0 (tombstoned by cancel()).
//   3. On full consumption: ++front_offset (not pop_front()).
void MatchingEngine::match_buy(MatchResult& out, Order& taker) {
  while (taker.qty > 0 && !book_.asks_.empty()) {
    auto best_it = book_.asks_.begin();
    const Price best_ask_px = best_it->first;

    if (taker.type == OrderType::Limit && best_ask_px > taker.price) break;

    auto& lvl = best_it->second;

    // Skip tombstones at front — O(k), typically k=0
    while (lvl.front_offset < lvl.q.size()
           && lvl.q[lvl.front_offset].qty == 0)
      ++lvl.front_offset;

    // Level exhausted (all cancelled or consumed)
    if (lvl.front_offset >= lvl.q.size()) {
      book_.asks_.erase(best_it);
      continue;
    }

    // Self-trade prevention
    if (rules_.config().stp != StpMode::None) {
      const OwnerId maker_owner = lvl.q[lvl.front_offset].owner;
      if (maker_owner == taker.owner) {
        if (rules_.config().stp == StpMode::CancelTaker) {
          taker.qty = 0;
          return;
        }
        // CancelMaker: tombstone the front order and try again
        const OrderId maker_id = lvl.q[lvl.front_offset].id;
        (void)book_.cancel(maker_id);   // sets qty=0, updates total_qty
        continue;                        // reacquire best_it next iteration
      }
    }

    auto& maker = lvl.q[lvl.front_offset];
    const Qty q = std::min(taker.qty, maker.qty);

    out.trades.push_back(make_trade(taker.ts, best_ask_px, q, maker.id, taker.id));

    taker.qty -= q;
    maker.qty -= q;
    lvl.total_qty -= q;

    if (maker.qty == 0) {
      book_.erase_locator(maker.id);
      ++lvl.front_offset;   // advance past consumed order (no pop_front needed)
    }
    if (lvl.total_qty == 0) book_.asks_.erase(best_it);
  }
}

// ── match_sell ────────────────────────────────────────────────────────────────
void MatchingEngine::match_sell(MatchResult& out, Order& taker) {
  while (taker.qty > 0 && !book_.bids_.empty()) {
    auto best_it = book_.bids_.begin();
    const Price best_bid_px = best_it->first;

    if (taker.type == OrderType::Limit && best_bid_px < taker.price) break;

    auto& lvl = best_it->second;

    // Skip tombstones at front
    while (lvl.front_offset < lvl.q.size()
           && lvl.q[lvl.front_offset].qty == 0)
      ++lvl.front_offset;

    if (lvl.front_offset >= lvl.q.size()) {
      book_.bids_.erase(best_it);
      continue;
    }

    if (rules_.config().stp != StpMode::None) {
      const OwnerId maker_owner = lvl.q[lvl.front_offset].owner;
      if (maker_owner == taker.owner) {
        if (rules_.config().stp == StpMode::CancelTaker) {
          taker.qty = 0;
          return;
        }
        const OrderId maker_id = lvl.q[lvl.front_offset].id;
        (void)book_.cancel(maker_id);
        continue;
      }
    }

    auto& maker = lvl.q[lvl.front_offset];
    const Qty q = std::min(taker.qty, maker.qty);

    out.trades.push_back(make_trade(taker.ts, best_bid_px, q, maker.id, taker.id));

    taker.qty -= q;
    maker.qty -= q;
    lvl.total_qty -= q;

    if (maker.qty == 0) {
      book_.erase_locator(maker.id);
      ++lvl.front_offset;
    }
    if (lvl.total_qty == 0) book_.bids_.erase(best_it);
  }
}

// ── process_market / process_limit ───────────────────────────────────────────
MatchResult MatchingEngine::process_market(Order incoming) {
  MatchResult out{};
  if (incoming.qty <= 0) return out;

  if (incoming.side == Side::Buy) match_buy(out, incoming);
  else                            match_sell(out, incoming);

  out.filled_qty = 0;
  for (const auto& tr : out.trades) out.filled_qty += tr.qty;

  if (incoming.mkt_style == MarketStyle::MarketToLimit
      && incoming.qty > 0 && !out.trades.empty()) {
    Order rest     = incoming;
    rest.type      = OrderType::Limit;
    rest.price     = out.trades.back().price;
    rest.tif       = TimeInForce::GTC;
    rest.mkt_style = MarketStyle::PureMarket;
    if (book_.add_resting_limit(rest)) out.resting = rest;
  }
  return out;
}

MatchResult MatchingEngine::process_limit(Order incoming) {
  MatchResult out{};
  if (incoming.qty <= 0) return out;

  if (incoming.side == Side::Buy) match_buy(out, incoming);
  else                            match_sell(out, incoming);

  out.filled_qty = 0;
  for (const auto& tr : out.trades) out.filled_qty += tr.qty;

  if (incoming.tif == TimeInForce::IOC) return out;
  if (incoming.qty > 0)
    if (book_.add_resting_limit(incoming)) out.resting = incoming;
  return out;
}

// ── process ───────────────────────────────────────────────────────────────────
MatchResult MatchingEngine::process(Order incoming) {
  MatchResult out{};

  // flush() fast path: returns {} immediately when ts < next_event_ts_
  auto flushed = flush(incoming.ts);
  if (!flushed.empty())
    out.trades.insert(out.trades.end(), flushed.begin(), flushed.end());

  const auto decision = rules_.pre_accept(incoming);
  if (!decision.accept) {
    out.status        = OrderStatus::Rejected;
    out.reject_reason = decision.reason;
    return out;
  }

  if (rules_.phase() == MarketPhase::Closed) return out;

  if (rules_.phase() == MarketPhase::Halted) {
    if (!rules_.config().queue_orders_during_halt) {
      out.status        = OrderStatus::Rejected;
      out.reject_reason = RejectReason::MarketHalted;
      return out;
    }
    (void)queue_in_auction(std::move(incoming));
    return out;
  }

  if (rules_.phase() == MarketPhase::TradingAtLast) {
    const auto last = rules_.last_trade_price();
    if (!last) {
      out.status        = OrderStatus::Rejected;
      out.reject_reason = RejectReason::NoReferencePrice;
      return out;
    }
    if (incoming.type == OrderType::Limit && incoming.price != *last) {
      out.status        = OrderStatus::Rejected;
      out.reject_reason = RejectReason::PriceNotAtLast;
      return out;
    }
    incoming.type  = OrderType::Limit;
    incoming.price = *last;
    auto r = process_limit(std::move(incoming));
    out.trades.insert(out.trades.end(), r.trades.begin(), r.trades.end());
    out.resting    = r.resting;
    out.filled_qty = 0;
    for (const auto& tr : out.trades) out.filled_qty += tr.qty;
    rules_.on_trades(out.trades);
    maybe_trigger_circuit_breaker(out.trades);
    return out;
  }

  if (rules_.phase() == MarketPhase::Auction
      || rules_.phase() == MarketPhase::ClosingAuction) {
    (void)queue_in_auction(std::move(incoming));
    return out;
  }

  if (should_trigger_volatility_auction(incoming)) {
    rules_.set_phase(MarketPhase::Auction);
    auction_end_ts_ = incoming.ts + rules_.config().vol_auction_duration_ns;
    update_next_event_ts_();   // auction_end_ts_ just set
    (void)queue_in_auction(std::move(incoming));
    return out;
  }

  if (incoming.tif == TimeInForce::FOK) {
    const Qty avail = available_liquidity(incoming);
    if (avail < incoming.qty) {
      rules_.on_trades(out.trades);
      maybe_trigger_circuit_breaker(out.trades);
      return out;
    }
  }

  MatchResult r = (incoming.type == OrderType::Market)
      ? process_market(std::move(incoming))
      : process_limit(std::move(incoming));

  out.trades.insert(out.trades.end(), r.trades.begin(), r.trades.end());
  out.resting    = r.resting;
  out.filled_qty = 0;
  for (const auto& tr : out.trades) out.filled_qty += tr.qty;

  rules_.on_trades(out.trades);
  maybe_trigger_circuit_breaker(out.trades);
  return out;
}

// ── process_into ─────────────────────────────────────────────────────────────
// With SmallVector<Trade, 4>, the common case (0–4 fills) stores
// data inline — move assignment is a cheap memcpy of the inline buffer,
// not a heap pointer transfer.  No saved-capacity dance needed.
void MatchingEngine::process_into(Order incoming, MatchResult& out) {
  out = process(std::move(incoming));
}

}  // namespace msim
