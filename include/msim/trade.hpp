#pragma once
#include "msim/types.hpp"

namespace msim {

struct Trade {
  TradeId id{};
  Ts      ts{};
  Price   price{};
  Qty     qty{};
  Side    aggressor_side{Side::Buy};  // ← move to here (fills padding after qty)
  OrderId maker_order_id{};
  OrderId taker_order_id{};
};

inline constexpr bool is_valid_trade(const Trade& t) noexcept {
  return (t.qty > 0) && (t.price >= 0);
}

} // namespace msim

