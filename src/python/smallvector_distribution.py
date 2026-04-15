import sys, collections
sys.path.insert(0, 'src/python')
import msim

def build_world():
    world = msim.World()
    world.prefill_book(mid=10_000, levels=20, qty=10)
    hc = msim.HawkesNoiseConfig(); hc.p_market = 0.5; hc.lot_size = 2
    for i in range(1, 6):
        world.add_agent(msim.agents.HawkesNoiseTrader(owner_id=i, config=hc))
    world.add_agent(msim.agents.MarketMakerAS(owner_id=10))
    fv1 = msim.FundamentalValueConfig(); fv1.threshold=1; fv1.sigma_v=0.3; fv1.lot_size=2
    world.add_agent(msim.agents.FundamentalValueAgent(owner_id=20, config=fv1))
    fv2 = msim.FundamentalValueConfig(); fv2.threshold=3; fv2.sigma_v=0.2; fv2.lot_size=1
    world.add_agent(msim.agents.FundamentalValueAgent(owner_id=21, config=fv2))
    mom = msim.MomentumConfig(); mom.entry_band=2; mom.lot_size=1
    world.add_agent(msim.agents.MomentumAgent(owner_id=30, config=mom))
    return world

counter = collections.Counter()
total = 0
for seed in range(42, 52):
    result = build_world().run(seed=seed, horizon=300.0)
    fills = result.fills_df()
    if len(fills) > 0 and 'order_id' in fills.columns:
        for v in fills.groupby('order_id').size().values:
            counter[int(v)] += 1; total += 1
print('Fills/call  Count  Fraction  Cumulative')
cum = 0
for k in sorted(counter):
    f = counter[k]/total; cum += f
    print(f'{k:>10}  {counter[k]:>6}  {f:>7.4f}  {cum:>9.4f}' + ('  HEAP' if k>4 else ''))
leq4 = sum(v for k,v in counter.items() if k<=4)
print(f'<=4 fills: {leq4/total*100:.2f}%  >4 fills: {(total-leq4)/total*100:.4f}%')
"
