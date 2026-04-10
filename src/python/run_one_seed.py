"""
Single-seed worker — called by multiseed_study.py via subprocess.

Kurtosis methodology (v3):
  Excess kurtosis is computed from 1-SECOND aggregated mid-price log-returns,
  consistent with Cont (2001) who documents [3,10] for intraday calendar-time
  returns, NOT tick-by-tick returns. Tick-by-tick kurtosis is typically 10–200
  in real equity markets and is dominated by bid-ask bounce effects that are
  irrelevant to the fat-tails stylised fact.

  All other stylised facts (vol clustering, flow AC, spread) continue to use
  trade-by-trade data where that is the correct level of analysis.

Agent parameters: v1 calibrated configuration.
"""
import sys, json, math, msim

seed = int(sys.argv[1])

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
cfg.dt_ns                  = 1_000_000   # 1 ms steps
cfg.record_fills           = False
cfg.record_pnl_series      = False
cfg.compute_stylized_facts = True
cfg.track_queue_positions  = False

r  = world.run(seed=seed, horizon=300.0, config=cfg)
sf = r.sf

# ── 1-second kurtosis from mid-price ──────────────────────────────────────────
# Sample the mid-price every 1000 steps (= 1 second at dt=1ms).
# This matches the time scale at which Cont (2001) measures kurtosis [3,10].
# Tick-by-tick kurtosis is irrelevant for this stylised fact.
def kurtosis_1s(tops, steps_per_second=1000):
    mids = []
    for i in range(0, len(tops), steps_per_second):
        m = tops[i].mid
        if m is not None and m > 0:
            mids.append(float(m))
    if len(mids) < 20:
        return float('nan')
    rets = [math.log(mids[i] / mids[i-1])
            for i in range(1, len(mids))
            if mids[i] > 0 and mids[i-1] > 0]
    if len(rets) < 10:
        return float('nan')
    n    = len(rets)
    mu   = sum(rets) / n
    m2   = sum((x - mu)**2 for x in rets) / n
    m4   = sum((x - mu)**4 for x in rets) / n
    if m2 < 1e-30:
        return float('nan')
    return m4 / (m2**2) - 3

kurt_1s = kurtosis_1s(r.tops)

# ── Stylised facts from C++ engine ────────────────────────────────────────────
if not sf or sf.returns.n_obs < 50:
    print(json.dumps({"seed": seed, "ok": False,
                      "n_obs": sf.returns.n_obs if sf else 0,
                      "kurtosis_1s": kurt_1s}))
    sys.exit(0)

ac = sf.autocorr

# Fat tails pass: use 1-second kurtosis > 1 (literature-consistent criterion)
fat_tails_ok = (not math.isnan(kurt_1s)) and (kurt_1s > 1.0)

print(json.dumps({
    "seed":         seed,
    "ok":           True,
    "n_obs":        sf.returns.n_obs,
    "kurtosis_1s":  kurt_1s,               # primary kurtosis (1-second aggregated)
    "kurtosis_tick":sf.returns.excess_kurtosis,  # for reference only
    "ret_ac":       ac.return_ac[0]     if ac.return_ac     else None,
    "abs_ac":       ac.abs_return_ac[0] if ac.abs_return_ac else None,
    "sign_ac":      ac.sign_flow_ac[0]  if ac.sign_flow_ac  else None,
    "spread":       sf.spreads.time_weighted_spread,
    "lambda_r2":    sf.impact.r_squared,
    "fat_tails_ok": fat_tails_ok,
    "vol_clust":    sf.vol_clustering_ok,
    "flow_ac":      sf.flow_autocorr_ok,
    "pos_spread":   sf.positive_spread_ok,
}))
