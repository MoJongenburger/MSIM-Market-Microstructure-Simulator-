#include "msim/matching_engine.hpp"
#include <algorithm>
#include <set>
#include <cstdlib>
#include <span>

namespace msim {

static Price mul_div_bps(Price x, int32_t num_bps, int32_t den_bps) noexcept {
  return static_cast<Price>((static_cast<int64_t>(x) * num_bps) / den_bps);
}

void MatchingEngine::start_trading_at_last(Ts end_ts) noexcept {
  tal_end_ts_ = end_ts;
  rules_.set_phase(MarketPhase::TradingAtLast);
}

void MatchingEngine::start_closing_auction(Ts end_ts) noexcept {
  auction_end_ts_ = end_ts;
  rules_.set_phase(MarketPhase::ClosingAuction);
}

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
    for (auto& o : lvl.q) auction_queue_.push_back(std::move(o));
  for (auto& [px, lvl] : book_.asks_)
    for (auto& o : lvl.q) auction_queue_.push_back(std::move(o));

  book_.bids_.clear();
  book_.asks_.clear();
  book_.loc_.clear();

  auction_end_ts_ = reopen_auction_end_ts_;
}

std::vector<Trade> MatchingEngine::flush(Ts ts) {
  std::vector<Trade> out;

  if (rules_.phase() == MarketPhase::TradingAtLast
      && tal_end_ts_ > 0 && ts >= tal_end_ts_)
    rules_.set_phase(MarketPhase::Continuous);

  if (rules_.phase() == MarketPhase::Halted
      && halt_end_ts_ > 0 && ts >= halt_end_ts_)
    rules_.set_phase(MarketPhase::Auction);

  if ((rules_.phase() == MarketPhase::Auction
       || rules_.phase() == MarketPhase::ClosingAuction)
      && auction_end_ts_ > 0 && ts >= auction_end_ts_)
  {
    out = uncross_auction(auction_end_ts_);

    if (rules_.phase() == MarketPhase::ClosingAuction)
      rules_.set_phase(MarketPhase::Closed);
    else
      rules_.set_phase(MarketPhase::Continuous);

    rules_.on_trades(out);
    maybe_trigger_circuit_breaker(out);
  }

  return out;
}

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

Qty MatchingEngine::available_liquidity(const Order& taker) const noexcept {
  Qty avail = 0;
  if (taker.side == Side::Buy) {
    for (auto it = book_.asks_.begin(); it != book_.asks_.end(); ++it) {
      if (taker.type == OrderType::Limit && it->first > taker.price) break;
      for (const auto& o : it->second.q) {
        avail += o.qty;
        if (avail >= taker.qty) return avail;
      }
    }
  } else {
    for (auto it = book_.bids_.begin(); it != book_.bids_.end(); ++it) {
      if (taker.type == OrderType::Limit && it->first < taker.price) break;
      for (const auto& o : it->second.q) {
        avail += o.qty;
        if (avail >= taker.qty) return avail;
      }
    }
  }
  return avail;
}

std::optional<Price> MatchingEngine::reference_price() const noexcept {
  if (auto lt = rules_.last_trade_price(); lt) return lt;
  return midprice(book_.best_bid(), book_.best_ask());
}

std::optional<Price> MatchingEngine::first_execution_price(
    const Order& incoming) const noexcept
{
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
    const Order& incoming) const noexcept
{
  const auto& cfg = rules_.config();
  if (!cfg.enable_volatility_interruption) return false;
  if (rules_.phase() != MarketPhase::Continuous) return false;
  auto exec_px = first_execution_price(incoming);
  if (!exec_px) return false;
  auto ref_px = reference_price();
  if (!ref_px) return false;
  return breaches_price_band(*exec_px, *ref_px);
}

MatchResult MatchingEngine::queue_in_auction(Order incoming) {
  MatchResult out{};
  auction_queue_.push_back(std::move(incoming));
  return out;
}

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

  const auto ref  = reference_price();
  Qty        best_vol = -1;
  Price      best_px  = *candidates.begin();

  for (Price px : candidates) {
    const Qty v = executable_volume_at(px);
    if (v > best_vol) {
      best_vol = v;
      best_px  = px;
    } else if (v == best_vol && ref) {
      const auto d_best = std::abs(best_px - *ref);
      const auto d_cur  = std::abs(px      - *ref);
      if (d_cur < d_best) best_px = px;
    } else if (v == best_vol) {
      if (px < best_px) best_px = px;
    }
  }

  if (best_vol <= 0) return std::nullopt;
  return best_px;
}

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

  const Price clearing_px = *px_opt;
  std::vector<Order> buys, sells;

  for (const auto& o : auction_queue_) {
    Order copy = o;
    copy.ts = uncross_ts;
    const bool eb = (copy.side == Side::Buy)  &&
                    (copy.type == OrderType::Market || copy.price >= clearing_px);
    const bool es = (copy.side == Side::Sell) &&
                    (copy.type == OrderType::Market || copy.price <= clearing_px);
    if (eb) buys.push_back(copy);
    if (es) sells.push_back(copy);
  }

  auto pri = [](const Order& a, const Order& b) {
    return a.ts != b.ts ? a.ts < b.ts : a.id < b.id;
  };
  std::sort(buys.begin(),  buys.end(),  pri);
  std::sort(sells.begin(), sells.end(), pri);

  std::size_t i = 0, j = 0;
  while (i < buys.size() && j < sells.size()) {
    auto& b = buys[i];
    auto& s = sells[j];
    if (b.qty <= 0) { ++i; continue; }
    if (s.qty <= 0) { ++j; continue; }
    const Qty q = std::min(b.qty, s.qty);
    trades.push_back(make_trade(uncross_ts, clearing_px, q, s.id, b.id));
    b.qty -= q;
    s.qty -= q;
  }

  for (auto& b : buys)
    if (b.qty > 0 && b.type == OrderType::Limit) (void)book_.add_resting_limit(b);
  for (auto& s : sells)
    if (s.qty > 0 && s.type == OrderType::Limit) (void)book_.add_resting_limit(s);

  for (auto& o : auction_queue_) {
    if (o.type != OrderType::Limit || o.qty <= 0) continue;
    const bool eligible = (o.side == Side::Buy)
        ? (o.price >= clearing_px) : (o.price <= clearing_px);
    if (!eligible) {
      o.ts = uncross_ts;
      (void)book_.add_resting_limit(o);
    }
  }

  auction_queue_.clear();
  return trades;
}

// ── process() — original, unchanged ──────────────────────────────────────────
MatchResult MatchingEngine::process(Order incoming) {
  MatchResult out{};

  auto flushed = flush(incoming.ts);
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
    out.resting = r.resting;
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

  MatchResult r{};
  if (incoming.type == OrderType::Market) r = process_market(std::move(incoming));
  else                                    r = process_limit (std::move(incoming));

  out.trades.insert(out.trades.end(), r.trades.begin(), r.trades.end());
  out.resting    = r.resting;
  out.filled_qty = 0;
  for (const auto& tr : out.trades) out.filled_qty += tr.qty;

  rules_.on_trades(out.trades);
  maybe_trigger_circuit_breaker(out.trades);
  return out;
}

// ── process_into() — reuses out.trades capacity ───────────────────────────────
// Identical logic to process() but writes into a pre-allocated MatchResult.
// The caller clears out.trades before the run loop; the vector retains capacity
// and never reallocates after the first warm-up order.
void MatchingEngine::process_into(Order incoming, MatchResult& out) {
  out.trades.clear();
  out.resting      = std::nullopt;
  out.filled_qty   = 0;
  out.status       = OrderStatus::Accepted;
  out.reject_reason= RejectReason::None;

  auto flushed = flush(incoming.ts);
  out.trades.insert(out.trades.end(), flushed.begin(), flushed.end());

  const auto decision = rules_.pre_accept(incoming);
  if (!decision.accept) {
    out.status        = OrderStatus::Rejected;
    out.reject_reason = decision.reason;
    return;
  }

  if (rules_.phase() == MarketPhase::Closed) return;

  if (rules_.phase() == MarketPhase::Halted) {
    if (!rules_.config().queue_orders_during_halt) {
      out.status        = OrderStatus::Rejected;
      out.reject_reason = RejectReason::MarketHalted;
      return;
    }
    (void)queue_in_auction(std::move(incoming));
    return;
  }

  if (rules_.phase() == MarketPhase::TradingAtLast) {
    const auto last = rules_.last_trade_price();
    if (!last) {
      out.status        = OrderStatus::Rejected;
      out.reject_reason = RejectReason::NoReferencePrice;
      return;
    }
    if (incoming.type == OrderType::Limit && incoming.price != *last) {
      out.status        = OrderStatus::Rejected;
      out.reject_reason = RejectReason::PriceNotAtLast;
      return;
    }
    incoming.type  = OrderType::Limit;
    incoming.price = *last;
    auto r = process_limit(std::move(incoming));
    out.trades.insert(out.trades.end(), r.trades.begin(), r.trades.end());
    out.resting = r.resting;
    for (const auto& tr : out.trades) out.filled_qty += tr.qty;
    rules_.on_trades(out.trades);
    maybe_trigger_circuit_breaker(out.trades);
    return;
  }

  if (rules_.phase() == MarketPhase::Auction
      || rules_.phase() == MarketPhase::ClosingAuction) {
    (void)queue_in_auction(std::move(incoming));
    return;
  }

  if (should_trigger_volatility_auction(incoming)) {
    rules_.set_phase(MarketPhase::Auction);
    auction_end_ts_ = incoming.ts + rules_.config().vol_auction_duration_ns;
    (void)queue_in_auction(std::move(incoming));
    return;
  }

  if (incoming.tif == TimeInForce::FOK) {
    const Qty avail = available_liquidity(incoming);
    if (avail < incoming.qty) {
      rules_.on_trades(out.trades);
      maybe_trigger_circuit_breaker(out.trades);
      return;
    }
  }

  MatchResult r{};
  if (incoming.type == OrderType::Market) r = process_market(std::move(incoming));
  else                                    r = process_limit (std::move(incoming));

  out.trades.insert(out.trades.end(), r.trades.begin(), r.trades.end());
  out.resting = r.resting;
  for (const auto& tr : out.trades) out.filled_qty += tr.qty;

  rules_.on_trades(out.trades);
  maybe_trigger_circuit_breaker(out.trades);
}

MatchResult MatchingEngine::process_market(Order incoming) {
  MatchResult out{};
  if (incoming.qty <= 0) return out;

  if (incoming.side == Side::Buy) match_buy (out, incoming);
  else                            match_sell(out, incoming);

  for (const auto& tr : out.trades) out.filled_qty += tr.qty;

  if (incoming.mkt_style == MarketStyle::MarketToLimit
      && incoming.qty > 0 && !out.trades.empty()) {
    Order rest    = incoming;
    rest.type     = OrderType::Limit;
    rest.price    = out.trades.back().price;
    rest.tif      = TimeInForce::GTC;
    rest.mkt_style= MarketStyle::PureMarket;
    if (book_.add_resting_limit(rest)) out.resting = rest;
  }

  return out;
}

MatchResult MatchingEngine::process_limit(Order incoming) {
  MatchResult out{};
  if (incoming.qty <= 0) return out;

  if (incoming.side == Side::Buy) match_buy (out, incoming);
  else                            match_sell(out, incoming);

  for (const auto& tr : out.trades) out.filled_qty += tr.qty;

  if (incoming.tif == TimeInForce::IOC) return out;

  if (incoming.qty > 0)
    if (book_.add_resting_limit(incoming)) out.resting = incoming;

  return out;
}

void MatchingEngine::match_buy(MatchResult& out, Order& taker) {
  while (taker.qty > 0 && !book_.asks_.empty()) {
    auto best_it         = book_.asks_.begin();
    const Price best_ask = best_it->first;

    if (taker.type == OrderType::Limit && best_ask > taker.price) break;

    auto& lvl = best_it->second;
    if (lvl.q.empty()) { book_.asks_.erase(best_it); continue; }

    if (rules_.config().stp != StpMode::None) {
      const OwnerId maker_owner = lvl.q.front().owner;
      if (maker_owner == taker.owner) {
        if (rules_.config().stp == StpMode::CancelTaker) { taker.qty = 0; return; }
        (void)book_.cancel(lvl.q.front().id);
        continue;
      }
    }

    auto& maker    = lvl.q.front();
    const Qty q    = std::min(taker.qty, maker.qty);
    out.trades.push_back(make_trade(taker.ts, best_ask, q, maker.id, taker.id));

    taker.qty     -= q;
    maker.qty     -= q;
    lvl.total_qty -= q;

    if (maker.qty    == 0) { book_.erase_locator(maker.id); lvl.q.pop_front(); }
    if (lvl.total_qty== 0) book_.asks_.erase(best_it);
  }
}

void MatchingEngine::match_sell(MatchResult& out, Order& taker) {
  while (taker.qty > 0 && !book_.bids_.empty()) {
    auto best_it         = book_.bids_.begin();
    const Price best_bid = best_it->first;

    if (taker.type == OrderType::Limit && best_bid < taker.price) break;

    auto& lvl = best_it->second;
    if (lvl.q.empty()) { book_.bids_.erase(best_it); continue; }

    if (rules_.config().stp != StpMode::None) {
      const OwnerId maker_owner = lvl.q.front().owner;
      if (maker_owner == taker.owner) {
        if (rules_.config().stp == StpMode::CancelTaker) { taker.qty = 0; return; }
        (void)book_.cancel(lvl.q.front().id);
        continue;
      }
    }

    auto& maker    = lvl.q.front();
    const Qty q    = std::min(taker.qty, maker.qty);
    out.trades.push_back(make_trade(taker.ts, best_bid, q, maker.id, taker.id));

    taker.qty     -= q;
    maker.qty     -= q;
    lvl.total_qty -= q;

    if (maker.qty    == 0) { book_.erase_locator(maker.id); lvl.q.pop_front(); }
    if (lvl.total_qty== 0) book_.bids_.erase(best_it);
  }
}

} // namespace msim
