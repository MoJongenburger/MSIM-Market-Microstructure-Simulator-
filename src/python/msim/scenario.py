"""
msim.scenario — Systematic strategy stress-testing
===================================================

The ScenarioRunner lets you answer questions like:

  "How does my market maker's PnL change as noise trader intensity
   doubles? Does it degrade gracefully or collapse?"

  "What is the 5th-percentile IS of my VWAP strategy across 200
   random seeds? Is it worse than TWAP under high volatility?"

  "At what Hawkes alpha does the LOB start showing fat-tailed returns?"

Usage pattern
-------------
1. Define a **world factory**: a callable(params) → World that builds
   a fresh, pre-filled World for one scenario point.

2. Define a **parameter grid** as a dict of {param_name: [values]}.
   The runner generates the Cartesian product of all axes.

3. Call ScenarioRunner.run() to execute every combination across
   multiple seeds in parallel (thread pool, GIL released during C++).

4. Inspect results via .results_df() — a tidy DataFrame with one row
   per (scenario, seed) pair and columns for all output metrics.

Design principles
-----------------
- Zero global state: each scenario creates a fresh World and engine.
- Fully deterministic: seed sequence is splitmix64-derived from the
  base seed, giving reproducible results regardless of thread ordering.
- Memory-efficient: raw WorldResult objects are discarded after metric
  extraction; only the summary row is kept.
- Parallel by default: uses concurrent.futures.ThreadPoolExecutor.
  The C++ matching engine releases the Python GIL during World::run(),
  so Python agents are the only bottleneck for parallelism.
- No mandatory dependencies beyond the msim module itself.
  pandas/numpy are only used when explicitly requested.
"""
from __future__ import annotations

import concurrent.futures
import itertools
import math
import time
from dataclasses import dataclass, field
from typing import Any, Callable, Iterator, Sequence

# ─── Type aliases ─────────────────────────────────────────────────────────────
ParamGrid   = dict[str, list[Any]]
ParamPoint  = dict[str, Any]
WorldFactory = Callable[[ParamPoint], Any]   # returns msim.World
MetricFn     = Callable[[Any, ParamPoint], dict[str, float]]
# Any = msim.World / msim.WorldResult (avoid hard import at module level)


# ─── Built-in metric extractors ───────────────────────────────────────────────

def metrics_sf(result, _params: ParamPoint) -> dict[str, float]:
    """Extract stylized facts metrics from a WorldResult.

    Returns an empty dict if compute_stylized_facts was False.
    """
    if result.sf is None:
        return {}
    sf = result.sf
    ac1 = lambda v: v[0] if v else math.nan
    return {
        "excess_kurtosis":    sf.returns.excess_kurtosis,
        "return_std":         sf.returns.std_dev,
        "vol_clustering_ac1": ac1(sf.autocorr.abs_return_ac),
        "sign_flow_ac1":      ac1(sf.autocorr.sign_flow_ac),
        "kyle_lambda":        sf.impact.kyle_lambda,
        "tw_spread":          sf.spreads.time_weighted_spread,
        "effective_spread":   sf.spreads.effective_spread_mean,
        "amihud_mean":        sf.amihud.mean_illiq,
        "fat_tails_ok":       float(sf.fat_tails_ok),
        "vol_clustering_ok":  float(sf.vol_clustering_ok),
        "flow_autocorr_ok":   float(sf.flow_autocorr_ok),
        "n_trades":           float(len(result.trades)),
    }


def metrics_tca(owner_id: int) -> MetricFn:
    """Return a metric function that extracts AgentTCA for one owner.

    Usage::

        runner = ScenarioRunner(factory, grid,
                                metrics=[metrics_tca(owner_id=10)])
    """
    def _extract(result, _params: ParamPoint) -> dict[str, float]:
        for tca in result.tca:
            if tca.owner == owner_id:
                return {
                    f"agent{owner_id}_pnl":         tca.final_mtm_pnl,
                    f"agent{owner_id}_position":    float(tca.final_position),
                    f"agent{owner_id}_fill_rate":   tca.limit_fill_rate,
                    f"agent{owner_id}_avg_slip":    tca.avg_slippage_ticks,
                    f"agent{owner_id}_qty_traded":  float(tca.total_qty_traded),
                    f"agent{owner_id}_n_orders":    float(tca.n_orders_submitted),
                }
        return {}
    return _extract


def metrics_execution_is(owner_id: int,
                          side: str = "Buy") -> MetricFn:
    """Return a metric function computing IS for an execution agent.

    Usage::

        runner = ScenarioRunner(factory, grid,
                                metrics=[metrics_execution_is(owner_id=50)])
    """
    def _extract(result, _params: ParamPoint) -> dict[str, float]:
        fills = result.fills_df()
        agent = fills[(fills["owner"] == owner_id) & (~fills["is_maker"])]
        if agent.empty:
            return {f"exec{owner_id}_is": math.nan,
                    f"exec{owner_id}_qty": 0.0,
                    f"exec{owner_id}_complete": 0.0}
        total_qty = float(agent["fill_qty"].sum())
        avg_fill  = float(
            (agent["fill_price"] * agent["fill_qty"]).sum()) / total_qty
        ap = float(agent["arrival_mid"].iloc[0])
        is_ticks = (avg_fill - ap) if side == "Buy" else (ap - avg_fill)
        return {
            f"exec{owner_id}_is":       round(is_ticks, 4),
            f"exec{owner_id}_avg_fill": round(avg_fill, 4),
            f"exec{owner_id}_qty":      total_qty,
        }
    return _extract


def metrics_all_tca(result, _params: ParamPoint) -> dict[str, float]:
    """Extract summary TCA for every registered agent."""
    out: dict[str, float] = {}
    for tca in result.tca:
        oid = tca.owner
        out[f"a{oid}_pnl"]       = tca.final_mtm_pnl
        out[f"a{oid}_position"]  = float(tca.final_position)
        out[f"a{oid}_fill_rate"] = tca.limit_fill_rate
        out[f"a{oid}_avg_slip"]  = tca.avg_slippage_ticks
    out["n_trades"]          = float(len(result.trades))
    out["cancel_failures"]   = float(result.cancel_failures)
    return out


# ─── Scenario result row ──────────────────────────────────────────────────────

@dataclass
class ScenarioRow:
    """One result row: one (params, seed) combination."""
    params:  ParamPoint
    seed:    int
    metrics: dict[str, float]
    elapsed: float = 0.0     # wall-clock seconds for this run
    error:   str   = ""      # non-empty if the run failed


# ─── ScenarioRunner ───────────────────────────────────────────────────────────

class ScenarioRunner:
    """Run a parameter sweep across multiple seeds.

    Parameters
    ----------
    world_factory : callable(params: dict) → msim.World
        Must return a freshly constructed, pre-filled World for each call.
        Called once per (params, seed) pair — never shared between runs.

    param_grid : dict[str, list]
        Parameter axes to sweep.  The runner generates the full Cartesian
        product.  Example::

            {"hawkes_mu": [5.0, 10.0, 20.0],
             "mm_gamma":  [0.005, 0.01]}

        This produces 3 × 2 = 6 scenario points.

    metrics : list[callable] | None
        List of metric extractor functions, each with signature::

            fn(result: WorldResult, params: dict) → dict[str, float]

        If None, defaults to [metrics_sf, metrics_all_tca].

    n_seeds : int
        Number of independent seeds per scenario point (default 10).

    base_seed : int
        Seeds are generated deterministically from base_seed using
        splitmix64.  Same base_seed always gives the same seed sequence.

    world_config : msim.WorldConfig | None
        Passed to World.run() for every run.  If None, uses default.

    horizon : float
        Simulation horizon in seconds (default 1.0).

    n_workers : int
        Number of parallel threads (default 4).  Set to 1 to disable
        parallelism (useful for debugging).

    verbose : bool
        Print progress to stdout (default True).

    Example
    -------
    ::

        import msim
        from msim.scenario import ScenarioRunner, metrics_sf, metrics_tca

        def factory(params):
            cfg = msim.HawkesNoiseConfig()
            cfg.hawkes.mu = params["hawkes_mu"]
            w = msim.make_world(mid=10_000)
            w.add_agent(msim.agents.HawkesNoiseTrader(owner_id=1, config=cfg))
            w.add_agent(msim.agents.MarketMakerAS(owner_id=10))
            return w

        runner = ScenarioRunner(
            world_factory = factory,
            param_grid    = {"hawkes_mu": [5.0, 10.0, 20.0]},
            metrics       = [metrics_sf, metrics_tca(owner_id=10)],
            n_seeds       = 20,
        )
        runner.run()
        df = runner.results_df()
        print(df.groupby("hawkes_mu")["excess_kurtosis"].mean())
    """

    def __init__(
        self,
        world_factory: WorldFactory,
        param_grid:    ParamGrid,
        metrics:       list[MetricFn] | None = None,
        n_seeds:       int   = 10,
        base_seed:     int   = 42,
        world_config         = None,
        horizon:       float = 1.0,
        n_workers:     int   = 4,
        verbose:       bool  = True,
    ):
        self._factory    = world_factory
        self._grid       = param_grid
        self._metrics    = metrics if metrics is not None \
                           else [metrics_sf, metrics_all_tca]
        self._n_seeds    = n_seeds
        self._base_seed  = base_seed
        self._cfg        = world_config
        self._horizon    = horizon
        self._n_workers  = max(1, n_workers)
        self._verbose    = verbose
        self._rows:      list[ScenarioRow] = []

    # ── Public API ────────────────────────────────────────────────────────────

    def run(self) -> "ScenarioRunner":
        """Execute the full sweep.  Returns self for chaining.

        ::

            runner.run().results_df()
        """
        import msim

        cfg = self._cfg
        if cfg is None:
            cfg = msim.WorldConfig()

        points   = list(self._grid_points())
        seeds    = self._make_seeds(len(points) * self._n_seeds)
        tasks    = []
        for i, params in enumerate(points):
            for j in range(self._n_seeds):
                s = seeds[i * self._n_seeds + j]
                tasks.append((params, s))

        total = len(tasks)
        if self._verbose:
            print(f"ScenarioRunner: {len(points)} param points × "
                  f"{self._n_seeds} seeds = {total} runs")
            print(f"  Workers: {self._n_workers}  Horizon: {self._horizon}s")

        self._rows = []
        done       = 0
        t0         = time.perf_counter()

        with concurrent.futures.ThreadPoolExecutor(
                max_workers=self._n_workers) as pool:
            futures = {
                pool.submit(self._run_one, params, seed, cfg): (params, seed)
                for params, seed in tasks
            }
            for fut in concurrent.futures.as_completed(futures):
                row = fut.result()
                self._rows.append(row)
                done += 1
                if self._verbose and (done % max(1, total // 20) == 0
                                      or done == total):
                    elapsed = time.perf_counter() - t0
                    pct     = 100 * done / total
                    rate    = done / elapsed if elapsed > 0 else 0
                    eta     = (total - done) / rate if rate > 0 else 0
                    print(f"  [{pct:5.1f}%] {done}/{total}  "
                          f"{rate:.1f} runs/s  ETA {eta:.0f}s")

        elapsed_total = time.perf_counter() - t0
        errors = sum(1 for r in self._rows if r.error)
        if self._verbose:
            print(f"Done in {elapsed_total:.1f}s. "
                  f"Errors: {errors}/{total}")
        return self

    def results_df(self) -> "Any":
        """Return results as a pandas DataFrame (one row per run).

        Columns: all param names + all metric names + seed + elapsed + error.

        Raises ImportError if pandas is not installed.
        """
        import pandas as pd

        rows = []
        for r in self._rows:
            row = dict(r.params)
            row.update(r.metrics)
            row["seed"]    = r.seed
            row["elapsed"] = r.elapsed
            row["error"]   = r.error
            rows.append(row)
        df = pd.DataFrame(rows)

        # Put param columns first
        param_cols  = list(self._grid.keys())
        metric_cols = [c for c in df.columns
                       if c not in param_cols + ["seed", "elapsed", "error"]]
        ordered     = param_cols + ["seed"] + metric_cols + ["elapsed", "error"]
        return df[[c for c in ordered if c in df.columns]]

    def summary_df(self) -> "Any":
        """Aggregate results across seeds: mean, std, p5, p50, p95.

        Returns a MultiIndex DataFrame indexed by param combinations.
        Numeric metric columns only.

        Raises ImportError if pandas is not installed.
        """
        import pandas as pd
        import numpy as np

        df = self.results_df()
        param_cols  = list(self._grid.keys())
        metric_cols = [c for c in df.columns
                       if c not in param_cols + ["seed", "elapsed", "error"]
                       and pd.api.types.is_numeric_dtype(df[c])]

        def agg(x):
            return pd.Series({
                "mean": x.mean(),
                "std":  x.std(),
                "p5":   x.quantile(0.05),
                "p25":  x.quantile(0.25),
                "p50":  x.quantile(0.50),
                "p75":  x.quantile(0.75),
                "p95":  x.quantile(0.95),
                "n":    x.count(),
            })

        return df.groupby(param_cols)[metric_cols].apply(
            lambda g: g[metric_cols].apply(agg)
        )

    def failed_runs(self) -> list[ScenarioRow]:
        """Return all rows where an error occurred."""
        return [r for r in self._rows if r.error]

    def n_runs(self) -> int:
        """Total number of completed runs (including failures)."""
        return len(self._rows)

    # ── Internal ──────────────────────────────────────────────────────────────

    def _grid_points(self) -> Iterator[ParamPoint]:
        """Generate Cartesian product of all parameter axes."""
        keys   = list(self._grid.keys())
        values = list(self._grid.values())
        for combo in itertools.product(*values):
            yield dict(zip(keys, combo))

    def _make_seeds(self, n: int) -> list[int]:
        """Generate n deterministic seeds from base_seed via splitmix64."""
        seeds = []
        x = self._base_seed
        for _ in range(n):
            x = (x + 0x9e3779b97f4a7c15) & 0xFFFF_FFFF_FFFF_FFFF
            z = x
            z = ((z ^ (z >> 30)) * 0xbf58476d1ce4e5b9) & 0xFFFF_FFFF_FFFF_FFFF
            z = ((z ^ (z >> 27)) * 0x94d049bb133111eb) & 0xFFFF_FFFF_FFFF_FFFF
            seeds.append(int(z ^ (z >> 31)))
        return seeds

    def _run_one(self, params: ParamPoint, seed: int, cfg) -> ScenarioRow:
        """Execute one (params, seed) combination and extract metrics."""
        t0 = time.perf_counter()
        try:
            world  = self._factory(params)
            result = world.run(seed=seed, horizon=self._horizon, config=cfg)
            metrics: dict[str, float] = {}
            for fn in self._metrics:
                metrics.update(fn(result, params))
            elapsed = time.perf_counter() - t0
            return ScenarioRow(params=dict(params), seed=seed,
                               metrics=metrics, elapsed=elapsed)
        except Exception as exc:
            elapsed = time.perf_counter() - t0
            return ScenarioRow(params=dict(params), seed=seed,
                               metrics={}, elapsed=elapsed,
                               error=str(exc))


# ─── Quick sweep helpers ──────────────────────────────────────────────────────

def single_param_sweep(
    world_factory: WorldFactory,
    param_name:    str,
    values:        Sequence[Any],
    metrics:       list[MetricFn] | None = None,
    n_seeds:       int   = 20,
    base_seed:     int   = 42,
    horizon:       float = 1.0,
    n_workers:     int   = 4,
    verbose:       bool  = True,
    world_config         = None,
) -> "Any":
    """One-liner single-axis sweep.  Returns summary_df().

    Example
    -------
    ::

        df = msim.scenario.single_param_sweep(
            world_factory = factory,
            param_name    = "hawkes_mu",
            values        = [5.0, 10.0, 15.0, 20.0],
            metrics       = [metrics_sf],
            n_seeds       = 30,
        )
        df["excess_kurtosis"]["mean"].plot()
    """
    runner = ScenarioRunner(
        world_factory = world_factory,
        param_grid    = {param_name: list(values)},
        metrics       = metrics,
        n_seeds       = n_seeds,
        base_seed     = base_seed,
        world_config  = world_config,
        horizon       = horizon,
        n_workers     = n_workers,
        verbose       = verbose,
    )
    runner.run()
    return runner.summary_df()


def sensitivity_analysis(
    world_factory:  WorldFactory,
    param_grid:     ParamGrid,
    target_metric:  str,
    metrics:        list[MetricFn] | None = None,
    n_seeds:        int   = 20,
    horizon:        float = 1.0,
    n_workers:      int   = 4,
    world_config          = None,
) -> "Any":
    """Run full grid and return mean target metric per param combination.

    Useful for sensitivity / heatmap analysis::

        df = msim.scenario.sensitivity_analysis(
            world_factory  = factory,
            param_grid     = {"hawkes_mu": [5, 10, 20],
                              "mm_gamma":  [0.005, 0.01, 0.02]},
            target_metric  = "a10_pnl",
        )
        # df is a pivot-ready DataFrame with param combinations
    """
    import pandas as pd

    runner = ScenarioRunner(
        world_factory = world_factory,
        param_grid    = param_grid,
        metrics       = metrics,
        n_seeds       = n_seeds,
        world_config  = world_config,
        horizon       = horizon,
        n_workers     = n_workers,
        verbose       = True,
    )
    runner.run()
    raw = runner.results_df()
    param_cols = list(param_grid.keys())
    if target_metric not in raw.columns:
        raise ValueError(
            f"Metric '{target_metric}' not found. "
            f"Available: {[c for c in raw.columns if c not in param_cols]}")
    return raw.groupby(param_cols)[target_metric].agg(
        ["mean", "std", "min", "max",
         lambda x: x.quantile(0.05),
         lambda x: x.quantile(0.95)]
    ).rename(columns={"<lambda_0>": "p5", "<lambda_1>": "p95"})
