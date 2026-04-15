import sys
sys.path.insert(0, 'src/python')
import msim, numpy as np

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

r = bw().run(seed=42, horizon=300.0)
df = r.fills_df()
if 'arrival_mid' in df.columns:
    df = df.rename(columns={'arrival_mid':'mid'})
if 'aggressor_side' in df.columns:
    D = df['aggressor_side'].map({'Buy':1,'Sell':-1}).fillna(0).values
elif 'side' in df.columns:
    D = df['side'].map({'Buy':1,'Sell':-1}).fillna(0).values
else:
    D = np.zeros(len(df))
p = df['fill_price'].values
m = df['mid'].values if 'mid' in df.columns else p
print('Fill records: ' + str(len(df)))
print('Delta    n      Eff     Real      Adv    Check')
for delta in [3,5,10,20]:
    idx = np.arange(len(df)-delta)
    if len(idx)<10:
        print(str(delta) + ' insufficient')
        continue
    mf=m[idx+delta]; mn=m[idx]; pn=p[idx]; dn=D[idx]
    eff=np.mean(2*dn*(pn-mn))
    real=np.mean(2*dn*(pn-mf))
    adv=np.mean(2*dn*(mf-mn))
    print(str(delta).rjust(5)+'  '+str(len(idx)).rjust(5)+'  '+str(round(eff,2)).rjust(7)+'  '+str(round(real,2)).rjust(7)+'  '+str(round(adv,2)).rjust(7)+'  '+str(round(real+adv,2)).rjust(7))
"
