#pragma once
#include <cstdint>
#include <string_view>

namespace msim {

using Price    = int32_t;   // ticks
using Qty      = int32_t;   // shares/contracts
using OrderId  = uint64_t;
using TradeId  = uint64_t;
using Ts       = int64_t;   // timestamp in nanoseconds (or any consistent unit)

enum class Side : uint8_t { Buy = 0, Sell = 1 };
enum class OrderType : uint8_t { Limit = 0, Market = 1 };

inline constexpr Side opposite(Side s) noexcept {
  return (s == Side::Buy) ? Side::Sell : Side::Buy;
}

inline std::string_view to_string(Side s) noexcept {
  return (s == Side::Buy) ? "BUY" : "SELL";
}

inline std::string_view to_string(OrderType t) noexcept {
  return (t == OrderType::Limit) ? "LIMIT" : "MARKET";
}

} // namespace msim

