"""
Single-seed worker — called by multiseed_study.py via subprocess.

Agent configuration (v2 — tuned for cross-seed kurtosis stability):
  - 5 Hawkes noise traders  (p_market=0.5, lot_size=2)
  - 1 Avellaneda-Stoikov market maker
  - 1 FundValue aggressive  (threshold=2, kappa=0.3, sigma_v=0.15, lot_size=2)
  - 1 FundValue slow        (threshold=4, kappa=0.3, sigma_v=0.10, lot_size=1)
  - 1 Momentum              (entry_band=2, lot_size=1)

Changes vs v1:
  - Both FundValue kappa: 0.1 → 0.3  (3× faster mean reversion,
    V stays within ±0.4 ticks of mid instead of ±0.7)
  - fv1 sigma_v: 0.3 → 0.15
  - fv2 sigma_v: 0.2 → 0.10
  - fv1 threshold: 1 → 2
  - fv2 threshold: 3 → 4
  - prefill qty per level: 10 → 20  (more book resilience, smaller sweeps)

These changes prevent multi-level sweeps that created extreme (>10σ) returns
while preserving fat-tail dynamics and bid-ask bounce.
"""
import sys, json, msim

seed = int(sys.argv[1])

world = msim.World()
world.prefill_book(mid=10000, levels=20, qty=20)   # doubled qty per level

for owner_id in range(1, 6):
    h = msim.HawkesNoiseConfig()
    h.p_market = 0.5
    h.lot_size = 2
    world.add_agent(msim.agents.HawkesNoiseTrader(owner_id=owner_id, config=h))

world.add_agent(msim.agents.MarketMakerAS(owner_id=10))

fv1 = msim.FundamentalValueConfig()
fv1.threshold = 2
fv1.sigma_v   = 0.15
fv1.kappa     = 0.3
fv1.lot_size  = 2
world.add_agent(msim.agents.FundamentalValueAgent(owner_id=20, config=fv1))

fv2 = msim.FundamentalValueConfig()
fv2.threshold = 4
fv2.sigma_v   = 0.10
fv2.kappa     = 0.3
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

r  = world.run(seed=seed, horizon=300.0, config=cfg)
sf = r.sf

if not sf or sf.returns.n_obs < 50:
    print(json.dumps({"seed": seed, "ok": False,
                      "n_obs": sf.returns.n_obs if sf else 0}))
    sys.exit(0)

ac = sf.autocorr
print(json.dumps({
    "seed":      seed,
    "ok":        True,
    "n_obs":     sf.returns.n_obs,
    "kurtosis":  sf.returns.excess_kurtosis,
    "ret_ac":    ac.return_ac[0]     if ac.return_ac     else None,
    "abs_ac":    ac.abs_return_ac[0] if ac.abs_return_ac else None,
    "sign_ac":   ac.sign_flow_ac[0]  if ac.sign_flow_ac  else None,
    "spread":    sf.spreads.time_weighted_spread,
    "lambda_r2": sf.impact.r_squared,
    "fat_tails": sf.fat_tails_ok,
    "vol_clust": sf.vol_clustering_ok,
    "flow_ac":   sf.flow_autocorr_ok,
    "pos_spread":sf.positive_spread_ok,
}))
