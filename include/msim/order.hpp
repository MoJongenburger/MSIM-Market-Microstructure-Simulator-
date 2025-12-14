#pragma once
#include <cstdint>

#include "msim/types.hpp"

namespace msim {

enum class Side : uint8_t { Buy = 0, Sell = 1 };
enum class OrderType : uint8_t { Limit = 0, Market = 1 };

// Step 8: Time-in-force
enum class TimeInForce : uint8_t { GTC = 0, IOC = 1, FOK = 2 };

// Step 8: Market order handling
enum class MarketStyle : uint8_t { PureMarket = 0, MarketToLimit = 1 };

struct Order {
  OrderId   id{};
  Ts        ts{};
  Side      side{};
  OrderType type{OrderType::Limit};
  Price     price{};
  Qty       qty{};
  OwnerId   owner{};

  // Step 8 additions (defaults preserve old aggregate init usage)
  TimeInForce tif{TimeInForce::GTC};
  MarketStyle mkt_style{MarketStyle::PureMarket};
};

} // namespace msim

