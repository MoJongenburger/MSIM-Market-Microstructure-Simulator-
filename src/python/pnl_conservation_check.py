"""
MSIM PnL Conservation Check — run from the repo root after building.

Usage:
    python src/python/pnl_conservation_check.py

What it checks:
    1. Per-agent final mark-to-market PnL (cash + position × mid)
    2. Sum of all agent PnL
    3. Whether the system conserves wealth (zero-sum vs mark-to-market)
    4. Realised-only PnL (cash component) which IS zero-sum in a closed system
"""

import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'src', 'python'))
import msim

# ── Calibrated 9-agent configuration (matches paper Section 7.1) ─────────────
def build_world():
    world = msim.World()
    world.prefill_book(mid=10_000, levels=20, qty=10)

    hawkes_cfg = msim.HawkesNoiseConfig()
    hawkes_cfg.p_market  = 0.5
    hawkes_cfg.lot_size  = 2
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
    world = build_world()
    result = world.run(seed=seed, horizon=horizon)
    tca    = result.tca_df()

    print(f"\n{'='*60}")
    print(f"MSIM PnL Conservation Check  (seed={seed}, horizon={horizon}s)")
    print(f"{'='*60}\n")

    # ── Per-agent breakdown ───────────────────────────────────────────────────
    print(f"{'Agent':<28} {'Cash (ticks)':>14} {'Position':>10} {'MTM PnL':>14}")
    print("-" * 70)

    total_mtm     = 0.0
    total_cash    = 0.0
    total_pos     = 0

    for _, row in tca.iterrows():
        owner   = int(row['owner'])
        cash    = float(row['final_cash_ticks'])
        pos     = int(row['final_position'])
        mtm_pnl = float(row['final_mtm_pnl'])
        label   = {1:'HawkesNoise-1', 2:'HawkesNoise-2', 3:'HawkesNoise-3',
                   4:'HawkesNoise-4', 5:'HawkesNoise-5',
                   10:'MarketMakerAS', 20:'FundValue-aggressive',
                   21:'FundValue-slow', 30:'Momentum'}.get(owner, f'Agent-{owner}')
        print(f"  {label:<26} {cash:>+14,.1f} {pos:>+10} {mtm_pnl:>+14,.1f}")
        total_mtm  += mtm_pnl
        total_cash += cash
        total_pos  += pos

    print("-" * 70)
    print(f"  {'TOTAL':26} {total_cash:>+14,.1f} {total_pos:>+10} {total_mtm:>+14,.1f}\n")

    # ── Conservation analysis ─────────────────────────────────────────────────
    print("=== Conservation Analysis ===\n")
    print(f"  Sum of all mark-to-market PnL:  {total_mtm:>+14,.1f} ticks")
    print(f"  Sum of all cash PnL:            {total_cash:>+14,.1f} ticks")
    print(f"  Sum of all final positions:     {total_pos:>+10} lots\n")

    if abs(total_cash) < 1.0:
        print("  ✅ CASH PnL CONSERVES: sum of all cash = ~0")
        print("     The system is zero-sum on realised cash flows.")
    else:
        print(f"  ⚠️  CASH PnL SUM = {total_cash:+,.1f} (expected ~0 for closed system)")
        print("     Investigate: possible prefill_book effect or accounting asymmetry.")

    if total_pos == 0:
        print("  ✅ NET POSITION = 0: all inventory is balanced across agents.")
    else:
        print(f"  ℹ️  NET POSITION = {total_pos:+d} lots (open at end of simulation).")
        print(f"     MTM diverges from cash by: {total_mtm - total_cash:+,.1f} ticks")
        print(f"     (= net_position × final_mid, explaining the MTM non-zero sum)")

    print()
    print("=== Paper-Reported Values (seed=42) vs This Run ===\n")
    paper_pnl = {10: +10094, 20: +119993, 21: -250072, 30: +49638}
    for owner, paper_val in paper_pnl.items():
        row = tca[tca['owner'] == owner]
        if not row.empty:
            actual = float(row['final_mtm_pnl'].values[0])
            diff   = actual - paper_val
            match  = "✅" if abs(diff) < 500 else "⚠️ "
            label  = {10:'MarketMakerAS', 20:'FundValue-aggressive',
                      21:'FundValue-slow', 30:'Momentum'}[owner]
            print(f"  {match} {label:<28} paper={paper_val:>+10,}  "
                  f"actual={actual:>+10,.0f}  diff={diff:>+8,.0f}")


if __name__ == '__main__':
    seed    = int(sys.argv[1]) if len(sys.argv) > 1 else 42
    horizon = float(sys.argv[2]) if len(sys.argv) > 2 else 300.0
    run_check(seed=seed, horizon=horizon)
