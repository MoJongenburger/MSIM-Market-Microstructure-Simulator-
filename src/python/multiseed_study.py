"""
MSIM Multi-Seed Robustness Study
=================================
Runs the same 6-agent simulation (5 Hawkes noise traders + A-S market maker)
across N_SEEDS independent seeds and reports mean ± std with 95% bootstrap CIs
for all five stylised facts.

This configuration generates ~3,600 trades per 300s run, giving reliable
per-seed estimates. Results are saved to multiseed_results.csv and
multiseed_summary.json.

Usage:
    python multiseed_study.py
"""

import msim, json, csv, statistics, random, sys, pathlib

# ── Configuration ──────────────────────────────────────────────────────────────
N_SEEDS     = 50
HORIZON     = 300.0
BOOTSTRAP_N = 2000

# ── Agent factory: 5 Hawkes noise traders + Avellaneda-Stoikov market maker ───
def make_world():
    world = msim.World()
    world.prefill_book(mid=10000, levels=20, qty=10)
    for owner_id in range(1, 6):
        h = msim.HawkesNoiseConfig()
        h.p_market = 0.5
        h.lot_size = 2
        world.add_agent(msim.agents.HawkesNoiseTrader(
            owner_id=owner_id, config=h))
    world.add_agent(msim.agents.MarketMakerAS(owner_id=10))
    return world

# ── Run configuration ──────────────────────────────────────────────────────────
def make_cfg():
    cfg = msim.WorldConfig()
    cfg.dt_ns                  = 1_000_000
    cfg.record_fills           = False
    cfg.record_pnl_series      = False
    cfg.compute_stylized_facts = True
    cfg.track_queue_positions  = False
    return cfg

# ── Bootstrap CI ───────────────────────────────────────────────────────────────
def bootstrap_ci(data, n=BOOTSTRAP_N, alpha=0.05):
    data = [x for x in data if x == x]
    if len(data) < 2:
        return float("nan"), float("nan"), float("nan")
    rng = random.Random(999)
    means = sorted(
        statistics.mean(rng.choices(data, k=len(data)))
        for _ in range(n)
    )
    return (statistics.mean(data),
            means[int(alpha / 2 * n)],
            means[int((1 - alpha / 2) * n)])

# ── Main loop ──────────────────────────────────────────────────────────────────
print(f"Running {N_SEEDS} seeds × {HORIZON:.0f}s  (5 Hawkes + A-S MM)")
print(f"{'Seed':>6} {'n':>6} {'kurt':>7} {'ret_ac':>8} "
      f"{'abs_ac':>8} {'sign_ac':>8} {'spread':>8} {'lambda_r2':>10}")
print("─" * 77)

rows, failed = [], []

for i in range(N_SEEDS):
    seed = 1000 + i
    try:
        world  = make_world()
        result = world.run(seed=seed, horizon=HORIZON, config=make_cfg())
        sf     = result.sf

        if not sf or sf.returns.n_obs < 100:
            failed.append(seed)
            print(f"{seed:>6}  SKIP (n={sf.returns.n_obs if sf else 0})")
            continue

        ac  = sf.autocorr
        row = {
            "seed":      seed,
            "n_obs":     sf.returns.n_obs,
            "kurtosis":  sf.returns.excess_kurtosis,
            "ret_ac":    ac.return_ac[0]     if ac.return_ac     else float("nan"),
            "abs_ac":    ac.abs_return_ac[0] if ac.abs_return_ac else float("nan"),
            "sign_ac":   ac.sign_flow_ac[0]  if ac.sign_flow_ac  else float("nan"),
            "spread":    sf.spreads.time_weighted_spread,
            "lambda_r2": sf.impact.r_squared,
            "fat_tails": sf.fat_tails_ok,
            "vol_clust": sf.vol_clustering_ok,
            "flow_ac":   sf.flow_autocorr_ok,
            "pos_spread":sf.positive_spread_ok,
        }
        rows.append(row)
        print(f"{seed:>6} {row['n_obs']:>6} {row['kurtosis']:>7.2f} "
              f"{row['ret_ac']:>8.3f} {row['abs_ac']:>8.3f} "
              f"{row['sign_ac']:>8.3f} {row['spread']:>8.2f} "
              f"{row['lambda_r2']:>10.4f}")

    except Exception as e:
        failed.append(seed)
        print(f"{seed:>6}  ERROR: {e}", file=sys.stderr)

# ── Summary ────────────────────────────────────────────────────────────────────
if not rows:
    print("No successful runs."); sys.exit(1)

print(f"\nCompleted: {len(rows)}/{N_SEEDS}")
if failed:
    print(f"Skipped/failed: {failed}")

metrics = [
    ("kurtosis",   "Excess kurtosis",     "3–10 (Cont 2001)"),
    ("ret_ac",     "Return AC lag-1",     "negative (Roll 1984)"),
    ("abs_ac",     "|Return| AC lag-1",   "0.10–0.40 (Engle 1982)"),
    ("sign_ac",    "Trade-sign AC lag-1", "0.30–0.70 (Bouchaud 2004)"),
    ("spread",     "TW spread (ticks)",   "positive"),
    ("lambda_r2",  "Kyle lambda R2",      "n/a (see discussion)"),
]

pass_rates = {
    "Fat tails (kurtosis>1)": sum(r["fat_tails"]  for r in rows) / len(rows),
    "Vol clustering (AC>0.05)": sum(r["vol_clust"] for r in rows) / len(rows),
    "Flow AC (AC>0.10)":      sum(r["flow_ac"]    for r in rows) / len(rows),
    "Positive spread":        sum(r["pos_spread"]  for r in rows) / len(rows),
}

print()
print("=" * 72)
print(f"  ROBUSTNESS SUMMARY — {len(rows)} seeds, {HORIZON:.0f}s each, "
      f"mean n_obs = {statistics.mean(r['n_obs'] for r in rows):.0f}")
print("=" * 72)
print(f"  {'Statistic':<24} {'Mean':>8} {'Std':>7} "
      f"{'95% CI':>20}  Literature")
print("  " + "─" * 68)

summary = {}
for key, label, lit in metrics:
    vals  = [r[key] for r in rows]
    mean, lo, hi = bootstrap_ci(vals)
    std   = statistics.stdev([v for v in vals if v == v]) if len(rows) > 1 else 0
    ci    = f"[{lo:.3f}, {hi:.3f}]"
    print(f"  {label:<24} {mean:>8.3f} {std:>7.3f} {ci:>20}  {lit}")
    summary[key] = {"mean": round(mean, 4), "std": round(std, 4),
                    "ci_lo": round(lo, 4),  "ci_hi": round(hi, 4)}

print()
print("  PASS RATES:")
for name, rate in pass_rates.items():
    bar = "█" * int(rate * 20)
    n   = int(rate * len(rows))
    print(f"  {name:<30} {n:>2}/{len(rows)}  {rate*100:5.1f}%  {bar}")

# ── Save outputs ───────────────────────────────────────────────────────────────
pathlib.Path("multiseed_results.csv").write_text(
    "\n".join([",".join(str(rows[0].keys()).strip("dict_keys(['").rstrip("'])").split("', '"))]
              + [",".join(str(v) for v in r.values()) for r in rows]))

with open("multiseed_results.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=rows[0].keys())
    w.writeheader(); w.writerows(rows)

with open("multiseed_summary.json", "w") as f:
    json.dump({"n_seeds": len(rows), "horizon": HORIZON,
               "summary": summary, "pass_rates": pass_rates}, f, indent=2)

# ── LaTeX snippet ──────────────────────────────────────────────────────────────
print()
print("  LATEX SNIPPET:")
print(r"  \begin{tabular}{lrrrr}")
print(r"  \toprule")
print(r"  Fact & Mean & Std & 95\,\% CI & Literature \\")
print(r"  \midrule")
for key, label, lit in metrics:
    d = summary[key]
    print(f"  {label} & {d['mean']:.3f} & {d['std']:.3f} & "
          f"[{d['ci_lo']:.3f},\\,{d['ci_hi']:.3f}] & {lit} \\\\")
print(r"  \bottomrule")
print(r"  \end{tabular}")
print()
print("  Files: multiseed_results.csv  multiseed_summary.json")
