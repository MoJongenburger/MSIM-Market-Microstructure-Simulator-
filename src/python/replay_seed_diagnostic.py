import sys
sys.path.insert(0, 'src/python')
import msim

def bw():
    w = msim.World()
    w.prefill_book(mid=10_000, levels=20, qty=10)
    hc = msim.HawkesNoiseConfig(); hc.p_market=0.5; hc.lot_size=2
    for i in range(1,6): w.add_agent(msim.agents.HawkesNoiseTrader(owner_id=i, config=hc))
    w.add_agent(msim.agents.MarketMakerAS(owner_id=10))
    fv1=msim.FundamentalValueConfig(); fv1.threshold=1; fv1.sigma_v=0.3; fv1.lot_size=2
    w.add_agent(msim.agents.FundamentalValueAgent(owner_id=20, config=fv1))
    fv2=msim.FundamentalValueConfig(); fv2.threshold=3; fv2.sigma_v=0.2; fv2.lot_size=1
    w.add_agent(msim.agents.FundamentalValueAgent(owner_id=21, config=fv2))
    mom=msim.MomentumConfig(); mom.entry_band=2; mom.lot_size=1
    w.add_agent(msim.agents.MomentumAgent(owner_id=30, config=mom))
    return w

labels={1:'HawkesNoise-1',2:'HawkesNoise-2',3:'HawkesNoise-3',4:'HawkesNoise-4',
        5:'HawkesNoise-5',10:'MarketMakerAS',20:'FundValue-aggr',21:'FundValue-slow',30:'Momentum'}

for seed in [1029, 1048]:
    print('')
    print('=== Seed ' + str(seed) + ' ===')
    r = bw().run(seed=seed, horizon=300.0)
    trades = r.trades_df()
    print('Trades: ' + str(len(trades)) + '  n_returns: ' + str(len(trades)-1))
    tops = r.tops_df() if hasattr(r,'tops_df') else None
    if tops is not None and 'spread' in tops.columns:
        s = tops['spread'].dropna()
        print('Spread min=' + str(round(s.min(),1)) + ' median=' + str(round(s.median(),1)) + ' max=' + str(round(s.max(),1)) + ' final=' + str(round(s.iloc[-1],1)))
    if tops is not None and 'mid' in tops.columns:
        m = tops['mid'].dropna()
        print('Mid initial=' + str(round(m.iloc[0],1)) + ' final=' + str(round(m.iloc[-1],1)) + ' drift=' + str(round(abs(m.iloc[-1]-m.iloc[0]),1)))
    for _, row in r.tca_df().iterrows():
        own = int(row['owner'])
        lbl = labels.get(own, str(own))
        orders = int(row.get('n_orders_submitted', 0))
        pos = float(row.get('final_position', 0))
        print('  ' + lbl + ' orders=' + str(orders) + ' pos=' + str(int(pos)))
"
