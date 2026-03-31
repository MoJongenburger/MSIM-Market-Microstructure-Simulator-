"""
msim.execution — Execution algorithm analysis
==============================================
Benchmarking helpers for VWAP, TWAP, and IS execution agents.

All functions operate on WorldResult objects produced by running
an execution agent alongside noise traders and a market maker.

The key metric is Implementation Shortfall (IS), defined as:

    IS (ticks) = (avg_fill_price - arrival_price) * qty_executed  [buys]
               = (arrival_price - avg_fill_price) * qty_executed  [sells]

Positive IS means you paid more than the arrival price (slippage cost).
The goal is to minimise IS while completing the full parent order.
"""
from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    import pandas as pd


# ─── Core IS computation ──────────────────────────────────────────────────────

def implementation_shortfall(result, owner_id: int,
                             arrival_price: int | None = None) -> dict:
    """Compute Implementation Shortfall for one execution agent.

    Parameters
    ----------
    result        : WorldResult
    owner_id      : int
        The execution agent's OwnerId.
    arrival_price : int | None
        Mid-price at order submission.  If None, inferred from the
        first fill's arrival_mid field (set by the C++ TCA layer).

    Returns
    -------
    dict with keys:
        qty_executed     — total lots filled
        qty_remaining    — lots not filled (incomplete execution)
        avg_fill_price   — VWAP of all fills (weighted by qty)
        arrival_price    — mid at submission
        is_ticks         — implementation shortfall in ticks
        is_bps           — IS in basis points vs arrival price
        n_child_orders   — number of child orders submitted
        completion_rate  — qty_executed / total_submitted_qty
    """
    # Get fill records for this agent's taker fills
    fills = result.fills_df()
    agent_fills = fills[(fills["owner"] == owner_id) & (~fills["is_maker"])].copy()

    if agent_fills.empty:
        return {
            "qty_executed": 0,
            "qty_remaining": None,
            "avg_fill_price": None,
            "arrival_price": arrival_price,
            "is_ticks": None,
            "is_bps": None,
            "n_child_orders": 0,
            "completion_rate": 0.0,
        }

    # VWAP of fills
    total_qty   = int(agent_fills["fill_qty"].sum())
    total_notional = int((agent_fills["fill_price"] * agent_fills["fill_qty"]).sum())
    avg_fill = total_notional / total_qty if total_qty > 0 else 0.0

    # Arrival price from first fill if not provided
    if arrival_price is None:
        arrival_price = int(agent_fills["arrival_mid"].iloc[0])

    # IS: positive = cost (paid above arrival for buys)
    # Determine side from fills
    side = str(agent_fills["side"].iloc[0])
    if side == "Buy":
        is_ticks = avg_fill - arrival_price
    else:
        is_ticks = arrival_price - avg_fill

    is_bps = (is_ticks / arrival_price * 10000.0) if arrival_price > 0 else None

    # Number of distinct child order submissions (unique order_ids)
    n_child = int(agent_fills["order_id"].nunique())

    # Completion: compare to TCA summary
    tca_df = result.tca_df()
    agent_tca = tca_df[tca_df["owner"] == owner_id]
    total_submitted = int(agent_tca["n_market_submitted"].iloc[0]) if not agent_tca.empty else n_child

    return {
        "qty_executed":    total_qty,
        "qty_remaining":   None,  # not tracked here — use agent.qty_remaining()
        "avg_fill_price":  round(avg_fill, 4),
        "arrival_price":   arrival_price,
        "is_ticks":        round(is_ticks, 4),
        "is_bps":          round(is_bps, 2) if is_bps else None,
        "n_child_orders":  n_child,
        "completion_rate": total_qty / max(total_submitted, 1),
    }


def execution_timeline(result, owner_id: int) -> "pd.DataFrame":
    """Return a step-by-step execution timeline.

    Merges taker fills with mid-price to show when execution happened
    relative to price moves.

    Returns
    -------
    DataFrame with columns:
        ts, fill_qty, fill_price, mid, slippage, cumulative_qty
    """
    fills = result.fills_df()
    agent = fills[(fills["owner"] == owner_id) & (~fills["is_maker"])].copy()
    if agent.empty:
        import pandas as pd
        return pd.DataFrame(columns=[
            "ts", "fill_qty", "fill_price", "mid", "slippage", "cumulative_qty"])

    agent = agent.sort_values("ts").reset_index(drop=True)
    agent["cumulative_qty"] = agent["fill_qty"].cumsum()
    return agent[["ts", "fill_qty", "fill_price", "arrival_mid",
                  "slippage", "cumulative_qty"]].rename(
        columns={"arrival_mid": "mid_at_submission"})


def compare_strategies(results: dict[str, object],
                       owner_ids: dict[str, int],
                       arrival_prices: dict[str, int] | None = None) -> "pd.DataFrame":
    """Compare IS across multiple strategies / runs.

    Parameters
    ----------
    results       : dict[str, WorldResult]   — strategy name → result
    owner_ids     : dict[str, int]           — strategy name → owner_id
    arrival_prices: dict[str, int] | None    — optional override per strategy

    Returns
    -------
    DataFrame indexed by strategy name, columns = IS metrics.

    Example
    -------
    >>> comparison = msim.execution.compare_strategies(
    ...     results    = {"TWAP": r_twap, "IS": r_is, "MyStrat": r_mine},
    ...     owner_ids  = {"TWAP": 51, "IS": 52, "MyStrat": 99},
    ... )
    >>> comparison.sort_values("is_ticks")
    """
    import pandas as pd

    rows = []
    for name, result in results.items():
        oid  = owner_ids[name]
        ap   = (arrival_prices or {}).get(name)
        row  = implementation_shortfall(result, oid, arrival_price=ap)
        row["strategy"] = name
        rows.append(row)
    df = pd.DataFrame(rows).set_index("strategy")
    return df


# ─── VWAP benchmark ───────────────────────────────────────────────────────────

def vwap_benchmark(result) -> float:
    """Compute the market VWAP from all trades in the simulation.

    This is the "fair" benchmark price — execution at exactly market VWAP
    means zero market impact relative to the rest of the market.

    Returns
    -------
    float — volume-weighted average trade price across all trades
    """
    df = result.trades_df()
    if df.empty:
        return 0.0
    total_notional = (df["price"] * df["qty"]).sum()
    total_qty = df["qty"].sum()
    return float(total_notional / total_qty) if total_qty > 0 else 0.0


def vwap_is(result, owner_id: int) -> float:
    """Compute IS relative to market VWAP rather than arrival price.

    A positive value means you executed worse than the market VWAP.
    Zero means you executed exactly at market VWAP (ideal).

    Returns
    -------
    float — IS vs market VWAP in ticks
    """
    fills = result.fills_df()
    agent = fills[(fills["owner"] == owner_id) & (~fills["is_maker"])]
    if agent.empty:
        return 0.0

    total_qty = int(agent["fill_qty"].sum())
    if total_qty == 0:
        return 0.0

    avg_fill = float((agent["fill_price"] * agent["fill_qty"]).sum()) / total_qty
    mkt_vwap = vwap_benchmark(result)
    side = str(agent["side"].iloc[0])
    return (avg_fill - mkt_vwap) if side == "Buy" else (mkt_vwap - avg_fill)


# ─── Plotting ─────────────────────────────────────────────────────────────────

def plot_execution(result, owner_id: int,
                  label: str = "Strategy",
                  ax=None):
    """Plot cumulative execution curve overlaid on mid-price.

    Shows: mid-price (left axis), cumulative qty filled (right axis).

    Parameters
    ----------
    result   : WorldResult
    owner_id : int
    label    : str — legend label
    ax       : matplotlib Axes | None

    Returns
    -------
    The matplotlib Axes object.
    """
    import matplotlib.pyplot as plt

    tops  = result.tops_df().dropna(subset=["mid"])
    fills = result.fills_df()
    agent = fills[(fills["owner"] == owner_id) & (~fills["is_maker"])].copy()

    if ax is None:
        fig, ax = plt.subplots(figsize=(12, 5))
    else:
        fig = ax.figure

    # Mid-price
    ax.plot(tops["ts"] / 1e9, tops["mid"],
            lw=0.8, color="#94a3b8", alpha=0.7, label="Mid price")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Mid price (ticks)")

    if not agent.empty:
        # Cumulative fills on a secondary axis
        agent = agent.sort_values("ts")
        agent["cum_qty"] = agent["fill_qty"].cumsum()

        ax2 = ax.twinx()
        ax2.step(agent["ts"] / 1e9, agent["cum_qty"],
                 where="post", lw=2.0, color="#2563eb", label=label)
        ax2.scatter(agent["ts"] / 1e9, agent["cum_qty"],
                    s=20, color="#2563eb", zorder=5)
        ax2.set_ylabel("Cumulative qty filled (lots)")

        # Arrival price line
        ap = int(agent["arrival_mid"].iloc[0])
        ax.axhline(ap, color="#ef4444", lw=1.0, ls="--",
                   label=f"Arrival mid={ap}")

        # Combined legend
        lines1, labels1 = ax.get_legend_handles_labels()
        lines2, labels2 = ax2.get_legend_handles_labels()
        ax.legend(lines1 + lines2, labels1 + labels2, loc="upper left")

    ax.set_title(f"Execution timeline — {label}")
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    return ax


def plot_is_comparison(comparison_df, ax=None):
    """Bar chart comparing IS in ticks across strategies.

    Parameters
    ----------
    comparison_df : DataFrame from compare_strategies()
    ax            : matplotlib Axes | None
    """
    import matplotlib.pyplot as plt

    if ax is None:
        _, ax = plt.subplots(figsize=(8, 5))

    df = comparison_df["is_ticks"].sort_values()
    colors = ["#10b981" if v <= 0 else "#ef4444" for v in df]
    ax.barh(df.index, df.values, color=colors, alpha=0.85, edgecolor="white")
    ax.axvline(0, color="black", lw=0.8)
    ax.set_xlabel("IS (ticks) — lower is better")
    ax.set_title("Implementation Shortfall Comparison")
    ax.grid(True, alpha=0.3, axis="x")

    for i, (idx, val) in enumerate(df.items()):
        ax.text(val + (0.02 if val >= 0 else -0.02), i,
                f"{val:+.3f}", va="center",
                ha="left" if val >= 0 else "right", fontsize=9)
    return ax
