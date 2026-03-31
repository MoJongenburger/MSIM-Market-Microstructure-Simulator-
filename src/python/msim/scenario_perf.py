"""
msim.scenario_perf — Performance-tuned WorldConfig for scenario sweeps
=======================================================================
Provides fast_config() and sweep_config() that pre-configure
CapacityHints and disable unnecessary outputs for the scenario runner.

The bottleneck in 1000-seed sweeps is not C++ computation — it is
memory allocation: hash map rehashing, output vector reallocation,
and Python object construction for unused outputs.

These helpers give you the right tradeoffs:

    fast_config()    — max speed, TCA only, no fills/pnl_series
    analysis_config()— full outputs, calibrated capacity hints
    sweep_config()   — configurable preset for ScenarioRunner

Usage
-----
    runner = ScenarioRunner(
        factory,
        grid    = {"hawkes_mu": [5, 10, 20]},
        metrics = [metrics_sf, metrics_tca(10)],
        n_seeds = 100,
        world_config = msim.scenario_perf.sweep_config(
            n_agents        = 5,
            horizon_seconds = 1.0,
            dt_ns           = 1_000_000,
        ),
    )
"""
from __future__ import annotations


def _estimate_capacity(n_agents: int, horizon_s: float,
                        dt_ns: int, orders_per_agent_per_step: float = 2.0,
                        fill_rate: float = 0.30) -> tuple[int, int]:
    """Return (expected_orders, expected_trades) estimates."""
    n_steps  = int(horizon_s * 1e9 / dt_ns) + 1
    n_orders = int(n_agents * orders_per_agent_per_step * n_steps)
    n_trades = int(n_orders * fill_rate)
    return n_orders, n_trades


def fast_config(n_agents: int = 6,
                horizon_seconds: float = 1.0,
                dt_ns: int = 1_000_000) -> "msim.WorldConfig":
    """Maximum-speed config for scenario sweeps.

    Disables:  stylized_facts, pnl_series, fills, fv_signals,
               queue_positions.
    Keeps:     TCA summary (AgentTCA is always populated, zero extra cost).

    Throughput gains vs default config on a 1000-seed sweep:
    - No pnl_series:    ~2–3× fewer StepSnapshot allocations
    - No fills:         ~2× fewer FillRecord allocations
    - No SF:            skip end-of-run autocorr/kurtosis computation
    - CapacityHints:    eliminate all hash map rehashing

    Use when you only need final PnL, fill rate, and slippage summary.

    Parameters
    ----------
    n_agents        : int   — number of registered agents
    horizon_seconds : float — simulation duration in seconds
    dt_ns           : int   — step width in nanoseconds (default 1ms)

    Returns
    -------
    msim.WorldConfig
    """
    import msim
    cfg = msim.WorldConfig()
    cfg.compute_stylized_facts = False
    cfg.record_fv_signals      = False
    cfg.record_fills           = False
    cfg.record_pnl_series      = False
    cfg.track_queue_positions  = False
    cfg.dt_ns                  = dt_ns

    n_orders, n_trades = _estimate_capacity(
        n_agents, horizon_seconds, dt_ns,
        orders_per_agent_per_step=2.5, fill_rate=0.3)
    cfg.capacity.expected_orders = n_orders
    cfg.capacity.expected_trades = n_trades
    return cfg


def analysis_config(n_agents: int = 6,
                    horizon_seconds: float = 1.0,
                    dt_ns: int = 1_000_000,
                    orders_per_agent_per_step: float = 2.0,
                    fill_rate: float = 0.30) -> "msim.WorldConfig":
    """Full-output config with calibrated capacity hints.

    Enables everything (stylized facts, fills, pnl_series, TCA).
    Pre-reserves all hash maps and output vectors.

    Use when you need the complete output for a single run or a
    small sweep (< 50 seeds) where output richness matters.

    Parameters
    ----------
    n_agents                   : int
    horizon_seconds            : float
    dt_ns                      : int
    orders_per_agent_per_step  : float — tune based on your agent mix
    fill_rate                  : float — fraction of orders that fill

    Returns
    -------
    msim.WorldConfig
    """
    import msim
    cfg = msim.WorldConfig()
    cfg.compute_stylized_facts = True
    cfg.record_fv_signals      = False
    cfg.record_fills           = True
    cfg.record_pnl_series      = True
    cfg.track_queue_positions  = True
    cfg.dt_ns                  = dt_ns

    n_orders, n_trades = _estimate_capacity(
        n_agents, horizon_seconds, dt_ns,
        orders_per_agent_per_step, fill_rate)
    cfg.capacity.expected_orders = n_orders
    cfg.capacity.expected_trades = n_trades
    return cfg


def sweep_config(n_agents: int = 6,
                 horizon_seconds: float = 1.0,
                 dt_ns: int = 1_000_000,
                 compute_stylized_facts: bool = True,
                 record_fills: bool = False,
                 record_pnl_series: bool = False,
                 track_queue_positions: bool = False,
                 orders_per_agent_per_step: float = 2.0,
                 fill_rate: float = 0.30) -> "msim.WorldConfig":
    """Configurable preset for ScenarioRunner.

    Defaults are tuned for large sweeps: SF on (needed for metrics_sf),
    fills/pnl_series off (not needed for summary metrics), queue positions
    off (no limit-order agents in most sweep scenarios).

    You can override any flag.

    Parameters
    ----------
    n_agents                   : int
    horizon_seconds            : float
    dt_ns                      : int
    compute_stylized_facts     : bool   — default True
    record_fills               : bool   — default False
    record_pnl_series          : bool   — default False
    track_queue_positions      : bool   — default False
    orders_per_agent_per_step  : float  — for capacity estimation
    fill_rate                  : float  — for capacity estimation

    Returns
    -------
    msim.WorldConfig

    Example
    -------
    >>> from msim.scenario_perf import sweep_config
    >>> runner = ScenarioRunner(
    ...     factory,
    ...     grid         = {"hawkes_mu": [5, 10, 20]},
    ...     metrics      = [metrics_sf],
    ...     n_seeds      = 200,
    ...     world_config = sweep_config(n_agents=5, horizon_seconds=1.0),
    ... )
    """
    import msim
    cfg = msim.WorldConfig()
    cfg.compute_stylized_facts = compute_stylized_facts
    cfg.record_fv_signals      = False
    cfg.record_fills           = record_fills
    cfg.record_pnl_series      = record_pnl_series
    cfg.track_queue_positions  = track_queue_positions
    cfg.dt_ns                  = dt_ns

    n_orders, n_trades = _estimate_capacity(
        n_agents, horizon_seconds, dt_ns,
        orders_per_agent_per_step, fill_rate)
    cfg.capacity.expected_orders = n_orders
    cfg.capacity.expected_trades = n_trades
    return cfg


def benchmark(factory, n_seeds: int = 100,
              horizon: float = 1.0,
              n_agents: int = 6,
              dt_ns: int = 1_000_000,
              n_workers: int = 4) -> dict:
    """Quick throughput benchmark comparing fast_config vs default config.

    Returns a dict with keys:
        fast_runs_per_s   — runs/second with fast_config
        default_runs_per_s— runs/second with default WorldConfig
        speedup           — fast / default

    Example
    -------
    >>> from msim.scenario_perf import benchmark
    >>> stats = benchmark(my_factory, n_seeds=200, n_agents=6)
    >>> print(f"Speedup: {stats['speedup']:.1f}x")
    """
    import time
    from msim.scenario import ScenarioRunner, metrics_all_tca

    def run(cfg_fn):
        cfg = cfg_fn()
        runner = ScenarioRunner(
            world_factory = factory,
            param_grid    = {"_dummy": [0]},
            metrics       = [metrics_all_tca],
            n_seeds       = n_seeds,
            world_config  = cfg,
            horizon       = horizon,
            n_workers     = n_workers,
            verbose       = False,
        )
        t0 = time.perf_counter()
        runner.run()
        elapsed = time.perf_counter() - t0
        return n_seeds / elapsed

    fast_rps    = run(lambda: fast_config(n_agents, horizon, dt_ns))
    default_rps = run(lambda: __import__("msim").WorldConfig())

    return {
        "fast_runs_per_s":    round(fast_rps, 1),
        "default_runs_per_s": round(default_rps, 1),
        "speedup":            round(fast_rps / max(default_rps, 0.001), 2),
    }
