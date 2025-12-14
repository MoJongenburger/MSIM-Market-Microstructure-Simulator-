#include "msim/matching_engine.hpp"
#include <algorithm>
#include <set>
#include <cstdlib> // std::abs

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

std::vector<Trade> MatchingEngine::flush(Ts ts) {
  std::vector<Trade> out;

  // TAL expiry -> back to Continuous (session controller decides next phase)
  if (rules_.phase() == MarketPhase::TradingAtLast && tal_end_ts_ > 0 && ts >= tal_end_ts_) {
    rules_.set_phase(MarketPhase::Continuous);
  }

  // Auction expiry -> uncross (Auction => Continuous, ClosingAuction => Closed)
  if ((rules_.phase() == MarketPhase::Auction || rules_.phase() == MarketPhase::ClosingAuction) &&
      auction_end_ts_ > 0 && ts >= auction_end_ts_) {

    out = uncross_auction(auction_end_ts_);

    if (rules_.phase() == MarketPhase::ClosingAuction) {
      rules_.set_phase(MarketPhase::Closed);
    } else {
      rules_.set_phase(MarketPhase::Continuous);
    }

    rules_.on_trades(out);
  }

  return out;
}

Trade MatchingEngine::make_trade(Ts ts, Price px, Qty q, OrderId maker, OrderId taker) {
  Trade t{};
  t.id = next_trade_id_++;
  t.ts = ts;
  t.price = px;
  t.qty = q;
  t.maker_order_id = maker;
  t.taker_order_id = taker;
  return t;
}

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

std::optional<Price> MatchingEngine::reference_price() const noexcept {
  if (auto lt = rules_.last_trade_price(); lt) return lt;
  return midprice(book_.best_bid(), book_.best_ask());
}

std::optional<Price> MatchingEngine::first_execution_price(const Order& incoming) const noexcept {
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

bool MatchingEngine::breaches_price_band(Price exec_px, Price ref_px) const noexcept {
  const auto& cfg = rules_.config();
  if (!cfg.enable_price_bands) return false;

  const int32_t B = cfg.band_bps;
  const Price lower = mul_div_bps(ref_px, 10000 - B, 10000);
  const Price upper = mul_div_bps(ref_px, 10000 + B, 10000);

  return (exec_px < lower) || (exec_px > upper);
}

bool MatchingEngine::should_trigger_volatility_auction(const Order& incoming) const noexcept {
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
  Qty buy = 0;
  Qty sell = 0;

  for (const auto& o : auction_queue_) {
    if (o.type == OrderType::Market) {
      if (o.side == Side::Buy) buy += o.qty;
      else sell += o.qty;
      continue;
    }
    if (o.side == Side::Buy) {
      if (o.price >= px) buy += o.qty;
    } else {
      if (o.price <= px) sell += o.qty;
    }
  }

  return std::min(buy, sell);
}

std::optional<Price> MatchingEngine::compute_clearing_price() const noexcept {
  if (auction_queue_.empty()) return std::nullopt;

  std::set<Price> candidates;
  for (const auto& o : auction_queue_) {
    if (o.type == OrderType::Limit) candidates.insert(o.price);
  }
  if (candidates.empty()) return std::nullopt;

  const auto ref = reference_price();
  Qty best_vol = -1;
  Price best_px = *candidates.begin();

  for (Price px : candidates) {
    const Qty v = executable_volume_at(px);
    if (v > best_vol) {
      best_vol = v;
      best_px = px;
    } else if (v == best_vol) {
      if (ref) {
        const auto d_best = std::abs(best_px - *ref);
        const auto d_cur  = std::abs(px - *ref);
        if (d_cur < d_best) best_px = px;
      } else {
        if (px < best_px) best_px = px;
      }
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
    auction_queue_.clear();
    return trades;
  }
  const Price clearing_px = *px_opt;

  std::vector<Order> buys;
  std::vector<Order> sells;

  for (const auto& o : auction_queue_) {
    Order copy = o;
    copy.ts = uncross_ts;

    if (copy.type == OrderType::Market) {
      if (copy.side == Side::Buy) buys.push_back(copy);
      else sells.push_back(copy);
    } else {
      if (copy.side == Side::Buy && copy.price >= clearing_px) buys.push_back(copy);
      if (copy.side == Side::Sell && copy.price <= clearing_px) sells.push_back(copy);
    }
  }

  auto pri = [](const Order& a, const Order& b) {
    if (a.ts != b.ts) return a.ts < b.ts;
    return a.id < b.id;
  };
  std::sort(buys.begin(), buys.end(), pri);
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

  auction_queue_.clear();
  return trades;
}

MatchResult MatchingEngine::process(Order incoming) {
  MatchResult out{};

  // Step 13: finalize any due phase endings BEFORE processing this message
  auto flushed = flush(incoming.ts);
  out.trades.insert(out.trades.end(), flushed.begin(), flushed.end());

  const auto decision = rules_.pre_accept(incoming);
  if (!decision.accept) {
    out.status = OrderStatus::Rejected;
    out.reject_reason = decision.reason;
    return out;
  }

  // Closed: ignore everything (accepted but no action)
  if (rules_.phase() == MarketPhase::Closed) return out;

  // Trading-at-Last: only trade at last trade price
  if (rules_.phase() == MarketPhase::TradingAtLast) {
    const auto last = rules_.last_trade_price();
    if (!last) {
      out.status = OrderStatus::Rejected;
      out.reject_reason = RejectReason::NoReferencePrice;
      return out;
    }

    if (incoming.type == OrderType::Limit && incoming.price != *last) {
      out.status = OrderStatus::Rejected;
      out.reject_reason = RejectReason::PriceNotAtLast;
      return out;
    }

    // Market orders become limit at last
    incoming.type = OrderType::Limit;
    incoming.price = *last;

    auto r = process_limit(std::move(incoming));
    out.trades.insert(out.trades.end(), r.trades.begin(), r.trades.end());
    out.resting = r.resting;

    out.filled_qty = 0;
    for (const auto& tr : out.trades) out.filled_qty += tr.qty;

    rules_.on_trades(out.trades);
    return out;
  }

  // Auction phases: if still in auction at this timestamp, queue (flush already handled any expiry)
  if (rules_.phase() == MarketPhase::Auction || rules_.phase() == MarketPhase::ClosingAuction) {
    return queue_in_auction(std::move(incoming));
  }

  // Volatility trigger only in continuous
  if (should_trigger_volatility_auction(incoming)) {
    rules_.set_phase(MarketPhase::Auction);
    auction_end_ts_ = incoming.ts + rules_.config().vol_auction_duration_ns;
    return queue_in_auction(std::move(incoming));
  }

  // FOK pre-check
  if (incoming.tif == TimeInForce::FOK) {
    const Qty avail = available_liquidity(incoming);
    if (avail < incoming.qty) {
      rules_.on_trades(out.trades);
      return out;
    }
  }

  MatchResult r{};
  if (incoming.type == OrderType::Market) r = process_market(std::move(incoming));
  else r = process_limit(std::move(incoming));

  out.trades.insert(out.trades.end(), r.trades.begin(), r.trades.end());
  out.resting = r.resting;

  out.filled_qty = 0;
  for (const auto& tr : out.trades) out.filled_qty += tr.qty;

  rules_.on_trades(out.trades);
  return out;
}

MatchResult MatchingEngine::process_market(Order incoming) {
  MatchResult out{};
  if (incoming.qty <= 0) return out;

  if (incoming.side == Side::Buy) match_buy(out, incoming);
  else match_sell(out, incoming);

  out.filled_qty = 0;
  for (const auto& tr : out.trades) out.filled_qty += tr.qty;

  if (incoming.mkt_style == MarketStyle::MarketToLimit && incoming.qty > 0 && !out.trades.empty()) {
    Order rest = incoming;
    rest.type = OrderType::Limit;
    rest.price = out.trades.back().price;
    rest.tif = TimeInForce::GTC;
    rest.mkt_style = MarketStyle::PureMarket;

    if (book_.add_resting_limit(rest)) out.resting = rest;
  }

  return out;
}

MatchResult MatchingEngine::process_limit(Order incoming) {
  MatchResult out{};
  if (incoming.qty <= 0) return out;

  if (incoming.side == Side::Buy) match_buy(out, incoming);
  else match_sell(out, incoming);

  out.filled_qty = 0;
  for (const auto& tr : out.trades) out.filled_qty += tr.qty;

  if (incoming.tif == TimeInForce::IOC) return out;

  if (incoming.qty > 0) {
    if (book_.add_resting_limit(incoming)) out.resting = incoming;
  }

  return out;
}

void MatchingEngine::match_buy(MatchResult& out, Order& taker) {
  while (taker.qty > 0 && !book_.asks_.empty()) {
    auto best_it = book_.asks_.begin();
    const Price best_ask_px = best_it->first;

    if (taker.type == OrderType::Limit && best_ask_px > taker.price) break;

    auto& lvl = best_it->second;
    if (lvl.q.empty()) {
      book_.asks_.erase(best_it);
      continue;
    }

    if (rules_.config().stp != StpMode::None) {
      const OwnerId maker_owner = lvl.q.front().owner;
      if (maker_owner == taker.owner) {
        if (rules_.config().stp == StpMode::CancelTaker) {
          taker.qty = 0;
          return;
        }
        const OrderId maker_id = lvl.q.front().id;
        (void)book_.cancel(maker_id);
        continue;
      }
    }

    auto& maker = lvl.q.front();
    const Qty q = std::min(taker.qty, maker.qty);

    out.trades.push_back(make_trade(taker.ts, best_ask_px, q, maker.id, taker.id));

    taker.qty -= q;
    maker.qty -= q;
    lvl.total_qty -= q;

    if (maker.qty == 0) lvl.q.pop_front();
    if (lvl.total_qty == 0) book_.asks_.erase(best_it);
  }
}

void MatchingEngine::match_sell(MatchResult& out, Order& taker) {
  while (taker.qty > 0 && !book_.bids_.empty()) {
    auto best_it = book_.bids_.begin();
    const Price best_bid_px = best_it->first;

    if (taker.type == OrderType::Limit && best_bid_px < taker.price) break;

    auto& lvl = best_it->second;
    if (lvl.q.empty()) {
      book_.bids_.erase(best_it);
      continue;
    }

    if (rules_.config().stp != StpMode::None) {
      const OwnerId maker_owner = lvl.q.front().owner;
      if (maker_owner == taker.owner) {
        if (rules_.config().stp == StpMode::CancelTaker) {
          taker.qty = 0;
          return;
        }
        const OrderId maker_id = lvl.q.front().id;
        (void)book_.cancel(maker_id);
        continue;
      }
    }

    auto& maker = lvl.q.front();
    const Qty q = std::min(taker.qty, maker.qty);

    out.trades.push_back(make_trade(taker.ts, best_bid_px, q, maker.id, taker.id));

    taker.qty -= q;
    maker.qty -= q;
    lvl.total_qty -= q;

    if (maker.qty == 0) lvl.q.pop_front();
    if (lvl.total_qty == 0) book_.bids_.erase(best_it);
  }
}

} // namespace msim
