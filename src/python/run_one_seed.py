"""
Single-seed worker called by multiseed_study.py via subprocess.

Agent configuration (calibrated for cross-seed stability):
  - 5 Hawkes noise traders  (p_market=0.5, lot_size=2)
  - 1 Avellaneda-Stoikov market maker
  - 1 FundamentalValue agent (threshold=1, sigma_v=0.3, lot_size=2)
  - 1 FundamentalValue agent (threshold=3, sigma_v=0.2, lot_size=1)
  - 1 Momentum agent         (entry_band=2, lot_size=1)

Compared to the original configuration the two changes are:
  fv1: sigma_v 0.5 → 0.3   (less volatile fundamental process)
  fv2: sigma_v 1.0 → 0.2,  threshold 2 → 3  (slower, less aggressive)
These changes keep price dynamics within realistic bounds across all seeds.
"""
import sys, json, msim

seed = int(sys.argv[1])

world = msim.World()
world.prefill_book(mid=10000, levels=20, qty=10)

# 5 Hawkes noise traders — unchanged from validation run
for owner_id in range(1, 6):
    h = msim.HawkesNoiseConfig()
    h.p_market = 0.5
    h.lot_size = 2
    world.add_agent(msim.agents.HawkesNoiseTrader(owner_id=owner_id, config=h))

# Avellaneda-Stoikov market maker — unchanged
world.add_agent(msim.agents.MarketMakerAS(owner_id=10))

# FundValue aggressive — sigma_v reduced 0.5 → 0.3
fv1 = msim.FundamentalValueConfig()
fv1.threshold = 1
fv1.sigma_v   = 0.3
fv1.lot_size  = 2
world.add_agent(msim.agents.FundamentalValueAgent(owner_id=20, config=fv1))

# FundValue slow — sigma_v reduced 1.0 → 0.2, threshold raised 2 → 3
fv2 = msim.FundamentalValueConfig()
fv2.threshold = 3
fv2.sigma_v   = 0.2
fv2.lot_size  = 1
world.add_agent(msim.agents.FundamentalValueAgent(owner_id=21, config=fv2))

# Momentum — entry_band raised 1 → 2 (less trigger-happy)
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
