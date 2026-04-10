"""
MSIM Multi-Seed Robustness Study
=================================
Runs the same 9-agent simulation across N_SEEDS independent seeds.
Reports mean ± std for all five stylised facts, plus bootstrap 95% CIs.

Usage:
    python multiseed_study.py

Output:
    multiseed_results.csv   — per-seed raw statistics
    multiseed_summary.txt   — mean ± std table ready to paste into paper
    multiseed_summary.json  — machine-readable summary
"""

import msim
import json
import statistics
import random
import sys

# ── Configuration ──────────────────────────────────────────────────────────────
N_SEEDS     = 50       # number of independent seeds (use 50 for paper, 5 for quick test)
HORIZON     = 300.0    # seconds per run (same as validation run)
PRINT_EACH  = True     # print each seed's result as it completes
BOOTSTRAP_N = 2000     # bootstrap resamples for 95% CIs on the final means

# ── Agent factory (identical to run_analysis.py) ───────────────────────────────
def make_world():
    world = msim.World()
    world.prefill_book(mid=10000, levels=20, qty=10)

    for owner_id in range(1, 6):
        hawkes_cfg = msim.HawkesNoiseConfig()
        hawkes_cfg.p_market = 0.5
        hawkes_cfg.lot_size = 2
        world.add_agent(msim.agents.HawkesNoiseTrader(
            owner_id=owner_id, config=hawkes_cfg))

    world.add_agent(msim.agents.MarketMakerAS(owner_id=10))

    fv1 = msim.FundamentalValueConfig()
    fv1.threshold = 1
    fv1.sigma_v   = 0.5
    fv1.lot_size  = 2
    world.add_agent(msim.agents.FundamentalValueAgent(owner_id=20, config=fv1))

    fv2 = msim.FundamentalValueConfig()
    fv2.threshold = 2
    fv2.sigma_v   = 1.0
    fv2.lot_size  = 1
    world.add_agent(msim.agents.FundamentalValueAgent(owner_id=21, config=fv2))

    mom = msim.MomentumConfig()
    mom.entry_band = 1
    mom.lot_size   = 1
    world.add_agent(msim.agents.MomentumAgent(owner_id=30, config=mom))

    return world

# ── Run configuration ──────────────────────────────────────────────────────────
cfg = msim.WorldConfig()
cfg.dt_ns                  = 1_000_000
cfg.record_fills           = False
cfg.record_pnl_series      = False
cfg.compute_stylized_facts = True
cfg.track_queue_positions  = False

# ── Run all seeds ──────────────────────────────────────────────────────────────
print(f"Running {N_SEEDS} seeds × {HORIZON:.0f}s each...")
print(f"{'Seed':>6} {'n':>5} {'kurt':>7} {'ret_ac':>8} {'abs_ac':>8} "
      f"{'sign_ac':>8} {'spread':>8} {'lambda_r2':>10}")
print("─" * 75)

rows = []
failed = []

for i in range(N_SEEDS):
    seed = 1000 + i   # reproducible seeds: 1000, 1001, …, 1049
    try:
        world  = make_world()
        result = world.run(seed=seed, horizon=HORIZON, config=cfg)
        sf     = result.sf

        if not sf or sf.returns.n_obs < 50:
            failed.append(seed)
            continue

        row = {
            "seed":     seed,
            "n_obs":    sf.returns.n_obs,
            "kurtosis": sf.returns.excess_kurtosis,
            "ret_ac":   sf.autocorr.return_ac[0]     if sf.autocorr.return_ac     else float("nan"),
            "abs_ac":   sf.autocorr.abs_return_ac[0] if sf.autocorr.abs_return_ac else float("nan"),
            "sign_ac":  sf.autocorr.sign_flow_ac[0]  if sf.autocorr.sign_flow_ac  else float("nan"),
            "spread":   sf.spreads.time_weighted_spread,
            "lambda_r2": sf.impact.r_squared,
            "fat_tails":  sf.fat_tails_ok,
            "vol_clust":  sf.vol_clustering_ok,
            "flow_ac":    sf.flow_autocorr_ok,
            "pos_spread": sf.positive_spread_ok,
            "pos_impact": sf.positive_impact_ok,
        }
        rows.append(row)

        if PRINT_EACH:
            print(f"{seed:>6} {row['n_obs']:>5} {row['kurtosis']:>7.2f} "
                  f"{row['ret_ac']:>8.3f} {row['abs_ac']:>8.3f} "
                  f"{row['sign_ac']:>8.3f} {row['spread']:>8.2f} "
                  f"{row['lambda_r2']:>10.4f}")

    except Exception as e:
        failed.append(seed)
        print(f"  Seed {seed} FAILED: {e}", file=sys.stderr)

if not rows:
    print("No successful runs. Exiting.")
    sys.exit(1)

print(f"\nCompleted: {len(rows)}/{N_SEEDS} runs")
if failed:
    print(f"Failed seeds: {failed}")

# ── Bootstrap CI function ──────────────────────────────────────────────────────
def bootstrap_ci(data, n=BOOTSTRAP_N, alpha=0.05):
    """Return (mean, lower_95, upper_95) via percentile bootstrap."""
    data = [x for x in data if x == x]  # remove NaN
    if len(data) < 2:
        return float("nan"), float("nan"), float("nan")
    means = sorted(
        statistics.mean(random.choices(data, k=len(data)))
        for _ in range(n)
    )
    lo = means[int(alpha/2 * n)]
    hi = means[int((1-alpha/2) * n)]
    return statistics.mean(data), lo, hi

# ── Summary statistics ─────────────────────────────────────────────────────────
metrics = [
    ("kurtosis",  "Excess kurtosis",       "3–10 (Cont 2001)"),
    ("ret_ac",    "Return AC lag-1",        "negative (Roll 1984)"),
    ("abs_ac",    "|Return| AC lag-1",      "0.10–0.40 (Engle 1982)"),
    ("sign_ac",   "Trade-sign AC lag-1",    "0.30–0.70 (Bouchaud 2004)"),
    ("spread",    "Time-weighted spread",   "positive"),
    ("lambda_r2", "Kyle λ  R²",            "n/a (see discussion)"),
]

pass_rates = {
    "Fat tails":       sum(r["fat_tails"]  for r in rows) / len(rows),
    "Vol clustering":  sum(r["vol_clust"]  for r in rows) / len(rows),
    "Flow AC":         sum(r["flow_ac"]    for r in rows) / len(rows),
    "Positive spread": sum(r["pos_spread"] for r in rows) / len(rows),
    "Nonzero impact":  sum(r["pos_impact"] for r in rows) / len(rows),
}

print()
print("═" * 72)
print(f"  MULTI-SEED ROBUSTNESS SUMMARY  ({len(rows)} seeds, {HORIZON:.0f}s each)")
print("═" * 72)
print(f"\n  {'Statistic':<28} {'Mean':>8} {'Std':>8} {'95% CI':>18}  {'Lit. range'}")
print("  " + "─" * 70)

summary = {}
for key, label, lit in metrics:
    vals = [r[key] for r in rows]
    mean, lo, hi = bootstrap_ci(vals)
    std = statistics.stdev([v for v in vals if v==v]) if len(rows)>1 else 0
    ci_str = f"[{lo:.3f}, {hi:.3f}]"
    print(f"  {label:<28} {mean:>8.3f} {std:>8.3f} {ci_str:>18}  {lit}")
    summary[key] = {"mean": round(mean,4), "std": round(std,4),
                    "ci_lo": round(lo,4), "ci_hi": round(hi,4)}

print()
print("  PASS RATES ACROSS SEEDS:")
for name, rate in pass_rates.items():
    bar = "█" * int(rate * 20)
    print(f"  {name:<20} {rate*100:5.1f}%  {bar}")

print()
mean_n = statistics.mean(r["n_obs"] for r in rows)
print(f"  Mean trades per run: {mean_n:.0f}")
print(f"  Total observations:  {sum(r['n_obs'] for r in rows):,}")

# ── Save outputs ───────────────────────────────────────────────────────────────
import csv, pathlib

out_csv = "multiseed_results.csv"
with open(out_csv, "w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=rows[0].keys())
    writer.writeheader()
    writer.writerows(rows)
print(f"\n  Raw data saved to: {out_csv}")

out_json = "multiseed_summary.json"
with open(out_json, "w") as f:
    json.dump({"n_seeds": len(rows), "horizon": HORIZON,
               "summary": summary, "pass_rates": pass_rates}, f, indent=2)
print(f"  Summary saved to: {out_json}")

# ── LaTeX table snippet ────────────────────────────────────────────────────────
print("\n  LATEX TABLE SNIPPET (paste into paper):")
print("  " + "─" * 60)
latex_lines = [
    r"  \begin{tabular}{lrrrr}",
    r"  \toprule",
    r"  Statistic & Mean & Std & 95\,\% CI & Literature \\",
    r"  \midrule",
]
for key, label, lit in metrics:
    d = summary[key]
    latex_lines.append(
        f"  {label} & {d['mean']:.3f} & {d['std']:.3f} & "
        f"[{d['ci_lo']:.3f},\\,{d['ci_hi']:.3f}] & {lit} \\\\"
    )
latex_lines += [r"  \bottomrule", r"  \end{tabular}"]
print("\n".join(latex_lines))
