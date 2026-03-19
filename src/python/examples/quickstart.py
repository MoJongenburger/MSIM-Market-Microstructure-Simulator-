#!/usr/bin/env python3
"""
MSIM Python Quickstart
======================
Three progressively advanced usage patterns:

  1. One-liner with built-in agents
  2. Custom Python strategy
  3. Latency-aware multi-agent run with analysis

Run from the repo root after building::

    cmake -S . -B build -DMSIM_BUILD_PYTHON=ON
    cmake --build build --target _msim_core
    PYTHONPATH=build:python python python/examples/quickstart.py
"""

import msim
from msim.analysis import dashboard, pnl_series, stylized_facts_df

print("=" * 60)
print("  MSIM Python Quickstart")
print(f"  Version: {msim.__version__}")
print("=" * 60)


# ─── Example 1: One-liner with built-in agents ────────────────────────────────
print("\n[1] One-liner quick_run")

result = msim.quick_run(
    msim.agents.HawkesNoiseTrader(owner_id=1),
    msim.agents.HawkesNoiseTrader(owner_id=2),
    msim.agents.MarketMakerAS(owner_id=10),
    seed=42,
    horizon=1.0,
)

print(f"    Trades:  {len(result.trades)}")
print(f"    Tops:    {len(result.tops)}")
if result.sf:
    print(f"    Kurtosis: {result.sf.returns.excess_kurtosis:.3f}")
    print(f"    All SF pass: {result.sf.passes()}")


# ─── Example 2: Custom Python strategy ────────────────────────────────────────
print("\n[2] Custom Python strategy (mean-reversion)")


class MeanReversionAgent(msim.Agent):
    """
    Simple mean-reversion agent.

    Tracks a rolling average mid-price. When the current mid deviates
    more than `threshold` ticks, trades towards the mean.
    """

    def __init__(self, owner_id: int, window: int = 20, threshold: float = 3.0,
                 lot_size: int = 2):
        super().__init__()
        self._owner     = owner_id
        self._window    = window
        self._threshold = threshold
        self._lot_size  = lot_size
        self._prices    = []
        self._counter   = 0

    def owner(self) -> int:
        return self._owner

    def seed(self, s: int) -> None:
        import random
        random.seed(s)
        self._prices  = []
        self._counter = 0

    def step(self, ts: int, view: msim.MarketView,
             state: msim.AgentState) -> list:
        if not view.has_quote() or view.mid is None:
            return []

        mid = view.mid
        self._prices.append(mid)
        if len(self._prices) > self._window:
            self._prices.pop(0)

        if len(self._prices) < self._window:
            return []  # warming up

        avg = sum(self._prices) / len(self._prices)
        deviation = mid - avg

        side = None
        if deviation > self._threshold:
            side = msim.Side.Sell   # price above mean: sell
        elif deviation < -self._threshold:
            side = msim.Side.Buy    # price below mean: buy

        if side is None:
            return []

        o = msim.Order()
        o.id        = (self._owner << 24) | (self._counter & 0xFFFFFF)
        o.owner     = self._owner
        o.side      = side
        o.type      = msim.OrderType.Market
        o.qty       = self._lot_size
        o.tif       = msim.TimeInForce.IOC
        o.mkt_style = msim.MarketStyle.PureMarket
        self._counter += 1
        return [msim.Action.submit(o)]


world = msim.make_world(mid=10_000, levels=20, qty=10)
world.add_agent(msim.agents.HawkesNoiseTrader(owner_id=1))
world.add_agent(msim.agents.HawkesNoiseTrader(owner_id=2))
world.add_agent(msim.agents.MarketMakerAS(owner_id=10))
world.add_agent(MeanReversionAgent(owner_id=99, window=20, threshold=3.0))

cfg = msim.WorldConfig()
cfg.compute_stylized_facts = True
cfg.record_fv_signals      = False

result2 = world.run(seed=0, horizon=2.0, config=cfg)

print(f"    Trades:   {len(result2.trades)}")
print(f"    Accounts: {len(result2.accounts)}")

# PnL of our custom agent
try:
    df_pnl = pnl_series(result2, owner_id=99)
    if not df_pnl.empty:
        final_pnl = df_pnl["pnl"].iloc[-1]
        max_dd    = (df_pnl["pnl"] - df_pnl["pnl"].cummax()).min()
        print(f"    MeanReversion PnL: {final_pnl:.1f} ticks")
        print(f"    Max drawdown:      {max_dd:.1f} ticks")
    else:
        print("    MeanReversion: no fills")
except ImportError:
    print("    (install pandas for PnL analysis)")

# Stylized facts
if result2.sf:
    try:
        df_sf = stylized_facts_df(result2.sf)
        print(f"    Fat tails OK:      {df_sf['fat_tails_ok'].iloc[0]}")
        print(f"    Vol clustering OK: {df_sf['vol_clustering_ok'].iloc[0]}")
        print(f"    Flow autocorr OK:  {df_sf['flow_autocorr_ok'].iloc[0]}")
    except ImportError:
        print("    (install pandas for SF summary DataFrame)")
    result2.summary()


# ─── Example 3: Latency-aware run ─────────────────────────────────────────────
print("\n[3] Latency-aware multi-agent run")

world3 = msim.make_world(mid=10_000, levels=20, qty=10)

# HFT market maker: 500ns fixed latency
world3.add_agent(msim.agents.MarketMakerAS(owner_id=10))

# Informed trader: ~5µs lognormal latency
world3.add_agent(msim.agents.FundamentalValueAgent(owner_id=20))

# Noise: ~50µs lognormal latency  
world3.add_agent(msim.agents.HawkesNoiseTrader(owner_id=30))
world3.add_agent(msim.agents.HawkesNoiseTrader(owner_id=31))

cfg3 = msim.WorldConfig()
cfg3.latency_enabled = True
cfg3.latency_configs = [
    msim.LatencyDistConfig.fixed(500),           # MM: HFT
    msim.LatencyDistConfig.lognormal(5_000, 1_000),  # FV: informed
    msim.LatencyDistConfig.lognormal(50_000, 10_000), # Noise1
    msim.LatencyDistConfig.lognormal(50_000, 10_000), # Noise2
]
cfg3.compute_stylized_facts = True

result3 = world3.run(seed=7, horizon=2.0, config=cfg3)
print(f"    Trades: {len(result3.trades)}")
if result3.sf:
    print(f"    Kyle's lambda: {result3.sf.impact.kyle_lambda:.4f}")
    print(f"    TW spread:     {result3.sf.spreads.time_weighted_spread:.2f} ticks")


# ─── Determinism check ────────────────────────────────────────────────────────
print("\n[4] Determinism verification")

world_a = msim.make_world(mid=10_000)
world_a.add_agent(msim.agents.HawkesNoiseTrader(owner_id=1))
world_a.add_agent(msim.agents.MarketMakerAS(owner_id=10))

world_b = msim.make_world(mid=10_000)
world_b.add_agent(msim.agents.HawkesNoiseTrader(owner_id=1))
world_b.add_agent(msim.agents.MarketMakerAS(owner_id=10))

res_a = world_a.run(seed=123, horizon=1.0)
res_b = world_b.run(seed=123, horizon=1.0)

prices_a = [t.price for t in res_a.trades]
prices_b = [t.price for t in res_b.trades]
identical = prices_a == prices_b

print(f"    Run A trades: {len(res_a.trades)}")
print(f"    Run B trades: {len(res_b.trades)}")
print(f"    Bit-identical: {identical}")
assert identical, "DETERMINISM FAILURE — same seed must produce same output"

print("\n" + "=" * 60)
print("  All examples completed successfully.")
print("=" * 60)
