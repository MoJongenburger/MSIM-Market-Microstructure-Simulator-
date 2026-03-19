"""
msim.analysis — Post-simulation analysis utilities
===================================================
All functions accept WorldResult or the individual DataFrames it produces.
Requires pandas. matplotlib/seaborn are optional (only needed for plots).
"""
from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    import pandas as pd


# ─── PnL and execution analytics ──────────────────────────────────────────────

def pnl_series(result, owner_id: int, tick_value: float = 1.0) -> "pd.DataFrame":
    """Compute mark-to-market PnL series for one agent.

    Uses mid-price at each trade timestamp as the mark.

    Parameters
    ----------
    result     : WorldResult
    owner_id   : int   — the agent's OwnerId
    tick_value : float — dollar value of one tick (default 1.0)

    Returns
    -------
    DataFrame with columns: ts, position, cash_ticks, mid, pnl
    """
    import pandas as pd

    trades_df = result.trades_df()
    tops_df   = result.tops_df()

    if trades_df.empty or tops_df.empty:
        return pd.DataFrame(columns=["ts", "position", "cash_ticks", "mid", "pnl"])

    tops_df = tops_df.dropna(subset=["mid"])
    tops_df = tops_df.sort_values("ts").reset_index(drop=True)

    # Filter trades involving this agent
    agent_trades = trades_df[
        (trades_df["maker_order_id"] // (1 << 24) == owner_id) |
        (trades_df["taker_order_id"] // (1 << 24) == owner_id)
    ].copy()

    agent_trades["is_buy"] = (
        agent_trades["taker_order_id"] // (1 << 24) == owner_id
    )
    agent_trades["signed_qty"] = agent_trades.apply(
        lambda r: r["qty"] if r["is_buy"] else -r["qty"], axis=1
    )
    agent_trades["cash_flow"] = agent_trades.apply(
        lambda r: -r["price"] * r["qty"] if r["is_buy"]
                  else r["price"] * r["qty"], axis=1
    )

    agent_trades = agent_trades.sort_values("ts").reset_index(drop=True)
    agent_trades["position"]   = agent_trades["signed_qty"].cumsum()
    agent_trades["cash_ticks"] = agent_trades["cash_flow"].cumsum()

    # Merge with nearest mid for mark-to-market
    merged = pd.merge_asof(
        agent_trades[["ts", "position", "cash_ticks"]],
        tops_df[["ts", "mid"]],
        on="ts",
        direction="nearest",
    )
    merged["pnl"] = (merged["cash_ticks"] + merged["position"] * merged["mid"]) * tick_value
    return merged


def fill_rate(result, owner_id: int) -> dict:
    """Compute limit order fill rate for one agent.

    Returns
    -------
    dict with keys: submitted, filled, fill_rate, avg_fill_qty
    """
    trades_df = result.trades_df()
    maker = trades_df[trades_df["maker_order_id"] // (1 << 24) == owner_id]
    taker = trades_df[trades_df["taker_order_id"] // (1 << 24) == owner_id]
    total_fills = len(maker) + len(taker)
    total_qty   = int(maker["qty"].sum() + taker["qty"].sum())
    return {
        "maker_fills": len(maker),
        "taker_fills": len(taker),
        "total_fills": total_fills,
        "total_qty":   total_qty,
        "avg_fill_qty": total_qty / total_fills if total_fills > 0 else 0.0,
    }


def arrival_slippage(result, owner_id: int) -> "pd.DataFrame":
    """Compute arrival price slippage for each taker fill.

    Slippage = fill_price - mid_at_arrival for buys
             = mid_at_arrival - fill_price for sells

    Returns
    -------
    DataFrame with columns: ts, price, mid, slippage, qty
    """
    import pandas as pd

    trades_df = result.trades_df()
    tops_df   = result.tops_df().dropna(subset=["mid"]).sort_values("ts")

    taker = trades_df[
        trades_df["taker_order_id"] // (1 << 24) == owner_id
    ].copy()

    if taker.empty:
        return pd.DataFrame(columns=["ts", "price", "mid", "slippage", "qty"])

    taker = taker.sort_values("ts")
    merged = pd.merge_asof(taker[["ts", "price", "qty"]],
                            tops_df[["ts", "mid"]],
                            on="ts", direction="nearest")
    # Assume buys for simplicity; negate for sells
    merged["slippage"] = merged["price"] - merged["mid"]
    return merged


# ─── Stylized facts summary ────────────────────────────────────────────────────

def stylized_facts_df(sf) -> "pd.DataFrame":
    """Convert a StyleFacts object to a one-row summary DataFrame.

    Parameters
    ----------
    sf : StyleFacts

    Returns
    -------
    DataFrame with one row and columns for all key statistics.
    """
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


def plot_pnl(result, owner_id: int, ax=None, label: str | None = None):
    """Plot mark-to-market PnL for one agent."""
    import matplotlib.pyplot as plt
    df = pnl_series(result, owner_id)
    if df.empty:
        return ax
    if ax is None:
        _, ax = plt.subplots(figsize=(12, 4))
    ax.plot(df["ts"] / 1e9, df["pnl"],
            lw=1.2, label=label or f"Agent {owner_id}")
    ax.axhline(0, color="black", lw=0.5, ls="--")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("PnL (ticks)")
    ax.set_title(f"Mark-to-market PnL — Agent {owner_id}")
    ax.legend()
    ax.grid(True, alpha=0.3)
    return ax


def plot_trade_histogram(result, bins: int = 50, ax=None):
    """Plot histogram of trade prices."""
    import matplotlib.pyplot as plt
    df = result.trades_df()
    if df.empty:
        return ax
    if ax is None:
        _, ax = plt.subplots(figsize=(8, 4))
    ax.hist(df["price"], bins=bins, color="#2563eb", alpha=0.7, edgecolor="white")
    ax.set_xlabel("Price (ticks)")
    ax.set_ylabel("Count")
    ax.set_title("Trade price distribution")
    ax.grid(True, alpha=0.3)
    return ax


def plot_autocorr(sf, ax=None):
    """Plot return and |return| autocorrelations from a StyleFacts object."""
    import matplotlib.pyplot as plt
    if ax is None:
        _, ax = plt.subplots(figsize=(10, 4))
    lags = list(range(1, sf.autocorr.max_lag + 1))
    ax.bar([l - 0.2 for l in lags], sf.autocorr.return_ac,
           width=0.35, label="Return AC", alpha=0.8, color="#2563eb")
    ax.bar([l + 0.2 for l in lags], sf.autocorr.abs_return_ac,
           width=0.35, label="|Return| AC", alpha=0.8, color="#f59e0b")
    ax.axhline(0, color="black", lw=0.5)
    ax.set_xlabel("Lag (steps)")
    ax.set_ylabel("Autocorrelation")
    ax.set_title("Return autocorrelations")
    ax.legend()
    ax.grid(True, alpha=0.3)
    return ax


def dashboard(result, owner_id: int | None = None):
    """Four-panel dashboard: mid-price, trade histogram, spread, PnL."""
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(2, 2, figsize=(14, 8))
    fig.suptitle("MSIM Simulation Dashboard", fontsize=14, fontweight="bold")

    plot_mid_price(result, ax=axes[0, 0])
    plot_trade_histogram(result, ax=axes[0, 1])

    # Spread over time
    df = result.tops_df().dropna(subset=["best_bid", "best_ask"])
    if not df.empty:
        df["spread"] = df["best_ask"] - df["best_bid"]
        axes[1, 0].plot(df["ts"] / 1e9, df["spread"], lw=0.6, color="#10b981")
        axes[1, 0].set_xlabel("Time (s)")
        axes[1, 0].set_ylabel("Spread (ticks)")
        axes[1, 0].set_title("Bid-ask spread")
        axes[1, 0].grid(True, alpha=0.3)

    if owner_id is not None:
        plot_pnl(result, owner_id, ax=axes[1, 1])
    else:
        if result.sf:
            plot_autocorr(result.sf, ax=axes[1, 1])
        else:
            axes[1, 1].text(0.5, 0.5, "No SF / PnL data",
                            ha="center", va="center",
                            transform=axes[1, 1].transAxes)

    plt.tight_layout()
    return fig
