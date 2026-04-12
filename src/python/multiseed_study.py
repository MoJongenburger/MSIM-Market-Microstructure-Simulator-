"""
MSIM Multi-Seed Robustness Study
Runs 50 seeds via subprocess isolation and reports stylised facts statistics.
Usage: python multiseed_study.py
"""
import subprocess, sys, json, csv, statistics, random, pathlib, math

N_SEEDS     = 50
BOOTSTRAP_N = 2000
WORKER      = pathlib.Path(__file__).parent / "run_one_seed.py"

def bootstrap_ci(data, n=BOOTSTRAP_N, alpha=0.05):
    data = [x for x in data if x is not None and not math.isnan(x)]
    if len(data) < 2:
        return float("nan"), float("nan"), float("nan")
    rng = random.Random(999)
    means = sorted(statistics.mean(rng.choices(data, k=len(data))) for _ in range(n))
    return (statistics.mean(data),
            means[int(alpha / 2 * n)],
            means[int((1 - alpha / 2) * n)])

print(f"Running {N_SEEDS} seeds × 300s  (9 agents, subprocess-isolated)")
print(f"{'Seed':>6} {'n':>6} {'kurt':>8} {'ret_ac':>8} "
      f"{'abs_ac':>8} {'sign_ac':>8} {'spread':>8} {'R2':>7}")
print("─" * 72)

rows, failed = [], []

for i in range(N_SEEDS):
    seed = 1000 + i
    try:
        proc = subprocess.run(
            [sys.executable, str(WORKER), str(seed)],
            capture_output=True, text=True, timeout=120
        )
        stdout = proc.stdout.strip()
        if proc.returncode != 0 or not stdout:
            failed.append(seed)
            err = proc.stderr.strip()[:80] if proc.stderr else ""
            print(f"{seed:>6}  ERROR (rc={proc.returncode}): {err}")
            continue

        # Take only the last non-empty line (guards against any C++ warnings)
        last_line = [l for l in stdout.splitlines() if l.strip()][-1]
        d = json.loads(last_line)

        if not d.get("ok"):
            failed.append(seed)
            print(f"{seed:>6}  SKIP (n_obs={d.get('n_obs', 0)})")
            continue

        rows.append(d)
        kurt = d["kurtosis"]
        print(f"{seed:>6} {d['n_obs']:>6} {kurt:>8.2f} "
              f"{d['ret_ac']:>8.3f} {d['abs_ac']:>8.3f} "
              f"{d['sign_ac']:>8.3f} {d['spread']:>8.2f} "
              f"{d['lambda_r2']:>7.4f}")

    except subprocess.TimeoutExpired:
        failed.append(seed)
        print(f"{seed:>6}  TIMEOUT")
    except Exception as e:
        failed.append(seed)
        print(f"{seed:>6}  EXCEPTION: {e}")

if not rows:
    print("No successful runs."); sys.exit(1)

print(f"\nCompleted: {len(rows)}/{N_SEEDS}")
if failed:
    print(f"Skipped/failed: {failed}")

metrics = [
    ("kurtosis",  "Excess kurtosis",     "3–10 daily; >3 tick-level (Cont 2001)"),
    ("ret_ac",    "Return AC lag-1",     "negative (Roll 1984)"),
    ("abs_ac",    "|Return| AC lag-1",   "0.10–0.40 (Engle 1982)"),
    ("sign_ac",   "Trade-sign AC lag-1", "0.30–0.70 (Bouchaud 2004)"),
    ("spread",    "TW spread (ticks)",   "positive"),
    ("lambda_r2", "Kyle lambda R2",      "n/a — see discussion"),
]

pass_rates = {
    "Fat tails (kurtosis>3)": sum(1 for r in rows if r["kurtosis"] > 3) / len(rows),
    "Vol clustering":         sum(r["vol_clust"]  for r in rows) / len(rows),
    "Flow AC":                sum(r["flow_ac"]    for r in rows) / len(rows),
    "Pos spread":             sum(r["pos_spread"] for r in rows) / len(rows),
}

# Compute median kurtosis
kurtosis_vals = sorted(r["kurtosis"] for r in rows if not math.isnan(r["kurtosis"]))
median_kurt = statistics.median(kurtosis_vals)

print()
print("=" * 70)
print(f"  ROBUSTNESS SUMMARY — {len(rows)} seeds, 300s each, "
      f"mean n_obs={statistics.mean(r['n_obs'] for r in rows):.0f}")
print("=" * 70)
print(f"  {'Statistic':<24} {'Mean':>8} {'Median':>8} {'Std':>7} {'95% CI':>20}  Literature")
print("  " + "─" * 72)

summary = {}
for key, label, lit in metrics:
    vals         = [r[key] for r in rows if r[key] is not None]
    mean, lo, hi = bootstrap_ci(vals)
    med          = statistics.median(vals) if vals else float("nan")
    vals_clean   = [v for v in vals if not math.isnan(v)]
    std          = statistics.stdev(vals_clean) if len(vals_clean) > 1 else 0
    summary[key] = {"mean": round(mean,4), "median": round(med,4),
                    "std": round(std,4), "ci_lo": round(lo,4), "ci_hi": round(hi,4)}
    print(f"  {label:<24} {mean:>8.3f} {med:>8.3f} {std:>7.3f} "
          f"[{lo:.3f}, {hi:.3f}]  {lit}")

print()
print(f"  Kurtosis median: {median_kurt:.2f}  "
      f"(seeds with kurtosis in [3,10]: {sum(1 for k in kurtosis_vals if 3<=k<=10)}/{len(kurtosis_vals)})")
print()
print("  PASS RATES ACROSS SEEDS:")
for name, rate in pass_rates.items():
    n = int(rate * len(rows))
    print(f"  {name:<28} {n:>2}/{len(rows)}  ({rate*100:.1f}%)  "
          f"{'█'*int(rate*20)}")

with open("multiseed_results.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=rows[0].keys())
    w.writeheader(); w.writerows(rows)

with open("multiseed_summary.json", "w") as f:
    import json as j
    j.dump({"n_seeds": len(rows), "summary": summary,
             "pass_rates": pass_rates, "kurtosis_median": median_kurt}, f, indent=2)

print()
print("  LATEX TABLE SNIPPET:")
print(r"  \begin{tabular}{lrrrr}")
print(r"  \toprule")
print(r"  Fact & Mean & Median & Std & Literature \\")
print(r"  \midrule")
for key, label, lit in metrics[:4]:
    d = summary[key]
    print(f"  {label} & {d['mean']:.3f} & {d['median']:.3f} & {d['std']:.3f} & {lit} \\\\")
print(r"  \bottomrule")
print(r"  \end{tabular}")
print("\n  Saved: multiseed_results.csv  multiseed_summary.json")
