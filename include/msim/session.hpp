#pragma once
#include "msim/matching_engine.hpp"

namespace msim {

struct SessionSchedule {
  Ts tal_start_ts{0};
  Ts tal_end_ts{0};

  Ts closing_auction_start_ts{0};
  Ts closing_auction_end_ts{0};
};

class SessionController {
public:
  explicit SessionController(SessionSchedule s) : s_(s) {}

  void on_time(MatchingEngine& eng, Ts ts) {
    // Start TAL once
    if (!tal_started_ && ts >= s_.tal_start_ts && ts < s_.tal_end_ts) {
      eng.start_trading_at_last(s_.tal_end_ts);
      tal_started_ = true;
    }

    // Start closing auction once
    if (!close_started_ && ts >= s_.closing_auction_start_ts && ts < s_.closing_auction_end_ts) {
      eng.start_closing_auction(s_.closing_auction_end_ts);
      close_started_ = true;
    }

    // Always allow engine to finalize any due auction/close at this timestamp
    (void)eng.flush(ts);
  }

private:
  SessionSchedule s_{};
  bool tal_started_{false};
  bool close_started_{false};
};

} // namespace msim
