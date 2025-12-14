#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "msim/order_flow.hpp"
#include "msim/simulator.hpp"

static void write_trades_csv(const std::string& path, const std::vector<msim::Trade>& trades) {
  std::ofstream f(path);
  f << "trade_id,ts,price,qty,maker_id,taker_id\n";
  for (const auto& t : trades) {
    f << t.id << "," << t.ts << "," << t.price << "," << t.qty << ","
      << t.maker_order_id << "," << t.taker_order_id << "\n";
  }
}

static void write_top_csv(const std::string& path, const std::vector<msim::BookTop>& tops) {
  std::ofstream f(path);
  f << "ts,best_bid,best_ask,mid\n";
  for (const auto& x : tops) {
    f << x.ts << ",";
    if (x.best_bid) f << *x.best_bid;
    f << ",";
    if (x.best_ask) f << *x.best_ask;
    f << ",";
    if (x.mid) f << *x.mid;
    f << "\n";
  }
}

int main(int argc, char** argv) {
  uint64_t seed = 1;
  double horizon = 2.0; // seconds

  if (argc >= 2) seed = static_cast<uint64_t>(std::stoull(argv[1]));
  if (argc >= 3) horizon = std::stod(argv[2]);

  msim::FlowParams p{};
  msim::OrderFlowGenerator gen(seed, p);

  const msim::Ts t0 = 0;
  auto events = gen.generate(t0, horizon);

  msim::Simulator sim;
  auto res = sim.run(events);

  write_trades_csv("trades.csv", res.trades);
  write_top_csv("top.csv", res.tops);

  std::cout << "events=" << events.size()
            << " trades=" << res.trades.size()
            << " cancel_failures=" << res.cancel_failures
            << " modify_failures=" << res.modify_failures
            << "\n";
  return 0;
}
