#pragma once
#include <optional>
#include "msim/types.hpp"

namespace msim {

inline constexpr bool is_book_crossed(std::optional<Price> best_bid,
                                      std::optional<Price> best_ask) noexcept {
  if (!best_bid || !best_ask) return false;
  return *best_bid >= *best_ask;
}

inline constexpr std::optional<Price> midprice(std::optional<Price> best_bid,
                                               std::optional<Price> best_ask) noexcept {
  if (!best_bid || !best_ask) return std::nullopt;
  return (*best_bid + *best_ask) / 2;
}

} // namespace msim

