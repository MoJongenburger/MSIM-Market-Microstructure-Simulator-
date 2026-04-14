# MSIM — Market Microstructure Simulator (C++20)

<!-- Badges -->
![C++](https://img.shields.io/badge/C%2B%2B-20-blue)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-064F8C)
[![CI](https://github.com/MoJongenburger/MSIM-Market-Microstructure-Simulator-/actions/workflows/ci.yml/badge.svg)](https://github.com/MoJongenburger/MSIM-Market-Microstructure-Simulator-/actions/workflows/ci.yml)
![Latency](https://img.shields.io/badge/Latency-p50%2046.6ns-brightgreen)
![Tail](https://img.shields.io/badge/Tail-p99%2047.1ns-brightgreen)
![Throughput](https://img.shields.io/badge/Throughput-21.9M%20ops%2Fs-blueviolet)
![Stylised Facts](https://img.shields.io/badge/Stylised%20Facts-4%2F5%20confirmed%2C%201%20emergent-blue)
![License](https://img.shields.io/github/license/MoJongenburger/MSIM-Market-Microstructure-Simulator-)

MSIM is a deterministic, event-driven **limit order book + matching engine** written in modern **C++20**, built as a **microstructure research sandbox** for studying execution mechanics, venue rules, and agent interaction. It ships with a full **Python strategy interface** via pybind11, enabling researchers to write strategies in Python against the same sub-50 ns C++ engine (p50 = 46.6 ns, p99 = 47.1 ns on a commodity workstation).

The project prioritises **reproducibility, correctness, and extensibility**. Its architecture separates:

* **Core exchange mechanics** — order book, matching, trade printing
* **Rule / policy logic** — admission checks, market phases, auctions, halts
* **Agent layer** — informed traders, trend-followers, noise traders, market makers, execution algorithms
* **Measurement layer** — stylized facts, TCA, PnL series, queue position tracking

This keeps the "exchange kernel" small and testable while allowing realistic venue behaviour to be layered on top without touching the matching engine.

---

## Performance

### Benchmark summary (Windows, MSVC 19.44, Release, 12-core/24-thread @ 2112 MHz)

| Benchmark | p50 (ns) | p90 (ns) | p99 (ns) | What it measures |
|---|---:|---:|---:|---|
| `BM_ProcessReject_InvalidQty` | 14.9 | 15.0 | 15.0 | Fast-path reject (invalid order) |
| `BM_ProcessMarketOrder` | **46.6** | 47.0 | **47.1** | Market order hot path |
| `BM_ProcessCrossingLimitIOC` | 46.8 | 47.4 | 48.0 | Crossing limit IOC |
| `BM_Throughput_ProcessMarketOrder` | 45.7 | 46.0 | 46.4 | Sustained throughput loop |
| `BM_ProcessMarket_SweepKLevels` | 26.3 | 26.6 | 26.9 | Per-level cost sweeping K=1024 levels |
| `BM_BookDepth_TopN` | 84.0 | 86.9 | 88.6 | L2 depth snapshot (Top-N) |
| `BM_BookCancel_O1` | 127.7 | 128.6 | 128.7 | Cancel a resting limit order |
| `BM_BookAddRestingLimit` | 150.2 | 153.2 | 155.1 | Insert a resting limit order |
| `BM_BookModifyQty_O1` | 221.8 | 223.6 | 224.5 | Reduce quantity of a resting order |

**Throughput:** 21.9 M market orders/sec at p50 · 21.5 M/sec at p99 (single-threaded, warm book).

---

### Hot-path stability — ProcessMarketOrder across book sizes

p50 stays flat at ~47 ns whether the book has 100 or 10,000 resting orders, demonstrating that the FlatPriceMap and vector-queue design keeps all hot data in L1 cache regardless of book depth.

<img width="2420" height="1100" alt="latency_benchmark" src="https://github.com/user-attachments/assets/389b9850-8c65-4fb7-a52e-174956b3cad1" />

---

### Latency distribution — Market order processing

Box-and-whisker distribution of `BM_ProcessMarketOrder` across all repetitions at N=10,000 resting orders. The tight interquartile range confirms low variance in the critical execution path.

<img width="1760" height="1100" alt="latency_box_BM_ProcessMarketOrder" src="https://github.com/user-attachments/assets/8736536c-0226-4664-b30c-61a76bbdadf4" />

---

### Latency distribution — Multi-level sweep (K=1024 levels)

`BM_ProcessMarket_SweepKLevels` at K=1024 price levels — p50=26.3 ns per level. The O(1) front-erase in `FlatPriceMap` means sweep cost grows linearly with levels consumed, not quadratically.

<img width="2200" height="1000" alt="latency_box_BM_ProcessMarket_SweepKLevels" src="https://github.com/user-attachments/assets/846e7a7a-291c-48d8-acef-d5562a11b2c7" />

---

### Latency distribution — L2 depth snapshot

`BM_BookDepth_TopN` across varying N. The `live_count` field in each price level makes order counting O(1) per level, keeping depth queries under 100 ns p99 regardless of how many orders have been cancelled at each level.

<img width="2200" height="1000" alt="latency_box_BM_BookDepth_TopN" src="https://github.com/user-attachments/assets/1872c7fd-21c6-4de0-ab3b-92be62dbcb22" />

---

## Empirical Validation — Stylised Facts

A 300-second simulation with 9 agents (5 Hawkes noise traders, Avellaneda-Stoikov market maker, 2 fundamental value agents, momentum agent) demonstrates qualitative emergence of five canonical microstructure regularities. Four facts are confirmed at pass rates of 96–100% across 50 independent seeds (see Multi-Seed Robustness below); price impact is qualitatively emergent:

| Statistic | Value | Literature range | Reference | Status |
|---|---:|---|---|---|
| Excess kurtosis | 4.04 (median 7.04 / 50 seeds) | > 3 tick-level | Cont (2001) | ✅ Fat tails — 46/48 seeds (96%) |
| Return AC lag-1 | −0.451 | negative | Roll (1984) | ✅ Bid-ask bounce — 47/48 seeds (98%) |
| \|Return\| AC lag-1 | 0.323 | 0.10–0.40 | Engle (1982) | ✅ Vol clustering — 46/48 seeds (96%) |
| Trade-sign AC lag-1 | 0.270 (median 0.327) | 0.30–0.70 | Bouchaud et al. (2004) | ✅ Flow AC — 50/50 seeds (100%) |
| Time-weighted spread | 8.86 ticks (median 8.37) | positive | Glosten-Milgrom (1985) | ✅ Positive spread — 49/50 seeds (98%) |

> **Note on price impact:** Kyle's λ is nonzero but R²≈0.0005 at n~800. The A-S market maker continuously re-prices quotes after each fill, causing mean reversion rather than sustained post-trade drift — this attenuates the signal that Kyle's λ captures. The near-zero R² is consistent across all 50 seeds (mean R²=0.003).
>
> **Spread decomposition:** The identity effective = realized + adverse selection holds (9.89 ≈ 36.38 + (−26.51) ticks), validating the Huang-Stoll estimator. The negative adverse selection component indicates that prices mean-revert after each fill — a testable prediction of the A-S model's continuous quote re-pricing.

### Multi-Seed Robustness

A 50-seed robustness study (`src/python/multiseed_study.py`) confirms these results are not specific to seed=42:

| Fact | Pass rate | Median |
|---|---|---|
| Fat tails (kurtosis > 3) | **46/48 (96%)** | kurtosis 7.04 |
| Volatility clustering | **46/48 (96%)** | \|ret\| AC 0.212 |
| Flow autocorrelation | **50/50 (100%)** | sign AC 0.327 |
| Positive spread | **49/50 (98%)** | 8.37 ticks |
| Return AC negative | **47/48 (98%)** | −0.317 |

Kurtosis median of 7.04 falls within the [3,10] range of Cont (2001). Seeds with kurtosis > 10 exhibit genuine fat-tail behaviour from occasional informed-flow sweeps, consistent with real transaction-level LOB data.

---

### Key Finding: Emergent vs. Idiosyncratic

A structural property of the simulation is that **aggregate market statistics are robust while individual agent outcomes are highly path-dependent**. Across code versions that produce entirely different stochastic trajectories for seed=42, the four stylised facts (kurtosis, volatility clustering, flow AC, spread) remain identical to three significant figures, while individual agent PnL values change by hundreds of thousands of ticks including sign reversals. This mirrors real markets: market-level regularities coexist with enormous idiosyncratic variance in individual trader performance. The market maker's PnL (~+10k ticks) is structurally robust across paths, consistent with the A-S inventory management framework.

---

## Optimization Journey

The engine has been systematically optimised through eight targeted passes. Each pass is independently motivated, measured before and after, and fully covered by the CI test suite.

| Pass | Change | Key improvement |
|---|---|---|
| 1 | `std::list` queue → `std::vector` + tombstone | BookAdd 888 → 161 ns **(5.5×)** |
| 2 | `FlatPriceMap` (sorted vector replaces `std::map`) | Eliminates red-black tree pointer-chasing |
| 3 | `FlatPriceMap::front_offset_` (O(1) level erase) | SweepKLevels 103 → 28 ns **(3.7×)** |
| 4 | `live_count` field in `Level` (O(1) depth query) | BookDepth 2107 → 84 ns **(25.1×)** on Windows |
| 5 | `SmallVector<Trade, 4>` for `MatchResult::trades` | Heap allocation eliminated for >95% of orders |
| 6 | `next_event_ts_` cache in matching engine | ~2000 redundant `flush()` calls/sec eliminated |
| 7 | Robin Hood `FlatHashMap` for `OrderBook::loc_` | BookCancel 900 → 128 ns **(7.0×)** |
| 8 | Run-loop bb/ba reuse, insertion sort, `sfm.reserve` | 2–4 redundant book queries/step eliminated |

---

## Python Quick Start

```python
import msim

# Create a world with a pre-filled symmetric book
world = msim.World()
world.prefill_book(mid=10_000, levels=20, qty=10)

# Register agents
world.add_agent(msim.agents.HawkesNoiseTrader(owner_id=1))
world.add_agent(msim.agents.MarketMakerAS(owner_id=2))

# Run 2 seconds of simulation
result = world.run(seed=42, horizon=2.0)

# Inspect outputs
print(result.trades_df().head())   # all trades
print(result.tca_df())             # per-agent TCA summary
result.summary()                   # stylized facts report
```

### Writing a custom strategy

```python
class MyStrategy(msim.Agent):
    def __init__(self, owner_id: int):
        super().__init__()
        self._owner   = owner_id
        self._counter = 0

    def owner(self) -> int:
        return self._owner

    def seed(self, s: int) -> None:
        import random
        self._rng     = random.Random(s)
        self._counter = 0

    def step(self, ts: int, view: msim.MarketView,
             state: msim.AgentState) -> list:
        if not view.has_quote():
            return []
        # Cancel resting orders at front of queue when spread is thin
        for qp in state.queue_positions:
            if qp.is_front() and view.spread() and view.spread() <= 2:
                return [msim.Action.cancel(qp.order_id)]
        # Submit a market buy
        o           = msim.Order()
        o.id        = (self._owner << 24) | (self._counter & 0xFFFFFF)
        o.owner     = self._owner
        o.side      = msim.Side.Buy
        o.type      = msim.OrderType.Market
        o.qty       = 1
        o.tif       = msim.TimeInForce.IOC
        o.mkt_style = msim.MarketStyle.PureMarket
        self._counter += 1
        return [msim.Action.submit(o)]
```

---

## Build & Install

### Requirements

* **Windows:** Visual Studio 2022 Build Tools (x64 Native Tools Command Prompt)
* **macOS / Linux:** Clang 14+ or GCC 12+
* CMake 3.20+, Python 3.11+

### Windows (x64 Native Tools Command Prompt for VS 2022)

```cmd
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMSIM_BUILD_PYTHON=ON -G "NMake Makefiles"
cmake --build build
setx PYTHONPATH "%CD%\src\python"
```

Close and reopen the prompt after `setx`, then verify:

```cmd
python -c "import msim; print(msim.__version__)"
```

### macOS / Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMSIM_BUILD_PYTHON=ON
cmake --build build
export PYTHONPATH=$PWD/src/python
python -c "import msim; print(msim.__version__)"
```

> **No `pip install` required.** The build writes `_msim_core.*.pyd` (Windows) or `_msim_core.*.so` (Unix) directly into `src/python/msim/`, so the package is importable immediately after `cmake --build`.

### Run tests

```cmd
build\msim_tests.exe        # Windows
./build/msim_tests          # macOS / Linux
```

All 26 unit tests pass on Linux (ASan + UBSan), macOS Debug/Release, and Windows MSVC.

---

## Why It Is Built This Way

**Deterministic replay.**
Microstructure experiments must be repeatable. The same event stream and seed produce bit-for-bit identical trades, fills, and book evolution. Every agent uses a splitmix64-derived seed; the scenario runner uses the same derivation so 1000-seed sweeps are fully reproducible.

**Separation of concerns.**
Matching is a narrow, correctness-critical component. Venue rules evolve quickly. MSIM keeps those concerns cleanly separated so new rules do not destabilise the core engine.

**Invariants first.**
Exchange logic is easy to break in subtle ways — crossed book, incorrect FIFO, partial fill accounting. MSIM leans heavily on unit tests and CI to protect invariants across all platforms.

**Phase-aware market model.**
Real venues are session-driven: continuous trading, auctions, and halts. MSIM treats market phase as a first-class concept integrated into order processing.

**Empirical validation built in.**
The stylized facts measurer runs automatically after every `World::run()`. It checks whether generated data reproduces five canonical empirical regularities, with citable thresholds from the microstructure literature.

**Python-first research interface.**
The C++ engine is the performance foundation; Python is the research interface. Researchers write strategies in Python, query TCA outputs as DataFrames, sweep parameters with the scenario runner, and benchmark execution algorithms — all without touching C++.

---

## Features

### Core book + matching

* Price–time priority **limit order book** (FIFO per price level)
* **Market** and **Limit** orders with partial fills and multi-level sweeps
* L2 depth snapshots (top-N levels) + top-of-book tracking
* **Cancel** and **reduce-only modify** (preserves time priority)

### Order instructions

* **Time-in-Force:** GTC / IOC / FOK
* **Market-to-Limit:** partial market fill remainder becomes a resting limit

### Rule / policy layer

* Structured rejects with reason codes: invalid order, market halted, tick/lot violations
* Reference price tracking (last trade, mid-price fallback)

### Market phases + auctions

* Phases: Continuous, Auction (uncrossing), Trading-at-Last, Closing Auction, Halted
* **Auction uncrossing** at a single clearing price: maximise executable volume, tie-break by closest to reference price
* **Price bands + volatility interruption** → transition to auction
* **Circuit breaker halt + reopening auction**

### Self-Trade Prevention

* Modes: None, CancelTaker, CancelMaker

---

## Agents

| Agent | Config | Description |
|---|---|---|
| `NoiseTrader` | — | Random market/limit order flow |
| `MarketMaker` | — | Quotes around mid with inventory skew |
| `FundamentalValueAgent` | `FundamentalValueConfig` | Glosten-Milgrom informed trader with OU private signal |
| `MomentumAgent` | `MomentumConfig` | MACD trend-follower with position limits |
| `HawkesNoiseTrader` | `HawkesNoiseConfig` | Self-exciting noise trader (Hawkes process arrivals) |
| `MarketMakerAS` | `MarketMakerASConfig` | Avellaneda-Stoikov optimal quoting with imbalance skew |
| `VWAPAgent` | `VWAPConfig` | VWAP execution (FLAT / U_SHAPE / CUSTOM schedule) |
| `TWAPAgent` | `TWAPConfig` | Uniform time-slicing execution baseline |
| `ISAgent` | `ISConfig` | Almgren-Chriss optimal IS trajectory |

### Hawkes Noise Trader

Replaces flat Poisson arrivals with a self-exciting process:

```
λ(t) = μ + ψ(t),   ψ_{t+1} = ψ_t · exp(−β·dt) + α·N_t
```

Order side is biased by LOB imbalance (Cont, Kukanov & Stoikov 2014), so noise traders partially follow short-term book pressure.

### Avellaneda-Stoikov Market Maker

Implements the full A-S (2008) optimal quoting formula with EWMA volatility estimation and LOB imbalance skew:

```
r     = s − q·γ·σ²·(T−t) + α_imb·I·σ
δ*    = γ·σ²·(T−t) + (2/γ)·ln(1 + γ/κ)
```

Quotes bid at `r − δ*/2`, ask at `r + δ*/2`.

### Implementation Shortfall Agent (Almgren-Chriss)

Executes the optimal trajectory minimising `E[IS] + λ·Var[IS]`:

```
x_k = X · sinh(κ(T−k)) / sinh(κT),   κ = √(λ·σ²/η)
```

With `λ=0` degenerates to TWAP. High `λ` front-loads execution.

---

## TCA / PnL Output Layer

Every `World::run()` call returns structured execution analytics alongside raw trades.

### Per-fill records

```python
result.fills_df()
# Columns: ts, owner, order_id, side, fill_qty, fill_price,
#          arrival_mid, is_maker, slippage
```

`slippage` = signed deviation from mid-price at order submission. Positive = paid above mid (market impact). Negative = filled inside mid (limit order edge).

### Per-step PnL series

```python
df = result.pnl_df()
# Columns: ts, owner, position, cash_ticks, mid, mtm_pnl
agent_pnl = df[df.owner == 2].set_index("ts")["mtm_pnl"]
```

`mtm_pnl = cash_ticks + position × mid` — full mark-to-market at every step.

### Per-agent TCA summary

```python
result.tca_df()
# Columns: owner, n_orders_submitted, n_limit_submitted,
#          n_market_submitted, n_cancels_sent, n_fills_maker,
#          n_fills_taker, total_qty_traded, limit_fill_rate,
#          avg_slippage_ticks, turnover_notional_ticks,
#          final_position, final_cash_ticks, final_mtm_pnl
```

Always populated regardless of `record_fills` / `record_pnl_series` flags.

---

## Execution Algorithm Benchmarking

```python
from msim import execution as ex

results = {
    "TWAP": r_twap,
    "VWAP": r_vwap,
    "IS":   r_is,
}
comparison = ex.compare_strategies(results, owner_ids={...})
print(comparison[["is_ticks", "is_bps", "completion_rate"]])
```

Implementation Shortfall vs arrival price:
```
IS (ticks) = (avg_fill_price − arrival_price) × qty  [buys]
```

---

## Queue Position Visibility

Every GTC limit order an agent has resting in the book is described in `AgentState.queue_positions` before `step()` is called:

```python
def step(self, ts, view, state):
    for qp in state.queue_positions:
        print(f"Order {qp.order_id} at px={qp.price}: "
              f"ahead={qp.qty_ahead}, behind={qp.qty_behind}, "
              f"front={qp.is_front()}, frac={qp.queue_fraction():.2f}")
    return []
```

`queue_fraction()` = `qty_ahead / level_total`. 0 = at the front of the queue. Useful for adverse-selection avoidance: cancel before being run over when `is_front()` and the spread is narrowing.

Disable overhead when not needed:

```python
cfg = msim.WorldConfig()
cfg.track_queue_positions = False  # skip for pure market-order strategies
```

---

## Scenario Runner

Systematic strategy stress-testing across parameter grids and multiple seeds:

```python
from msim.scenario import ScenarioRunner, metrics_sf, metrics_tca

def factory(params):
    world = msim.World()
    world.prefill_book(mid=10_000, levels=20, qty=10)
    hawkes_cfg = msim.HawkesNoiseConfig()
    hawkes_cfg.p_market = params["p_market"]
    world.add_agent(msim.agents.HawkesNoiseTrader(owner_id=1, config=hawkes_cfg))
    world.add_agent(msim.agents.MarketMakerAS(owner_id=10))
    return world

runner = ScenarioRunner(
    world_factory = factory,
    param_grid    = {"p_market": [0.2, 0.4, 0.6]},
    metrics       = [metrics_sf, metrics_tca(owner_id=10)],
    n_seeds       = 50,
)
runner.run()
df = runner.summary_df()
# Returns mean, std, p5, p25, p50, p75, p95 per parameter combination
```

---

## Stylized Facts Measurement

Runs automatically after every `World::run()` (controlled by `WorldConfig::compute_stylized_facts`):

| Statistic | Description | Validation threshold |
|---|---|---|
| Excess kurtosis | Fat-tailed return distribution | > 1.0 |
| \|return\| autocorrelation | Volatility clustering | lag-1 AC > 0.05 |
| Trade-sign autocorrelation | Order-flow persistence | lag-1 AC > 0.10 |
| Kyle's lambda | Linear price impact (OLS) | lambda ≠ 0 |
| Time-weighted spread | Positive bid-ask spread | > 0 |

Also computes Amihud illiquidity, effective/realized spread decomposition, and power-law impact exponent.

```
=== MSIM Stylized Facts Report ===

Return Distribution (n=773):
  Std dev:         0.000995
  Excess kurtosis: 4.04  [OK — fat tails]

Autocorrelation (max_lag=20):
  Return AC lag-1:     -0.451  [bid-ask bounce, Roll 1984]
  |Return| AC lag-1:    0.323  [OK — vol clustering]
  Trade-sign AC lag-1:  0.270  [OK — flow autocorr, Bouchaud 2004]

Spread:
  Time-weighted spread:  8.86 ticks  [OK]
  Effective spread:      8.42 ticks

Validation: Fat tails PASS | Vol clustering PASS | Flow autocorr PASS |
            Positive spread PASS | Price impact INCONCLUSIVE (R²≈0.001)
```

---

## Per-Agent Latency Model

Every agent can be assigned an independent network latency distribution. When enabled, actions are processed in effective-arrival-time order (`ts + δ_i`), so lower-latency agents win execution priority.

| Distribution | Use case |
|---|---|
| `FIXED` | HFT colocation — constant sub-microsecond delay |
| `GAUSSIAN` | Easy to calibrate from exchange timestamps |
| `LOG_NORMAL` | Best empirical model for real network jitter |
| `UNIFORM` | Simple worst-case bound |

```python
cfg = msim.WorldConfig()
cfg.latency_enabled = True
cfg.latency_configs = [
    msim.LatencyDistConfig.fixed(500.0),              # HFT: 500 ns
    msim.LatencyDistConfig.lognormal(50_000, 10_000), # retail: ~50 µs
]
```

When `latency_enabled = False` (default), behaviour is byte-identical to the deterministic loop — zero overhead.

---

## Performance Configuration

Pre-reserve internal hash maps to eliminate rehashing during long simulations or scenario sweeps:

```python
cfg = msim.WorldConfig()
cfg.expected_resting_orders = 25_000   # eliminates order_meta_ rehashing
cfg.expected_fills          = 7_500    # pre-reserves fills vector
```

Output vectors are pre-reserved from `n_steps` at run start:
- `out.tops` — exactly `n_steps` entries, zero reallocations
- `out.fills` — reserved from `expected_fills * 2`
- `out.pnl_series` — reserved from `n_steps * n_agents`

---

## Run Modes

### 1) Python strategy interface

```python
import msim

world = msim.World()
world.prefill_book(mid=10_000, levels=20, qty=10)
world.add_agent(msim.agents.HawkesNoiseTrader(owner_id=1))
world.add_agent(msim.agents.MarketMakerAS(owner_id=2))

result = world.run(seed=42, horizon=2.0)
result.summary()
```

### 2) Offline CLI simulator (CSV outputs)

```bash
# args: <seed> <horizon_seconds>
./build/msim_cli 1 2.0
# Produces: trades.csv, top.csv
```

### 3) Live exchange gateway (local web UI)

```bash
./build/msim_gateway
# Open http://localhost:8080
# Type 'quit' in the terminal to stop cleanly
```

### 4) Multi-seed robustness study

```cmd
python src\python\multiseed_study.py
```

Runs 50 subprocess-isolated seeds and reports mean ± std with 95% bootstrap CIs for all five stylised facts. Results saved to `multiseed_results.csv` and `multiseed_summary.json`. Bootstrap CIs also computed by `src/python/bootstrap_ci.py`.

### 5) PnL conservation check

```cmd
python src\python\pnl_conservation_check.py
```

Verifies that total mark-to-market PnL, cash PnL, and net position sum to zero across all agents — confirming the simulation is a closed zero-sum system.

---

## Benchmarks

```bash
# Run full suite and generate plots
./build/msim_bench --benchmark_format=json --benchmark_out=bench.json
python tools/plot_bench_suite.py bench.json docs
python tools/plot_latency.py bench.json docs/latency_benchmark.png --prefix BM_ProcessMarketOrder
```

---

## Project Structure

```text
include/msim/
  flat_price_map.hpp              # Sorted-vector price map (O(1) front erase)
  flat_hash_map.hpp               # Robin Hood open-addressing hash map
  small_vector.hpp                # Inline-storage SmallVector<T,N>
  book.hpp                        # OrderBook (vector queue, live_count, FlatHashMap)
  matching_engine.hpp             # MatchingEngine with next_event_ts_ cache
  stylized_facts.hpp              # Stylized facts measurer (5 canonical checks)
  tca.hpp                         # FillRecord, StepSnapshot, AgentTCA
  world.hpp                       # World, IAgent, QueuePosition
  latency_model.hpp               # Per-agent latency distributions
  agents/
    noise_trader.hpp
    market_maker.hpp
    fundamental_value_agent.hpp   # Glosten-Milgrom informed trader
    momentum_agent.hpp            # MACD trend-follower
    noise_trader_hawkes.hpp       # Hawkes self-exciting noise trader
    market_maker_as.hpp           # Avellaneda-Stoikov optimal MM
    vwap_agent.hpp                # VWAP execution agent
    twap_agent.hpp                # TWAP execution agent
    is_agent.hpp                  # Almgren-Chriss IS agent
src/
  book.cpp
  matching_engine.cpp
  world.cpp
  python/msim_py.cpp              # pybind11 bindings
src/python/msim/
  __init__.py                     # Public Python API
  analysis.py                     # TCA analysis helpers
  execution.py                    # IS computation, compare_strategies
  scenario.py                     # ScenarioRunner, metric extractors
src/python/
  run_one_seed.py                 # Single-seed worker for multiseed_study
  multiseed_study.py              # 50-seed robustness study
  bootstrap_ci.py                 # Bootstrap and asymptotic CIs
tests/                            # GoogleTest suite (26 tests) + benchmarks
tools/                            # Benchmark plotting scripts
web/                              # Browser UI served by gateway
.github/workflows/                # CI: Linux (ASan/UBSan), macOS Debug/Release
```

---

## Engineering Notes

### v1.2.2 — PnL conservation verified + paper sync

- **Wealth conservation confirmed.** A full PnL audit (`src/python/pnl_conservation_check.py`) confirms the simulation is exactly zero-sum: total mark-to-market PnL, total cash PnL, and net position all sum to zero across all nine agents at every horizon. The system conserves wealth exactly.
- **Paper synchronisation.** All benchmark and validation numbers in `paper/msim_paper.tex` updated to match `paper/data/` raw outputs. Agent TCA table updated with current v1.2.1 values (all 9 agents, correct orders/fills/slippage/PnL). Kyle λ updated (−5.79, R²=0.0005). Spread decomposition updated (effective 9.89, realized +36.38, adverse −26.51 ticks). New subsection on emergent vs. idiosyncratic agent outcomes added.
- **README sync.** Volatility clustering corrected to 46/48 (96%), spread median to 8.37 ticks, sweep caption to 26.3 ns.

### v1.2.1 — Bug fixes

- **Fixed double-free heap corruption in Python bindings.** Agent objects registered with `std::unique_ptr<T>` as the pybind11 holder type were deleted both by the C++ `World` destructor and by Python's garbage collector. Fixed by registering all built-in agent classes with `py::nodelete`, making C++ the sole owner. This fix enables multi-seed simulation from a single Python process.
- **Added `aggressor_side` to `Trade`.** The matching engine now records whether each trade was buyer- or seller-initiated, exposed as `trade.aggressor_side` in Python (`'Buy'` or `'Sell'`). Also available in `result.trades_df()` as the `aggressor_side` column.
- **Fixed prefill order ID range.** `prefill_book` now uses IDs starting at `0xFFFF0000` to avoid collision with agent-generated order IDs in long simulation runs.

---

## Roadmap

1. ~~**Multi-seed scenario validation**~~ ✅ **DONE** — `src/python/multiseed_study.py` runs 50 subprocess-isolated seeds. Pass rates: fat tails 96% (46/48), vol clustering 96% (46/48), flow AC 100% (50/50), positive spread 98% (49/50). PnL conservation verified: total system PnL = 0 exactly.

2. **Unit tests for agent and TCA layer** — structured tests for `HawkesNoiseTrader`, `MarketMakerAS`, `VWAPAgent`, `ISAgent`, and the TCA computation pipeline.

3. **Gymnasium wrapper for RL research** — wrap `World` as a `gym.Env` so RL agents (PPO, SAC) can train against MSIM. Deterministic seeding makes runs directly comparable.

4. **Fuzz testing the matching engine** — LibFuzzer target covering auction uncrossing, FOK atomicity, and circuit breaker transition edge cases.

5. **numpy structured array output** — expose `trades`, `tops`, and `fills` as zero-copy numpy arrays for faster downstream pandas construction in large-scale sweeps.
