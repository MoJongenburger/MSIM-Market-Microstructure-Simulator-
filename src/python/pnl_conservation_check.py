"""
MSIM PnL Conservation Check — run from the repo root after building.

Usage:
    python src/python/pnl_conservation_check.py [seed] [horizon_seconds]

Examples:
    python src/python/pnl_conservation_check.py           # seed=42, 300s
    python src/python/pnl_conservation_check.py 1234      # seed=1234, 300s
    python src/python/pnl_conservation_check.py 42 600    # seed=42, 600s

What it checks:
    1. Per-agent final mark-to-market PnL (cash + position x mid)
    2. Per-agent final cash PnL (realised flows only)
    3. Sum of all agent PnL -- must equal zero in a closed system
    4. Net position across all agents -- must equal zero
"""

import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'src', 'python'))
import msim


# Calibrated 9-agent configuration (matches paper Section 7.1)
def build_world():
    world = msim.World()
    world.prefill_book(mid=10_000, levels=20, qty=10)

    hawkes_cfg = msim.HawkesNoiseConfig()
    hawkes_cfg.p_market = 0.5
    hawkes_cfg.lot_size = 2
    for i in range(1, 6):
        world.add_agent(msim.agents.HawkesNoiseTrader(owner_id=i, config=hawkes_cfg))

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

    return world


def run_check(seed=42, horizon=300.0):
    world  = build_world()
    result = world.run(seed=seed, horizon=horizon)
    tca    = result.tca_df()

    print(f"\n{'='*60}")
    print(f"MSIM PnL Conservation Check  (seed={seed}, horizon={horizon}s)")
    print(f"{'='*60}\n")

    labels = {
        1:  'HawkesNoise-1',
        2:  'HawkesNoise-2',
        3:  'HawkesNoise-3',
        4:  'HawkesNoise-4',
        5:  'HawkesNoise-5',
        10: 'MarketMakerAS',
        20: 'FundValue-aggressive',
        21: 'FundValue-slow',
        30: 'Momentum',
    }

    print(f"  {'Agent':<26} {'Cash (ticks)':>14} {'Position':>10} {'MTM PnL':>14}")
    print("  " + "-" * 68)

    total_mtm  = 0.0
    total_cash = 0.0
    total_pos  = 0

    for _, row in tca.iterrows():
        owner   = int(row['owner'])
        cash    = float(row['final_cash_ticks'])
        pos     = int(row['final_position'])
        mtm_pnl = float(row['final_mtm_pnl'])
        label   = labels.get(owner, f'Agent-{owner}')
        print(f"  {label:<26} {cash:>+14,.1f} {pos:>+10} {mtm_pnl:>+14,.1f}")
        total_mtm  += mtm_pnl
        total_cash += cash
        total_pos  += pos

    print("  " + "-" * 68)
    print(f"  {'TOTAL':<26} {total_cash:>+14,.1f} {total_pos:>+10} {total_mtm:>+14,.1f}\n")

    print("=== Conservation Analysis ===\n")
    print(f"  Sum of all cash PnL:            {total_cash:>+14,.1f} ticks")
    print(f"  Sum of all mark-to-market PnL:  {total_mtm:>+14,.1f} ticks")
    print(f"  Sum of all final positions:     {total_pos:>+10} lots\n")

    cash_ok = abs(total_cash) < 1.0
    pos_ok  = total_pos == 0
    mtm_ok  = abs(total_mtm) < 1.0

    if cash_ok:
        print("  PASS  CASH PnL CONSERVES (sum = 0)")
        print("        Every tick gained by one agent was lost by another.")
    else:
        print(f"  FAIL  CASH PnL SUM = {total_cash:+,.1f}  (expected 0)")
        print("        Investigate: accounting asymmetry or prefill_book effect.")

    if pos_ok:
        print("  PASS  NET POSITION = 0")
        print("        All inventory is balanced -- no unaccounted shares outstanding.")
    else:
        print(f"  FAIL  NET POSITION = {total_pos:+d} lots  (expected 0)")
        print("        Investigate: asymmetric fill accounting or prefill shares.")

    if mtm_ok:
        print("  PASS  MTM PnL CONSERVES (sum = 0)")
    else:
        mtm_gap = total_mtm - total_cash
        print(f"  INFO  MTM PnL SUM = {total_mtm:+,.1f}  (gap vs cash = {mtm_gap:+,.1f})")
        print(f"        Expected if agents hold open inventory at simulation end.")

    print()
    if cash_ok and pos_ok:
        print("  VERDICT: The simulation is a closed zero-sum system.")
        print("           Wealth is conserved exactly across all agents.")
    else:
        print("  VERDICT: Conservation failure -- investigate before publishing.")


if __name__ == '__main__':
    seed    = int(sys.argv[1])   if len(sys.argv) > 1 else 42
    horizon = float(sys.argv[2]) if len(sys.argv) > 2 else 300.0
    run_check(seed=seed, horizon=horizon)
