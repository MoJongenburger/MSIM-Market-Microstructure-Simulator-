"""
msim.analysis — Post-simulation analysis and visualisation
===========================================================
All functions accept a WorldResult directly.  The TCA data
(fills, pnl_series, tca) is now computed live in C++ so these
functions are simple wrappers rather than expensive reconstructions.

Requires pandas.  matplotlib is optional (only for plot functions).
"""
from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    import pandas as pd


# ─── Primary TCA accessors ────────────────────────────────────────────────────

def pnl_df(result, owner_id: int | None = None) -> "pd.DataFrame":
    """Return the mark-to-market PnL series from C++ step snapshots.

    Parameters
    ----------
    result   : WorldResult
    owner_id : int | None
        If given, filter to one agent. If None, return all agents.

    Returns
    -------
    DataFrame with columns: ts, owner, position, cash_ticks, mid, mtm_pnl

    Example
    -------
    >>> df = msim.analysis.pnl_df(result, owner_id=10)
    >>> df.set_index("ts")["mtm_pnl"].plot()
    """
    df = result.pnl_df()
    if owner_id is not None:
        df = df[df["owner"] == owner_id].reset_index(drop=True)
    return df


def fills_df(result, owner_id: int | None = None,
             maker_only: bool = False,
             taker_only: bool = False) -> "pd.DataFrame":
    """Return per-fill execution data from C++ FillRecords.

    Parameters
    ----------
    result     : WorldResult
    owner_id   : int | None — filter to one agent if given
    maker_only : bool       — only passive (limit) fills
    taker_only : bool       — only aggressive (market/IOC) fills

    Returns
    -------
    DataFrame with columns:
        ts, owner, order_id, side, fill_qty, fill_price,
        arrival_mid, is_maker, slippage

    Example
    -------
    >>> taker_fills = msim.analysis.fills_df(result, owner_id=10, taker_only=True)
    >>> taker_fills["slippage"].describe()
    """
    df = result.fills_df()
    if owner_id is not None:
        df = df[df["owner"] == owner_id]
    if maker_only:
        df = df[df["is_maker"]]
    if taker_only:
        df = df[~df["is_maker"]]
    return df.reset_index(drop=True)


def tca_df(result) -> "pd.DataFrame":
    """Return per-agent TCA summary from C++ AgentTCA structs.

    Always populated, regardless of record_fills / record_pnl_series.

    Returns
    -------
    DataFrame with one row per agent and columns:
        owner, n_orders_submitted, n_limit_submitted, n_market_submitted,
        n_cancels_sent, n_fills_maker, n_fills_taker, total_qty_traded,
        limit_fill_rate, avg_slippage_ticks, turnover_notional_ticks,
        final_position, final_cash_ticks, final_mtm_pnl
    """
    return result.tca_df()


def tca_report(result) -> str:
    """Return a formatted TCA report string for all agents.

    Example
    -------
    >>> print(msim.analysis.tca_report(result))
    """
    df = tca_df(result)
    if df.empty:
        return "No TCA data (no agents registered)."

    lines = ["=" * 62, "  MSIM Transaction Cost Analysis Report", "=" * 62]
    for _, row in df.iterrows():
        lines.append(f"\nAgent {int(row['owner'])}")
        lines.append(f"  Orders submitted : {int(row['n_orders_submitted']):>8}")
        lines.append(f"    Limit          : {int(row['n_limit_submitted']):>8}")
        lines.append(f"    Market/IOC     : {int(row['n_market_submitted']):>8}")
        lines.append(f"    Cancels sent   : {int(row['n_cancels_sent']):>8}")
        lines.append(f"  Fills (maker)    : {int(row['n_fills_maker']):>8}")
        lines.append(f"  Fills (taker)    : {int(row['n_fills_taker']):>8}")
        lines.append(f"  Total qty traded : {int(row['total_qty_traded']):>8}")
        fr = row['limit_fill_rate']
        lines.append(f"  Limit fill rate  : {fr:>7.1%}")
        slip = row['avg_slippage_ticks']
        lines.append(f"  Avg slippage     : {slip:>+8.3f} ticks")
        lines.append(f"  Final position   : {int(row['final_position']):>+8}")
        lines.append(f"  Final cash       : {int(row['final_cash_ticks']):>+8} ticks")
        lines.append(f"  Final MtM PnL    : {row['final_mtm_pnl']:>+8.1f} ticks")
    lines.append("=" * 62)
    return "\n".join(lines)


# ─── Derived metrics ──────────────────────────────────────────────────────────

def sharpe(result, owner_id: int, periods_per_year: float = 252.0) -> float:
    """Annualised Sharpe ratio from the PnL series.

    Uses step-to-step PnL differences as returns.

    Parameters
    ----------
    result           : WorldResult
    owner_id         : int
    periods_per_year : float — annualisation factor (default 252 trading days)

    Returns
    -------
    float — annualised Sharpe ratio (nan if insufficient data)
    """
    import math
    df = pnl_df(result, owner_id)
    if len(df) < 2:
        return float("nan")
    pnl = df["mtm_pnl"]
    rets = pnl.diff().dropna()
    if rets.std() == 0:
        return float("nan")
    # Number of steps per year depends on dt_ns
    # We estimate from the ts series
    dt_ns = int(df["ts"].diff().dropna().median())
    steps_per_year = int(365.25 * 24 * 3600 * 1e9 / dt_ns) if dt_ns > 0 else 1
    return float((rets.mean() / rets.std()) * math.sqrt(steps_per_year))


def max_drawdown(result, owner_id: int) -> float:
    """Maximum drawdown of the MtM PnL series in ticks.

    Returns
    -------
    float — maximum peak-to-trough drawdown (always <= 0)
    """
    df = pnl_df(result, owner_id)
    if df.empty:
        return 0.0
    pnl = df["mtm_pnl"]
    return float((pnl - pnl.cummax()).min())


def slippage_summary(result, owner_id: int) -> dict:
    """Slippage statistics for taker fills of one agent.

    Returns
    -------
    dict with keys: n_taker_fills, mean, std, p25, p50, p75, p95
    """
    df = fills_df(result, owner_id=owner_id, taker_only=True)
    if df.empty:
        return {"n_taker_fills": 0}
    s = df["slippage"]
    return {
        "n_taker_fills": len(df),
        "mean": float(s.mean()),
        "std":  float(s.std()),
        "p25":  float(s.quantile(0.25)),
        "p50":  float(s.quantile(0.50)),
        "p75":  float(s.quantile(0.75)),
        "p95":  float(s.quantile(0.95)),
    }


def stylized_facts_df(sf) -> "pd.DataFrame":
    """Convert a StyleFacts object to a one-row summary DataFrame."""
    import pandas as pd
    ac1 = lambda v: v[0] if v else float("nan")
    return pd.DataFrame([{
        "excess_kurtosis":       sf.returns.excess_kurtosis,
        "return_std":            sf.returns.std_dev,
        "return_skew":           sf.returns.skewness,
        "return_ac_lag1":        ac1(sf.autocorr.return_ac),
        "abs_return_ac_lag1":    ac1(sf.autocorr.abs_return_ac),
        "sign_flow_ac_lag1":     ac1(sf.autocorr.sign_flow_ac),
        "kyle_lambda":           sf.impact.kyle_lambda,
        "impact_r2":             sf.impact.r_squared,
        "power_exponent":        sf.impact.power_exponent,
        "tw_spread":             sf.spreads.time_weighted_spread,
        "effective_spread":      sf.spreads.effective_spread_mean,
        "realized_spread":       sf.spreads.realized_spread_mean,
        "adverse_selection":     sf.spreads.adverse_selection_mean,
        "amihud_mean":           sf.amihud.mean_illiq,
        "fat_tails_ok":          sf.fat_tails_ok,
        "vol_clustering_ok":     sf.vol_clustering_ok,
        "flow_autocorr_ok":      sf.flow_autocorr_ok,
        "positive_spread_ok":    sf.positive_spread_ok,
        "positive_impact_ok":    sf.positive_impact_ok,
        "all_pass":              sf.passes(),
    }])


# ─── Plotting ─────────────────────────────────────────────────────────────────

def plot_pnl(result, owner_id: int, ax=None, label: str | None = None):
    """Plot mark-to-market PnL for one agent."""
    import matplotlib.pyplot as plt
    df = pnl_df(result, owner_id)
    if df.empty:
        return ax
    if ax is None:
        _, ax = plt.subplots(figsize=(12, 4))
    ts_s = df["ts"] / 1e9
    ax.plot(ts_s, df["mtm_pnl"],
            lw=1.2, label=label or f"Agent {owner_id}")
    ax.fill_between(ts_s, df["mtm_pnl"], 0,
                    where=(df["mtm_pnl"] >= 0), alpha=0.15, color="#10b981")
    ax.fill_between(ts_s, df["mtm_pnl"], 0,
                    where=(df["mtm_pnl"] < 0),  alpha=0.15, color="#ef4444")
    ax.axhline(0, color="black", lw=0.6, ls="--")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("MtM PnL (ticks)")
    ax.set_title(f"Mark-to-market PnL — Agent {owner_id}")
    ax.legend()
    ax.grid(True, alpha=0.3)
    return ax


def plot_position(result, owner_id: int, ax=None):
    """Plot signed inventory through time for one agent."""
    import matplotlib.pyplot as plt
    df = pnl_df(result, owner_id)
    if df.empty:
        return ax
    if ax is None:
        _, ax = plt.subplots(figsize=(12, 3))
    ax.step(df["ts"] / 1e9, df["position"], where="post",
            lw=1.0, color="#6366f1")
    ax.axhline(0, color="black", lw=0.5, ls="--")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Position (lots)")
    ax.set_title(f"Inventory — Agent {owner_id}")
    ax.grid(True, alpha=0.3)
    return ax


def plot_slippage_hist(result, owner_id: int, ax=None, bins: int = 40):
    """Histogram of taker fill slippage for one agent."""
    import matplotlib.pyplot as plt
    df = fills_df(result, owner_id=owner_id, taker_only=True)
    if df.empty:
        return ax
    if ax is None:
        _, ax = plt.subplots(figsize=(8, 4))
    ax.hist(df["slippage"], bins=bins, color="#f59e0b",
            alpha=0.8, edgecolor="white")
    ax.axvline(df["slippage"].mean(), color="#ef4444",
               lw=1.5, ls="--", label=f"Mean {df['slippage'].mean():.2f}")
    ax.axvline(0, color="black", lw=0.8)
    ax.set_xlabel("Slippage (ticks)")
    ax.set_ylabel("Count")
    ax.set_title(f"Taker slippage distribution — Agent {owner_id}")
    ax.legend()
    ax.grid(True, alpha=0.3)
    return ax


def plot_mid_price(result, ax=None, title: str = "Mid-price evolution"):
    """Plot mid-price through time."""
    import matplotlib.pyplot as plt
    df = result.tops_df().dropna(subset=["mid"])
    if ax is None:
        _, ax = plt.subplots(figsize=(12, 4))
    ax.plot(df["ts"] / 1e9, df["mid"], lw=0.8, color="#2563eb")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Mid price (ticks)")
    ax.set_title(title)
    ax.grid(True, alpha=0.3)
    return ax


def dashboard(result, owner_id: int | None = None):
    """Six-panel strategy dashboard.

    Panels: mid-price, spread, PnL, position, slippage histogram, TCA table.
    """
    import matplotlib.pyplot as plt

    fig = plt.figure(figsize=(16, 10))
    fig.suptitle("MSIM Strategy Dashboard", fontsize=14, fontweight="bold")

    ax1 = fig.add_subplot(3, 2, 1)
    ax2 = fig.add_subplot(3, 2, 2)
    ax3 = fig.add_subplot(3, 2, 3)
    ax4 = fig.add_subplot(3, 2, 4)
    ax5 = fig.add_subplot(3, 2, 5)
    ax6 = fig.add_subplot(3, 2, 6)

    # Mid-price
    plot_mid_price(result, ax=ax1)

    # Spread
    tops = result.tops_df().dropna(subset=["best_bid", "best_ask"])
    if not tops.empty:
        tops["spread"] = tops["best_ask"] - tops["best_bid"]
        ax2.plot(tops["ts"] / 1e9, tops["spread"], lw=0.6, color="#10b981")
        ax2.set_xlabel("Time (s)")
        ax2.set_ylabel("Spread (ticks)")
        ax2.set_title("Bid-ask spread")
        ax2.grid(True, alpha=0.3)

    if owner_id is not None:
        plot_pnl(result, owner_id, ax=ax3)
        plot_position(result, owner_id, ax=ax4)
        plot_slippage_hist(result, owner_id, ax=ax5)

        # TCA summary table
        df_tca = tca_df(result)
        row = df_tca[df_tca["owner"] == owner_id]
        if not row.empty:
            r = row.iloc[0]
            metrics = [
                ("Orders submitted", f"{int(r['n_orders_submitted'])}"),
                ("Limit fill rate",  f"{r['limit_fill_rate']:.1%}"),
                ("Avg slippage",     f"{r['avg_slippage_ticks']:+.3f} ticks"),
                ("Total qty traded", f"{int(r['total_qty_traded'])}"),
                ("Final position",   f"{int(r['final_position']):+d}"),
                ("Final MtM PnL",    f"{r['final_mtm_pnl']:+.1f} ticks"),
            ]
            ax6.axis("off")
            y = 0.95
            ax6.text(0.05, y, f"TCA Summary — Agent {owner_id}",
                     fontsize=11, fontweight="bold",
                     transform=ax6.transAxes)
            for label, value in metrics:
                y -= 0.13
                ax6.text(0.05, y, label, fontsize=9,
                         transform=ax6.transAxes, color="#6b7280")
                ax6.text(0.65, y, value, fontsize=9,
                         transform=ax6.transAxes, fontweight="bold")
    else:
        # No agent: show trade volume histogram
        trades = result.trades_df()
        if not trades.empty:
            ax3.hist(trades["price"], bins=50, color="#2563eb", alpha=0.7)
            ax3.set_xlabel("Price (ticks)")
            ax3.set_ylabel("Count")
            ax3.set_title("Trade price distribution")
            ax3.grid(True, alpha=0.3)

    plt.tight_layout()
    return fig
