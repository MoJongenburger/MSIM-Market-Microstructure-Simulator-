"""
MSIM Bootstrap Confidence Interval Study
=========================================
Runs the single validated simulation (seed=42, 9 agents, 300s)
with calibrated parameters and computes proper uncertainty estimates:

  - Excess kurtosis: 2000-resample i.i.d. bootstrap (valid for
    distributional statistics that don't depend on ordering)
  - Autocorrelations: asymptotic SE = 1/sqrt(n) (correct for time
    series; i.i.d. bootstrap is invalid for AC because resampling
    destroys the serial dependence structure)

Usage:
    python bootstrap_ci.py
"""
import msim, json, random, statistics, math

SEED        = 42
HORIZON     = 300.0
N_BOOTSTRAP = 2000
RNG_SEED    = 999

print(f"Step 1: Running simulation (seed={SEED}, horizon={HORIZON:.0f}s)...")

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
fv1.sigma_v   = 0.3
fv1.lot_size  = 2
world.add_agent(msim.agents.FundamentalValueAgent(owner_id=20, config=fv1))

fv2 = msim.FundamentalValueConfig()
fv2.threshold = 3
fv2.sigma_v   = 0.2
fv2.lot_size  = 1
world.add_agent(msim.agents.FundamentalValueAgent(owner_id=21, config=fv2))

mom = msim.MomentumConfig()
mom.entry_band = 2
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

# ── Extract return series for bootstrap ──────────────────────────────────────
trades = result.trades
prices = [t.price for t in trades]
returns = [math.log(prices[i]/prices[i-1]) for i in range(1, len(prices))]

# Use true aggressor_side from the C++ engine (not tick rule)
signs = [1 if t.aggressor_side.name == 'Buy' else -1 for t in trades[1:]]

print(f"\nStep 2: Bootstrap kurtosis CI ({N_BOOTSTRAP} resamples)...")

rng = random.Random(RNG_SEED)

def kurtosis(data):
    n = len(data)
    if n < 4: return float('nan')
    mu  = statistics.mean(data)
    var = statistics.variance(data)
    if var == 0: return float('nan')
    return (sum((x-mu)**4 for x in data)/n) / (var**2) - 3

boot_kurt = []
for b in range(N_BOOTSTRAP):
    if b % 500 == 0: print(f"  {b}/{N_BOOTSTRAP}...")
    sample = rng.choices(returns, k=len(returns))
    k = kurtosis(sample)
    if not math.isnan(k):
        boot_kurt.append(k)

boot_kurt.sort()
kurt_lo = boot_kurt[int(0.025 * len(boot_kurt))]
kurt_hi = boot_kurt[int(0.975 * len(boot_kurt))]
print(f"  {N_BOOTSTRAP}/{N_BOOTSTRAP} done.")

# ── Asymptotic SEs for autocorrelations ──────────────────────────────────────
# SE = 1/sqrt(n) is standard for autocorrelation estimators under weak
# stationarity. i.i.d. bootstrap is NOT valid for AC because resampling
# destroys serial dependence, centering the bootstrap distribution at 0.
se  = 1.0 / math.sqrt(n)
z95 = 1.96

obs_kurt    = sf.returns.excess_kurtosis
obs_ret_ac  = ac.return_ac[0]     if ac.return_ac     else float('nan')
obs_abs_ac  = ac.abs_return_ac[0] if ac.abs_return_ac else float('nan')
obs_sign_ac = ac.sign_flow_ac[0]  if ac.sign_flow_ac  else float('nan')

print()
print("=" * 72)
print(f"  UNCERTAINTY ESTIMATES  (n={n}, seed={SEED})")
print("=" * 72)
print(f"\n  {'Statistic':<28} {'Observed':>10} {'95% CI':>22}  Method")
print("  " + "─" * 68)

rows = [
    ("Excess kurtosis",     obs_kurt,    kurt_lo,                   kurt_hi,
     "bootstrap (i.i.d., valid for distributional stats)"),
    ("Return AC lag-1",     obs_ret_ac,  obs_ret_ac - z95*se,       obs_ret_ac + z95*se,
     "asymptotic SE = 1/sqrt(n)"),
    ("|Return| AC lag-1",   obs_abs_ac,  obs_abs_ac - z95*se,       obs_abs_ac + z95*se,
     "asymptotic SE = 1/sqrt(n)"),
    ("Trade-sign AC lag-1", obs_sign_ac, obs_sign_ac - z95*se,      obs_sign_ac + z95*se,
     "asymptotic SE = 1/sqrt(n)"),
]

for label, obs, lo, hi, method in rows:
    ci = f"[{lo:.3f}, {hi:.3f}]"
    print(f"  {label:<28} {obs:>10.3f} {ci:>22}  {method}")

print(f"\n  TW spread:  {sf.spreads.time_weighted_spread:.2f} ticks (no CI — point estimate)")
print(f"  Kyle λ R²:  {sf.impact.r_squared:.4f}          (no CI — near zero, unreliable)")

# ── LaTeX ─────────────────────────────────────────────────────────────────────
print("\n  LATEX TABLE:")
print(r"  \begin{tabular}{lrrrl}")
print(r"  \toprule")
print(r"  Fact & Observed & 95\,\% CI & Literature & CI method \\")
print(r"  \midrule")
for label, obs, lo, hi, _ in rows:
    print(f"  {label} & {obs:.3f} & $[{lo:.3f},\\,{hi:.3f}]$ & --- & --- \\\\")
print(r"  \bottomrule")
print(r"  \end{tabular}")

out = {
    "seed": SEED, "n_obs": n, "n_bootstrap": N_BOOTSTRAP,
    "kurtosis":  {"obs": obs_kurt,    "ci_lo": kurt_lo,             "ci_hi": kurt_hi,             "method": "bootstrap"},
    "ret_ac":    {"obs": obs_ret_ac,  "ci_lo": obs_ret_ac-z95*se,   "ci_hi": obs_ret_ac+z95*se,   "method": "asymptotic"},
    "abs_ac":    {"obs": obs_abs_ac,  "ci_lo": obs_abs_ac-z95*se,   "ci_hi": obs_abs_ac+z95*se,   "method": "asymptotic"},
    "sign_ac":   {"obs": obs_sign_ac, "ci_lo": obs_sign_ac-z95*se,  "ci_hi": obs_sign_ac+z95*se,  "method": "asymptotic"},
    "spread":    sf.spreads.time_weighted_spread,
    "lambda_r2": sf.impact.r_squared,
    "se": se,
}
with open("bootstrap_results.json", "w") as f:
    json.dump(out, f, indent=2)
print("\n  Saved: bootstrap_results.json")
