"""
msim — Market Microstructure Simulator
=======================================
Sub-100ns C++20 matching engine with a full Python strategy interface.

Quick start
-----------
>>> import msim
>>>
>>> world = msim.World()
>>> world.prefill_book(mid=10000, levels=20, qty=10)
>>>
>>> # Use a built-in agent
>>> world.add_agent(msim.agents.HawkesNoiseTrader(owner_id=1))
>>> world.add_agent(msim.agents.MarketMakerAS(owner_id=2))
>>>
>>> result = world.run(seed=42, horizon=2.0)
>>> df = result.trades_df()
>>> result.summary()

Writing a custom strategy
-------------------------
>>> class MyStrategy(msim.Agent):
...     def __init__(self, owner_id: int):
...         super().__init__()
...         self._owner = owner_id
...         self._counter = 0
...
...     def owner(self) -> int:
...         return self._owner
...
...     def seed(self, s: int) -> None:
...         import random
...         self._rng = random.Random(s)
...         self._counter = 0
...
...     def step(self, ts: int, view: msim.MarketView,
...              state: msim.AgentState) -> list:
...         if not view.has_quote():
...             return []
...         o = msim.Order()
...         o.id        = (self._owner << 24) | (self._counter & 0xFFFFFF)
...         o.owner     = self._owner
...         o.side      = msim.Side.Buy
...         o.type      = msim.OrderType.Market
...         o.qty       = 1
...         o.tif       = msim.TimeInForce.IOC
...         o.mkt_style = msim.MarketStyle.PureMarket
...         self._counter += 1
...         return [msim.Action.submit(o)]

Scenario runner
---------------
>>> from msim.scenario import ScenarioRunner, metrics_sf, metrics_tca
>>>
>>> def factory(params):
...     w = msim.make_world(mid=10_000)
...     cfg = msim.HawkesNoiseConfig()
...     cfg.hawkes.mu = params["hawkes_mu"]
...     w.add_agent(msim.agents.HawkesNoiseTrader(owner_id=1, config=cfg))
...     w.add_agent(msim.agents.MarketMakerAS(owner_id=10))
...     return w
>>>
>>> runner = ScenarioRunner(
...     world_factory = factory,
...     param_grid    = {"hawkes_mu": [5.0, 10.0, 20.0]},
...     metrics       = [metrics_sf, metrics_tca(owner_id=10)],
...     n_seeds       = 20,
... )
>>> runner.run()
>>> df = runner.results_df()
"""

from __future__ import annotations

# ── C++ extension ─────────────────────────────────────────────────────────────
from ._msim_core import (
    # Enums
    Side,
    OrderType,
    TimeInForce,
    MarketStyle,
    ActionType,
    LatencyDistType,

    # Data types
    Order,
    Trade,
    BookTop,
    LevelSummary,
    MarketView,
    AgentState,
    Action,
    FVLogEntry,

    # Stylized facts
    ReturnStats,
    AutocorrResult,
    PriceImpactResult,
    SpreadStats,
    AmihudStats,
    StyleFacts,
    AccountSnapshot,

    # Config
    WorldConfig,
    LatencyDistConfig,
    HawkesConfig,

    # Built-in agent configs
    FundamentalValueConfig,
    MomentumConfig,
    HawkesNoiseConfig,
    MarketMakerASConfig,

    # Execution agent configs  (added in execution agents step)
    VWAPConfig,
    TWAPConfig,
    ISConfig,
    VWAPSchedule,

    # TCA types  (added in TCA step)
    FillRecord,
    StepSnapshot,
    AgentTCA,

    # Core classes
    Agent,
    WorldResult,
    World,

    # Built-in agents submodule
    agents,

    # Package metadata
    __version__,
    __author__,
)

# ── Pure-Python submodules ────────────────────────────────────────────────────
from . import analysis   # noqa: F401
from . import execution  # noqa: F401
from . import scenario   # noqa: F401

# ── Scenario runner convenience re-exports ────────────────────────────────────
from .scenario import (
    ScenarioRunner,
    metrics_sf,
    metrics_tca,
    metrics_all_tca,
    metrics_execution_is,
    single_param_sweep,
    sensitivity_analysis,
)

# ── Public API ────────────────────────────────────────────────────────────────
__all__ = [
    # Enums
    "Side", "OrderType", "TimeInForce", "MarketStyle",
    "ActionType", "LatencyDistType", "VWAPSchedule",
    # Data
    "Order", "Trade", "BookTop", "LevelSummary",
    "MarketView", "AgentState", "Action", "FVLogEntry",
    "AccountSnapshot", "FillRecord", "StepSnapshot", "AgentTCA",
    # Stylized facts
    "ReturnStats", "AutocorrResult", "PriceImpactResult",
    "SpreadStats", "AmihudStats", "StyleFacts",
    # Config
    "WorldConfig", "LatencyDistConfig", "HawkesConfig",
    "FundamentalValueConfig", "MomentumConfig",
    "HawkesNoiseConfig", "MarketMakerASConfig",
    "VWAPConfig", "TWAPConfig", "ISConfig",
    # Core
    "Agent", "WorldResult", "World",
    # Submodules
    "agents", "analysis", "execution", "scenario",
    # Scenario helpers
    "ScenarioRunner", "metrics_sf", "metrics_tca",
    "metrics_all_tca", "metrics_execution_is",
    "single_param_sweep", "sensitivity_analysis",
    # World helpers
    "quick_run", "make_world",
]


# ── Convenience helpers ───────────────────────────────────────────────────────

def make_world(
    mid: int = 10_000,
    levels: int = 20,
    qty: int = 10,
) -> World:
    """Create a World pre-filled with a symmetric limit order book.

    Parameters
    ----------
    mid    : int — mid-price in ticks
    levels : int — number of bid and ask levels to pre-fill
    qty    : int — quantity per level

    Returns
    -------
    World
        Ready for ``add_agent()`` and ``run()``.

    Example
    -------
    >>> world = msim.make_world(mid=10_000)
    >>> world.add_agent(msim.agents.MarketMakerAS(owner_id=1))
    >>> result = world.run(seed=0, horizon=1.0)
    """
    w = World()
    w.prefill_book(mid=mid, levels=levels, qty=qty)
    return w


def quick_run(
    *agent_list,
    mid: int = 10_000,
    levels: int = 20,
    qty: int = 10,
    seed: int = 0,
    horizon: float = 1.0,
    config: WorldConfig | None = None,
) -> WorldResult:
    """One-liner simulation runner.

    Creates a world, pre-fills the book, adds all supplied agents, and runs.

    Parameters
    ----------
    *agent_list : Agent
        Any number of agents to add (in order).
    mid         : int   — mid-price in ticks
    levels      : int   — book pre-fill depth
    qty         : int   — qty per level
    seed        : int   — reproducibility seed
    horizon     : float — simulation duration in seconds
    config      : WorldConfig | None — optional config (default WorldConfig())

    Returns
    -------
    WorldResult

    Example
    -------
    >>> result = msim.quick_run(
    ...     msim.agents.HawkesNoiseTrader(owner_id=1),
    ...     msim.agents.MarketMakerAS(owner_id=2),
    ...     seed=42, horizon=2.0
    ... )
    >>> result.trades_df().head()
    """
    w = make_world(mid=mid, levels=levels, qty=qty)
    for agent in agent_list:
        w.add_agent(agent)
    return w.run(seed=seed, horizon=horizon, config=config or WorldConfig())
