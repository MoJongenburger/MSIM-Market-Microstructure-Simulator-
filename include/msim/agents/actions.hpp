#pragma once
#include <cstdint>
#include <optional>

#include "msim/order.hpp"
#include "msim/types.hpp"

namespace msim::agents {

enum class ActionType : uint8_t { Place = 0, Cancel = 1, ModifyQty = 2 };

struct Action {
  ActionType type{ActionType::Place};
  Ts ts{0};                 // when the action arrives at the exchange (latency later)
  OwnerId owner{0};         // agent identity (maps to Order.owner)

  // One of:
  Order place{};            // for Place
  OrderId cancel_id{0};     // for Cancel
  Qty new_qty{0};           // for ModifyQty
};

} // namespace msim::agents
