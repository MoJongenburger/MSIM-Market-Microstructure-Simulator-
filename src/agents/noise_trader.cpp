#include "msim/agents/noise_trader.hpp"
#include <algorithm>

namespace msim::agents {

Price NoiseTrader::snap_to_tick(Price p) const noexcept {
  const Price tick = std::max<Price>(1, cfg_.tick_size);
  // snap down to grid
  return (p / tick) * tick;
}

Qty NoiseTrader::snap_to_lot(Qty q) const noexcept {
  const Qty lot  = std::max<Qty>(1, cfg_.lot_size);
  const Qty minq = std::max<Qty>(1, cfg_.min_qty);

  q = std::max(q, minq);

  // snap down to lot grid
  q = (q / lot) * lot;
  if (q <= 0) q = lot;
  return q;
}

std::vector<Action> NoiseTrader::generate_actions(const MarketView& view, std::mt19937_64& rng) {
  std::vector<Action> out;

  std::uniform_real_distribution<double> U01(0.0, 1.0);
  if (U01(rng) > cfg_.intensity_per_step) return out;

  // Reference price
  Price ref = view.mid.value_or(cfg_.default_mid);
  ref = snap_to_tick(ref);
  if (ref <= 0) ref = std::max<Price>(1, cfg_.tick_size);

  // Side
  std::bernoulli_distribution coin_side(0.5);
  const msim::Side side = coin_side(rng) ? msim::Side::Buy : msim::Side::Sell;

  // Qty
  std::uniform_int_distribution<int32_t> qty_dist(
      static_cast<int32_t>(std::max<Qty>(1, cfg_.min_qty)),
      static_cast<int32_t>(std::max<Qty>(cfg_.min_qty, cfg_.max_qty)));
  Qty qty = static_cast<Qty>(qty_dist(rng));
  qty = snap_to_lot(qty);

  const bool is_market = (U01(rng) < cfg_.prob_market);

  msim::Order o{};
  o.id = next_order_id_++;
  o.ts = view.ts;
  o.side = side;
  o.owner = owner_;
  o.qty = qty;

  if (is_market) {
    o.type = msim::OrderType::Market;
    o.price = 0;
    o.tif = msim::TimeInForce::IOC;              // market behaves like immediate
    o.mkt_style = msim::MarketStyle::PureMarket; // simple baseline
  } else {
    o.type = msim::OrderType::Limit;

    const int32_t max_off = std::max<int32_t>(1, cfg_.max_offset_ticks);
    std::uniform_int_distribution<int32_t> off_dist(1, max_off);
    const int32_t off = off_dist(rng);

    Price px = ref;
    if (side == msim::Side::Buy)  px = ref - off;
    else                         px = ref + off;

    px = snap_to_tick(px);
    if (px <= 0) px = snap_to_tick(ref);

    o.price = px;
    o.tif = msim::TimeInForce::GTC;
    o.mkt_style = msim::MarketStyle::PureMarket;
  }

  Action a{};
  a.type = ActionType::Place;
  a.ts = view.ts;
  a.owner = owner_;
  a.place = o;

  out.push_back(a);
  return out;
}

} // namespace msim::agents
