#include <gtest/gtest.h>
#include "msim/types.hpp"
#include "msim/order.hpp"
#include "msim/trade.hpp"
#include "msim/events.hpp"
#include "msim/invariants.hpp"

TEST(Types, OppositeSide) {
  EXPECT_EQ(msim::opposite(msim::Side::Buy), msim::Side::Sell);
  EXPECT_EQ(msim::opposite(msim::Side::Sell), msim::Side::Buy);
}

TEST(Order, Validity) {
  msim::Order o{};
  o.id = 1;
  o.ts = 100;
  o.side = msim::Side::Buy;
  o.type = msim::OrderType::Limit;
  o.price = 10100;
  o.qty = 10;

  EXPECT_TRUE(msim::is_valid_order(o));

  o.qty = 0;
  EXPECT_FALSE(msim::is_valid_order(o));
}

TEST(Trade, Validity) {
  msim::Trade t{};
  t.id = 1;
  t.ts = 200;
  t.price = 10100;
  t.qty = 5;
  EXPECT_TRUE(msim::is_valid_trade(t));

  t.qty = -1;
  EXPECT_FALSE(msim::is_valid_trade(t));
}

TEST(Invariants, CrossedBook) {
  EXPECT_FALSE(msim::is_book_crossed(std::nullopt, std::nullopt));
  EXPECT_FALSE(msim::is_book_crossed(100, std::nullopt));
  EXPECT_FALSE(msim::is_book_crossed(std::nullopt, 101));
  EXPECT_FALSE(msim::is_book_crossed(100, 101));
  EXPECT_TRUE(msim::is_book_crossed(101, 101));
  EXPECT_TRUE(msim::is_book_crossed(102, 101));
}

