#!/usr/bin/env python3
"""
Scenario Runner Quickstart
==========================
Three worked examples that show what the scenario runner
is actually for.

Example 1 — Single-axis sweep
    "How does Hawkes intensity affect stylized facts?"

Example 2 — Two-axis sensitivity heatmap
    "At which (gamma, sigma_v) does the A-S market maker
     earn the most PnL?"

Example 3 — Execution algorithm stress test
    "What is the distribution of TWAP vs IS Implementation
     Shortfall across 50 random seeds?"

Run from repo root after building:
    PYTHONPATH=build:python python python/examples/scenario_quickstart.py
"""

import msim
from msim.scenario import (
    ScenarioRunner,
    metrics_sf,
    metrics_all_tca,
    metrics_tca,
    metrics_execution_is,
    single_param_sweep,
    sensitivity_analysis,
)
from msim.execution import compare_strategies

print("=" * 64)
print("  MSIM Scenario Runner Quickstart")
print("=" * 64)


# ─── Shared helpers ───────────────────────────────────────────────────────────

def make_base_world(params: dict, *, mm_owner: int = 10) -> msim.World:
    """Background world used by examples 1 and 2."""
    w = msim.make_world(mid=10_000, levels=20, qty=10)

    # Hawkes noise traders — intensity from params
    hawkes_cfg        = msim.HawkesNoiseConfig()
    hawkes_cfg.dt_ns  = 1_000_000
    h                 = msim.HawkesConfig()
    h.mu              = params.get("hawkes_mu", 10.0)
    h.alpha           = params.get("hawkes_alpha", 4.0)
    h.beta            = params.get("hawkes_beta", 16.0)
    hawkes_cfg.hawkes = h
    for i in range(3):
        w.add_agent(msim.agents.HawkesNoiseTrader(
            owner_id=1 + i, config=hawkes_cfg))

    # Avellaneda-Stoikov market maker
    mm_cfg          = msim.MarketMakerASConfig()
    mm_cfg.gamma    = params.get("mm_gamma", 0.01)
    mm_cfg.sigma_init = params.get("mm_sigma_init", 2.0)
    mm_cfg.lot_size = 2
    mm_cfg.max_inv  = 30
    w.add_agent(msim.agents.MarketMakerAS(owner_id=mm_owner, config=mm_cfg))

    return w


# ─── Example 1: Single-axis sweep — Hawkes intensity vs stylized facts ─────────

print("\n[1] Hawkes intensity sweep (μ = 5 → 30 orders/s)")
print("    Sweeps 5 values × 20 seeds = 100 runs")

cfg1 = msim.WorldConfig()
cfg1.compute_stylized_facts = True
cfg1.record_pnl_series      = False   # faster — don't need step-by-step

try:
    df1 = single_param_sweep(
        world_factory = lambda p: make_base_world(p),
        param_name    = "hawkes_mu",
        values        = [5.0, 10.0, 15.0, 20.0, 30.0],
        metrics       = [metrics_sf, metrics_tca(owner_id=10)],
        n_seeds       = 20,
        horizon       = 1.0,
        n_workers     = 4,
        world_config  = cfg1,
    )

    print("\n  Mean stylized facts by Hawkes intensity:")
    cols = ["excess_kurtosis", "vol_clustering_ac1",
            "sign_flow_ac1",   "tw_spread"]
    available = [c for c in cols if c in df1.columns]
    if available:
        print(df1[available]["mean"].to_string())

    print("\n  Market maker mean PnL by intensity:")
    pnl_col = "a10_pnl"
    if pnl_col in df1.columns:
        print(df1[pnl_col][["mean", "std", "p5", "p95"]].to_string())

except Exception as e:
    print(f"  Error: {e}")


# ─── Example 2: Two-axis heatmap — MM gamma vs FV sigma_v ─────────────────────

print("\n[2] Sensitivity heatmap: mm_gamma × hawkes_mu")
print("    9 param combinations × 15 seeds = 135 runs")

cfg2 = msim.WorldConfig()
cfg2.compute_stylized_facts = False
cfg2.record_pnl_series      = False
cfg2.record_fills           = False   # fastest possible — just TCA summary

try:
    runner2 = ScenarioRunner(
        world_factory = lambda p: make_base_world(p),
        param_grid    = {
            "mm_gamma":  [0.005, 0.01, 0.02],
            "hawkes_mu": [5.0, 10.0, 20.0],
        },
        metrics       = [metrics_tca(owner_id=10)],
        n_seeds       = 15,
        horizon       = 1.0,
        n_workers     = 4,
        world_config  = cfg2,
        verbose       = True,
    )
    runner2.run()

    raw2    = runner2.results_df()
    pnl_col = "agent10_pnl"

    if pnl_col in raw2.columns:
        pivot = raw2.groupby(["mm_gamma", "hawkes_mu"])[pnl_col].mean().unstack()
        print("\n  Mean MM PnL heatmap (rows=mm_gamma, cols=hawkes_mu):")
        print(pivot.round(2).to_string())

        # Also show which combination is most profitable
        best_idx = raw2.groupby(
            ["mm_gamma", "hawkes_mu"])[pnl_col].mean().idxmax()
        print(f"\n  Best combination: mm_gamma={best_idx[0]}, "
              f"hawkes_mu={best_idx[1]}")

except Exception as e:
    print(f"  Error: {e}")


# ─── Example 3: Execution IS stress test across seeds ─────────────────────────

print("\n[3] Execution IS stress test: TWAP vs IS (50 seeds each)")

HORIZON_S     = 2.0
HORIZON_STEPS = 2000
TOTAL_QTY     = 300


def make_exec_world(params: dict) -> msim.World:
    """Background world + one execution agent specified by params."""
    w = msim.make_world(mid=10_000, levels=25, qty=15)

    # Background
    hcfg        = msim.HawkesNoiseConfig()
    hcfg.dt_ns  = 1_000_000
    h           = msim.HawkesConfig()
    h.mu        = 10.0; h.alpha = 4.0; h.beta = 16.0
    hcfg.hawkes = h
    for i in range(3):
        w.add_agent(msim.agents.HawkesNoiseTrader(
            owner_id=1 + i, config=hcfg))

    mm_cfg          = msim.MarketMakerASConfig()
    mm_cfg.lot_size = 3; mm_cfg.max_inv = 50
    w.add_agent(msim.agents.MarketMakerAS(owner_id=10, config=mm_cfg))

    # Execution agent: chosen by params["strategy"]
    strategy = params.get("strategy", "TWAP")
    if strategy == "TWAP":
        tcfg           = msim.TWAPConfig()
        tcfg.total_qty = TOTAL_QTY
        tcfg.side      = msim.Side.Buy
        agent = msim.agents.TWAPAgent(
            owner_id=50, horizon_steps=HORIZON_STEPS, config=tcfg)

    elif strategy == "VWAP":
        vcfg           = msim.VWAPConfig()
        vcfg.total_qty = TOTAL_QTY
        vcfg.side      = msim.Side.Buy
        vcfg.schedule  = msim.VWAPSchedule.U_SHAPE
        agent = msim.agents.VWAPAgent(owner_id=51, config=vcfg)
        agent.set_total_steps(HORIZON_STEPS)

    else:  # IS
        lam = params.get("risk_aversion", 0.01)
        icfg               = msim.ISConfig()
        icfg.total_qty     = TOTAL_QTY
        icfg.side          = msim.Side.Buy
        icfg.risk_aversion = lam
        icfg.sigma         = 2.0
        icfg.eta           = 0.5
        agent = msim.agents.ISAgent(
            owner_id=52, horizon_steps=HORIZON_STEPS, config=icfg)

    w.add_agent(agent)
    return w


def owner_for(strategy: str) -> int:
    return {"TWAP": 50, "VWAP": 51, "IS": 52}.get(strategy, 50)


cfg3 = msim.WorldConfig()
cfg3.compute_stylized_facts = False
cfg3.record_pnl_series      = False
cfg3.record_fills           = True   # need fills for IS computation

try:
    runner3 = ScenarioRunner(
        world_factory = make_exec_world,
        param_grid    = {
            "strategy":      ["TWAP", "VWAP", "IS"],
            "risk_aversion": [0.01],   # only used by IS; ignored by TWAP/VWAP
        },
        metrics       = [
            metrics_execution_is(owner_id=50, side="Buy"),  # TWAP
            metrics_execution_is(owner_id=51, side="Buy"),  # VWAP
            metrics_execution_is(owner_id=52, side="Buy"),  # IS
        ],
        n_seeds       = 50,
        horizon       = HORIZON_S,
        n_workers     = 4,
        world_config  = cfg3,
        verbose       = True,
    )
    runner3.run()

    raw3 = runner3.results_df()

    print("\n  IS distribution by strategy (ticks, lower = better):")
    for strat, oid in [("TWAP", 50), ("VWAP", 51), ("IS", 52)]:
        col = f"exec{oid}_is"
        sub = raw3[raw3["strategy"] == strat][col].dropna()
        if not sub.empty:
            print(f"    {strat:6s}  mean={sub.mean():+.3f}  "
                  f"std={sub.std():.3f}  "
                  f"p5={sub.quantile(0.05):+.3f}  "
                  f"p95={sub.quantile(0.95):+.3f}")

    print("\n  Completion rate by strategy:")
    for strat, oid in [("TWAP", 50), ("VWAP", 51), ("IS", 52)]:
        col = f"exec{oid}_qty"
        sub = raw3[raw3["strategy"] == strat][col].dropna()
        if not sub.empty:
            pct = sub.mean() / TOTAL_QTY * 100
            print(f"    {strat:6s}  avg_qty={sub.mean():.0f} / {TOTAL_QTY}"
                  f"  ({pct:.1f}%)")

except Exception as e:
    print(f"  Error: {e}")


# ─── Optional plots ────────────────────────────────────────────────────────────

try:
    import matplotlib.pyplot as plt
    import numpy as np

    fig, axes = plt.subplots(1, 3, figsize=(16, 5))
    fig.suptitle("MSIM Scenario Runner Results", fontsize=13,
                 fontweight="bold")

    # Plot 1: Kurtosis vs Hawkes mu
    try:
        ax = axes[0]
        grp = df1["excess_kurtosis"]
        ax.errorbar(
            [5, 10, 15, 20, 30],
            grp["mean"].values,
            yerr=grp["std"].values,
            fmt="o-", color="#2563eb", capsize=4, lw=1.5,
        )
        ax.axhline(3.0, color="#6b7280", ls="--", lw=0.8,
                   label="Normal dist. (κ=3)")
        ax.set_xlabel("Hawkes μ (orders/s)")
        ax.set_ylabel("Excess kurtosis")
        ax.set_title("Fat tails vs arrival rate")
        ax.legend(); ax.grid(True, alpha=0.3)
    except Exception:
        axes[0].text(0.5, 0.5, "Ex.1 data unavailable",
                     ha="center", transform=axes[0].transAxes)

    # Plot 2: PnL heatmap
    try:
        ax   = axes[1]
        pnl_col = "agent10_pnl"
        pivot = raw2.groupby(
            ["mm_gamma", "hawkes_mu"])[pnl_col].mean().unstack()
        im = ax.imshow(pivot.values, aspect="auto", cmap="RdYlGn",
                       origin="lower")
        plt.colorbar(im, ax=ax, label="Mean PnL (ticks)")
        ax.set_xticks(range(len(pivot.columns)))
        ax.set_xticklabels([f"{v}" for v in pivot.columns])
        ax.set_yticks(range(len(pivot.index)))
        ax.set_yticklabels([f"{v}" for v in pivot.index])
        ax.set_xlabel("Hawkes μ")
        ax.set_ylabel("MM γ")
        ax.set_title("MM PnL heatmap")
    except Exception:
        axes[1].text(0.5, 0.5, "Ex.2 data unavailable",
                     ha="center", transform=axes[1].transAxes)

    # Plot 3: IS distribution comparison
    try:
        ax = axes[2]
        colors = {"TWAP": "#f59e0b", "VWAP": "#10b981", "IS": "#2563eb"}
        for strat, oid in [("TWAP", 50), ("VWAP", 51), ("IS", 52)]:
            col = f"exec{oid}_is"
            sub = raw3[raw3["strategy"] == strat][col].dropna()
            if not sub.empty:
                ax.hist(sub, bins=20, alpha=0.6,
                        color=colors[strat], label=strat, density=True)
        ax.axvline(0, color="black", lw=0.8, ls="--")
        ax.set_xlabel("IS (ticks)")
        ax.set_ylabel("Density")
        ax.set_title("IS distribution (50 seeds)")
        ax.legend(); ax.grid(True, alpha=0.3)
    except Exception:
        axes[2].text(0.5, 0.5, "Ex.3 data unavailable",
                     ha="center", transform=axes[2].transAxes)

    plt.tight_layout()
    plt.savefig("scenario_results.png", dpi=150, bbox_inches="tight")
    print("\nSaved scenario_results.png")
    plt.close()

except ImportError:
    print("\n  (install matplotlib for plots)")

print("\nDone.")
