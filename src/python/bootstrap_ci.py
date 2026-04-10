"""
MSIM Bootstrap Confidence Interval Study
==========================================
Runs the single validated simulation (seed=42, 9 agents, 300s)
and computes 2000-resample bootstrap 95% CIs for all stylised fact statistics.

This is a standard approach when multi-seed runs are computationally expensive
or when a single long run provides sufficient observations. The 464 trade-to-trade
returns from this run are resampled with replacement to estimate sampling uncertainty.

Usage:
    python bootstrap_ci.py

Output:
    bootstrap_results.json  — full CI data
    (LaTeX table snippet printed to console)
"""

import msim, json, random, statistics, math

SEED        = 42
HORIZON     = 300.0
N_BOOTSTRAP = 2000
RNG_SEED    = 999    # reproducible bootstrap

print(f"Step 1: Running simulation (seed={SEED}, horizon={HORIZON:.0f}s, 9 agents)...")

# ── Simulation — exact same setup as run_analysis.py ─────────────────────────
world = msim.World()
world.prefill_book(mid=10000, levels=20, qty=10)

for owner_id in range(1, 6):
    h = msim.HawkesNoiseConfig()
    h.p_market = 0.5
    h.lot_size = 2
    world.add_agent(msim.agents.HawkesNoiseTrader(owner_id=owner_id, config=h))

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

cfg = msim.WorldConfig()
cfg.dt_ns                  = 1_000_000
cfg.record_fills           = False
cfg.record_pnl_series      = False
cfg.compute_stylized_facts = True
cfg.track_queue_positions  = False

result = world.run(seed=SEED, horizon=HORIZON, config=cfg)
sf     = result.sf

if not sf:
    print("ERROR: no stylised facts computed"); exit(1)

n   = sf.returns.n_obs
ac  = sf.autocorr
print(f"  Trades: {len(result.trades)},  n_obs = {n}")
print(f"  Kurtosis: {sf.returns.excess_kurtosis:.3f}")
print(f"  Spread:   {sf.spreads.time_weighted_spread:.3f} ticks")

# ── Extract raw return series for bootstrapping ───────────────────────────────
# We reconstruct log-returns from trade prices
trades = result.trades
if len(trades) < 2:
    print("ERROR: too few trades"); exit(1)

import math
prices  = [t.price for t in trades]
returns = [math.log(prices[i] / prices[i-1]) for i in range(1, len(prices))]

# Tick rule: sign of price change as trade-direction proxy (Lee & Ready 1991).
# Unchanged price carries forward the previous sign.
signs, last = [], 1
for i in range(1, len(prices)):
    dp = prices[i] - prices[i-1]
    if dp > 0:   last = 1
    elif dp < 0: last = -1
    signs.append(last)

print(f"\nStep 2: Bootstrap resampling ({N_BOOTSTRAP} resamples)...")

rng = random.Random(RNG_SEED)

def sample_stats(ret_sample, sgn_sample):
    """Compute all statistics from a return/sign sample."""
    n = len(ret_sample)
    if n < 10:
        return None
    mean_r = statistics.mean(ret_sample)
    var_r  = statistics.variance(ret_sample)
    if var_r == 0:
        return None
    std_r  = math.sqrt(var_r)

    # kurtosis
    kurt = (sum((r - mean_r)**4 for r in ret_sample) / n) / (var_r**2) - 3

    # autocorrelations at lag 1
    def ac1(series):
        mu = statistics.mean(series)
        num = sum((series[i] - mu) * (series[i-1] - mu) for i in range(1, len(series)))
        den = sum((x - mu)**2 for x in series)
        return num / den if den != 0 else float("nan")

    ret_ac  = ac1(ret_sample)
    abs_ac  = ac1([abs(r) for r in ret_sample])
    sign_ac = ac1(sgn_sample)

    return {"kurtosis": kurt, "ret_ac": ret_ac, "abs_ac": abs_ac, "sign_ac": sign_ac}

# Observed statistics
obs = sample_stats(returns, signs)
if not obs:
    print("ERROR: could not compute observed statistics"); exit(1)

# Bootstrap
boot_stats = {"kurtosis": [], "ret_ac": [], "abs_ac": [], "sign_ac": []}
paired = list(zip(returns, signs))

for b in range(N_BOOTSTRAP):
    if b % 500 == 0:
        print(f"  {b}/{N_BOOTSTRAP}...")
    sample = rng.choices(paired, k=len(paired))
    ret_s, sgn_s = zip(*sample)
    s = sample_stats(list(ret_s), list(sgn_s))
    if s:
        for k in boot_stats:
            boot_stats[k].append(s[k])

print(f"  {N_BOOTSTRAP}/{N_BOOTSTRAP} done.")

def ci(vals, alpha=0.05):
    v = sorted(v for v in vals if not math.isnan(v))
    if not v:
        return float("nan"), float("nan")
    return v[int(alpha/2 * len(v))], v[int((1-alpha/2) * len(v))]

# ── Summary table ─────────────────────────────────────────────────────────────
print()
print("=" * 72)
print(f"  BOOTSTRAP CI RESULTS  (n={n}, {N_BOOTSTRAP} resamples, seed={SEED})")
print("=" * 72)
print(f"  {'Statistic':<28} {'Observed':>10} {'95% CI':>22}  Literature")
print("  " + "─" * 68)

rows = [
    ("kurtosis", "Excess kurtosis",     "3–10 (Cont 2001)"),
    ("ret_ac",   "Return AC lag-1",     "negative (Roll 1984)"),
    ("abs_ac",   "|Return| AC lag-1",   "0.10–0.40 (Engle 1982)"),
    ("sign_ac",  "Trade-sign AC lag-1", "0.30–0.70 (Bouchaud 2004)"),
]

# Add spread and lambda_r2 from sf directly (no bootstrap — they depend on timing)
extra = {
    "spread":    (sf.spreads.time_weighted_spread,
                  sf.spreads.effective_spread_mean, "positive"),
    "lambda_r2": (sf.impact.r_squared, None, "n/a (see discussion)"),
}

summary = {}
for key, label, lit in rows:
    obs_val = obs[key]
    lo, hi  = ci(boot_stats[key])
    ci_str  = f"[{lo:.3f},\\ {hi:.3f}]"
    print(f"  {label:<28} {obs_val:>10.3f} {ci_str:>22}  {lit}")
    summary[key] = {"observed": round(obs_val, 4),
                    "ci_lo": round(lo, 4), "ci_hi": round(hi, 4)}

# Spread and impact (point estimates only)
print(f"  {'TW spread (ticks)':<28} {sf.spreads.time_weighted_spread:>10.2f}"
      f"  {'n/a':>22}  positive")
print(f"  {'Kyle lambda R2':<28} {sf.impact.r_squared:>10.4f}"
      f"  {'n/a':>22}  n/a — see discussion")

print()
print("  VALIDATION PASS RATES (single run):")
checks = [
    ("Fat tails (kurtosis>1)", sf.fat_tails_ok),
    ("Vol clustering (|r|AC>0.05)", sf.vol_clustering_ok),
    ("Flow AC (sign AC>0.10)", sf.flow_autocorr_ok),
    ("Positive spread", sf.positive_spread_ok),
]
for name, ok in checks:
    print(f"  {'✓' if ok else '✗'}  {name}")

# ── Save output ───────────────────────────────────────────────────────────────
out = {
    "seed": SEED, "n_obs": n, "n_bootstrap": N_BOOTSTRAP,
    "observed": {k: obs[k] for k in ["kurtosis","ret_ac","abs_ac","sign_ac"]},
    "spread": sf.spreads.time_weighted_spread,
    "lambda_r2": sf.impact.r_squared,
    "bootstrap_ci": summary,
}
with open("bootstrap_results.json", "w") as f:
    json.dump(out, f, indent=2)
print("\n  Saved: bootstrap_results.json")

# ── LaTeX snippet ─────────────────────────────────────────────────────────────
print()
print("  LATEX TABLE SNIPPET:")
print(r"  \begin{tabular}{lrrr}")
print(r"  \toprule")
print(r"  Fact & Observed & 95\,\% Bootstrap CI & Literature \\")
print(r"  \midrule")
for key, label, lit in rows:
    d = summary[key]
    print(f"  {label} & {d['observed']:.3f} & "
          f"$[{d['ci_lo']:.3f},\\,{d['ci_hi']:.3f}]$ & {lit} \\\\")
print(f"  Time-weighted spread & {sf.spreads.time_weighted_spread:.2f}\\,ticks"
      r" & \multicolumn{1}{c}{---} & positive \\")
print(f"  Kyle $\\lambda$ $R^2$ & {sf.impact.r_squared:.4f}"
      r" & \multicolumn{1}{c}{---} & n/a \\")
print(r"  \bottomrule")
print(r"  \end{tabular}")
