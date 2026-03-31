# MSIM — Market Microstructure Simulator (C++20)

<!-- Badges -->
![C++](https://img.shields.io/badge/C%2B%2B-20-blue)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-064F8C)
[![CI](https://github.com/MoJongenburger/MSIM-Market-Microstructure-Simulator-/actions/workflows/ci.yml/badge.svg)](https://github.com/MoJongenburger/MSIM-Market-Microstructure-Simulator-/actions/workflows/ci.yml)
![Latency](https://img.shields.io/badge/Latency-p50%2090.7ns-brightgreen)
![Tail](https://img.shields.io/badge/Tail-p99%2094.3ns-brightgreen)
![Throughput](https://img.shields.io/badge/Throughput-~10.5M%20ops%2Fs-blueviolet)
![License](https://img.shields.io/github/license/MoJongenburger/MSIM-Market-Microstructure-Simulator-)

MSIM is a deterministic, event-driven **limit order book + matching engine** written in modern **C++20**, built as a **microstructure research sandbox** for studying execution mechanics, venue rules, and agent interaction. It ships with a full **Python strategy interface** via pybind11, so strategies can be written in Python against the same sub-100ns C++ engine.

The project prioritizes **reproducibility, correctness, and extensibility**. Its architecture deliberately separates:

* **Core exchange mechanics** (order book, matching, trade printing)
* **Rule / policy logic** (admission checks, market phases, auctions, halts)
* **Agent layer** (informed traders, trend-followers, noise traders, market makers, execution algorithms)
* **Measurement layer** (stylized facts, TCA, PnL series, queue position tracking)

This keeps the "exchange kernel" small and testable while allowing realistic venue behavior to be layered on without rewriting the matching engine.

---

## Performance (Latency Proof)

MSIM ships with a benchmark suite (Google Benchmark) plus plotting tools that generate latency, throughput, and allocation figures.

### Hot-path microbenchmark (Release, warm book; Google Benchmark)

Market order processing on a warm book:

* `BM_ProcessMarketOrder` @ **N=10,000** resting orders: **p50 = 90.7 ns**, **p99 = 94.3 ns** (reps=29)

Per-N tail stats for the same benchmark (from `plot_latency.py`):

| Prefill N | p50 (ns) | p90 (ns) | p99 (ns) | reps |
| --------: | -------: | -------: | -------: | ---: |
|       100 |   91.103 |   95.929 |  268.177 |   29 |
|     1,000 |   91.558 |   97.184 |  297.521 |   29 |
|    10,000 |   90.734 |   92.008 |   94.277 |   29 |

<img width="2420" height="1100" alt="latency_benchmark" src="https://github.com/user-attachments/assets/97da4718-5376-45eb-951a-5948072e6e0c" />

> Interpretation: this is a tight "hot path" benchmark for the matching call with a warmed order book. It is intended to provide objective, reproducible latency evidence (not a full end-to-end trading stack measurement).

### Market-data extraction latency

L2 depth snapshot scaling (Top-N). This demonstrates that MSIM can publish depth quickly and predictably (important for UIs and downstream agents).

<img width="2200" height="1000" alt="latency_box_BM_BookDepth_TopN" src="https://github.com/user-attachments/assets/cac89335-77fb-40c8-a8c5-bb3625ed802b" />

### Benchmark suite summary (p50/p99)

Quick stats (from `plot_bench_suite.py`) — **all in nanoseconds**:

| Benchmark                          | Parameter | p50 (ns) | p99 (ns) | reps | What it measures                                           |
| ---------------------------------- | --------: | -------: | -------: | ---: | ---------------------------------------------------------- |
| `BM_BookAddRestingLimit`           |  N=10,000 |    218.7 |    294.4 |   10 | Insert a resting limit (book add path)                     |
| `BM_BookDepth_TopN`                |     N=128 |     85.8 |     93.0 |   25 | L2 snapshot extraction cost (Top-N)                        |
| `BM_ProcessCrossingLimitIOC`       |  N=10,000 |    101.2 |    116.9 |   10 | Crossing limit IOC (limit order that executes immediately) |
| `BM_ProcessMarketOrder`            |  N=10,000 |     90.7 |     94.3 |   25 | Market order hot path (`engine.process`)                   |
| `BM_ProcessMarket_SweepKLevels`    |   K=1,024 |     32.6 |     34.6 |   25 | Market order sweeping multiple price levels (K-level walk) |
| `BM_Throughput_ProcessMarketOrder` |  N=10,000 |     96.2 |     99.9 |   10 | Throughput-oriented market order loop                      |

> Note: "allocs/op" plots come from a benchmark-only global allocation counter (see `docs/allocs_*.png`). These plots help validate whether the critical path stays allocation-light under the tested workloads.

---

## Python Quick Start

```bash
# Build with Python bindings
cmake -S . -B build -DMSIM_BUILD_PYTHON=ON
cmake --build build --target _msim_core

# Install the Python package (editable)
pip install -e ".[analysis]"
```

```python
import msim

# Create a world with a pre-filled symmetric book
world = msim.make_world(mid=10_000, levels=20, qty=10)

# Register agents
world.add_agent(msim.agents.HawkesNoiseTrader(owner_id=1))
world.add_agent(msim.agents.MarketMakerAS(owner_id=2))

# Run 2 seconds of simulation
result = world.run(seed=42, horizon=2.0)

# Inspect outputs
print(result.trades_df().head())     # all trades
print(result.tca_df())               # per-agent TCA summary
result.summary()                     # stylized facts report
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
        # Check queue position of any resting orders
        for qp in state.queue_positions:
            if qp.is_front() and view.spread() and view.spread() <= 2:
                # At front of queue, spread is thin — potential adverse selection
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

## Why it's built this way

* **Deterministic replay**
  Microstructure experiments must be repeatable: the same event stream and seed produce bit-for-bit identical trades, fills, and book evolution. Every agent uses a splitmix64-derived seed. The scenario runner uses the same derivation so 1000-seed sweeps are fully reproducible.

* **Separation of concerns**
  Matching is a narrow, correctness-critical component. Venue rules evolve quickly. MSIM keeps those concerns cleanly separated so new rules don't destabilize the core engine.

* **Invariants first**
  Exchange logic is easy to break in subtle ways (crossed book, incorrect FIFO, partial fill accounting). MSIM leans heavily on unit tests + CI to protect invariants.

* **Phase-aware market model**
  Real venues are session-driven: continuous trading, auctions (volatility/closing/reopen), and halts. MSIM treats "market phase" as a first-class concept integrated into processing.

* **Empirical validation built in**
  The stylized facts measurer runs automatically at the end of every simulation. It checks whether the generated data reproduces five canonical empirical regularities: fat-tailed returns, volatility clustering, order-flow autocorrelation, Glosten-Milgrom spread prediction, and nonzero Kyle's lambda.

* **Python-first research interface**
  The C++ engine is the performance foundation; the Python layer is the research interface. Researchers write strategies in Python, query TCA outputs as DataFrames, sweep parameters with the scenario runner, and benchmark execution algorithms — all without touching C++.

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
* **Auction uncrossing** at a single clearing price: maximize executable volume, tie-break by closest to reference price
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
| `QueueAwareMarketMaker` | `QueueAwareMMConfig` | A-S market maker with queue-position cancel logic |

### Hawkes Noise Trader

Replaces flat Poisson arrival with a self-exciting process:

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

Quote bid at `r − δ*/2`, ask at `r + δ*/2`.

### Implementation Shortfall Agent (Almgren-Chriss)

Executes the optimal trajectory minimising `E[IS] + λ·Var[IS]`:

```
x_k = X · sinh(κ(T−k)) / sinh(κT),   κ = √(λ·σ²/η)
```

With `λ=0` degenerates to TWAP. High `λ` front-loads execution.

---

## TCA / PnL Output Layer

Every `World::run()` call now returns structured execution analytics alongside raw trades.

### Per-fill records

```python
result.fills_df()
# Columns: ts, owner, order_id, side, fill_qty, fill_price,
#          arrival_mid, is_maker, slippage
```

`slippage` = signed deviation from the mid-price at order submission. Positive = paid above mid (market impact cost). Negative = filled inside mid (limit order edge).

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

# Compare TWAP vs VWAP vs IS across one simulation
results = {
    "TWAP":  r_twap,
    "VWAP":  r_vwap,
    "IS":    r_is,
}
comparison = ex.compare_strategies(results, owner_ids={...})
print(comparison[["is_ticks", "is_bps", "completion_rate"]])

# Plot cumulative execution curves
ex.plot_execution(r_is, owner_id=52, label="IS (λ=0.01)")
ex.plot_is_comparison(comparison)
```

Implementation Shortfall vs arrival price:
```
IS (ticks) = (avg_fill_price − arrival_price) × qty  [buys]
```
Positive IS = paid above arrival price.

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

`queue_fraction()` = `qty_ahead / level_total`. 0 = at the front of the queue. Useful for adverseselection avoidance: if `is_front()` and the spread is narrowing, cancel before being adversely selected.

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
    w = msim.make_world(mid=10_000)
    cfg = msim.HawkesNoiseConfig()
    cfg.hawkes.mu = params["hawkes_mu"]
    w.add_agent(msim.agents.HawkesNoiseTrader(owner_id=1, config=cfg))
    w.add_agent(msim.agents.MarketMakerAS(owner_id=10))
    return w

runner = ScenarioRunner(
    world_factory = factory,
    param_grid    = {"hawkes_mu": [5.0, 10.0, 20.0],
                     "mm_gamma":  [0.005, 0.01]},
    metrics       = [metrics_sf, metrics_tca(owner_id=10)],
    n_seeds       = 50,
    n_workers     = 4,
)
runner.run()
df = runner.summary_df()
# Returns mean, std, p5, p25, p50, p75, p95 per parameter combination
```

Built-in metric extractors: `metrics_sf`, `metrics_tca(owner_id)`, `metrics_all_tca`, `metrics_execution_is(owner_id)`.

For large sweeps, use the performance-tuned config:

```python
from msim.scenario_perf import sweep_config, fast_config

runner = ScenarioRunner(
    factory, grid, metrics,
    n_seeds      = 200,
    world_config = fast_config(n_agents=5, horizon_seconds=1.0),
)
```

`fast_config()` disables fills, pnl_series, queue tracking, and pre-reserves all hash maps — eliminating rehashing entirely during 1000-seed sweeps.

---

## Performance Improvements

Hash map rehashing was the dominant cost in scenario sweeps. All six `unordered_map`s in `World` now use `max_load_factor(0.5)` + `reserve()` before the simulation loop.

```python
cfg = msim.WorldConfig()
cfg.capacity.expected_orders = 25_000   # eliminates all rehashing
cfg.capacity.expected_trades = 7_500    # pre-reserves fills vector
```

Output vectors are pre-reserved from `n_steps` (computed once at run start):
- `out.tops` — exactly `n_steps` entries, zero reallocations
- `out.fills` — reserved from `expected_trades * 2`
- `out.pnl_series` — reserved from `n_steps * n_agents`

The `OrderBook::reserve(n)` method pre-reserves the book's internal `loc_` map with the same load factor.

---

## Stylized Facts Measurement

Runs automatically after every `World::run()` (controlled by `WorldConfig::compute_stylized_facts`):

| Statistic | Description | Validation criterion |
|---|---|---|
| Excess kurtosis | Fat-tailed return distribution | `excess_kurtosis > 1.0` |
| `\|return\|` autocorrelation | Volatility clustering | `abs_return_ac[lag=1] > 0.05` |
| Trade-sign autocorrelation | Order-flow persistence | `\|sign_flow_ac[lag=1]\| > 0.10` |
| Kyle's lambda | Linear price impact (OLS) | `lambda ≠ 0` |
| Time-weighted spread | Positive bid-ask spread | `tw_spread > 0` |

Also computes Amihud illiquidity, effective/realized spread decomposition, and power-law impact exponent.

```
=== MSIM Stylized Facts Report ===

Return Distribution (n=1847):
  Mean:            -0.000031 ticks
  Std dev:         0.00412 ticks
  Excess kurtosis: 3.87  [OK — fat tails]

Autocorrelation (max_lag=20):
  |Return| AC lag-1:   0.142  [OK — vol clustering]
  Trade-sign AC lag-1: 0.318  [OK — flow autocorr]

Price Impact:
  Kyle's lambda:   0.48 ticks/lot
  Power exponent:  0.53  (theory ~0.5)

Validation: Fat tails PASS | Vol clustering PASS | Flow autocorr PASS |
            Positive spread PASS | Nonzero impact PASS
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

When `latency_enabled = False` (default), behavior is byte-identical to the original deterministic loop — zero overhead.

---

## Build & Test

### C++ library + CLI + tests

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

### With Python bindings

```bash
cmake -S . -B build -DMSIM_BUILD_PYTHON=ON
cmake --build build --target _msim_core
pip install -e ".[analysis]"
```

---

## Run modes

### 1) Python strategy interface

```python
import msim

result = msim.quick_run(
    msim.agents.HawkesNoiseTrader(owner_id=1),
    msim.agents.MarketMakerAS(owner_id=2),
    seed=42, horizon=2.0
)
result.summary()
```

### 2) Offline CLI simulator (CSV outputs)

```bash
# args: <seed> <horizon_seconds>
./build/msim_cli 1 2.0
```

Outputs: `trades.csv`, `top.csv`

### 3) Live exchange gateway (local web UI)

```bash
./build/msim_gateway
```

Open `http://localhost:8080`. To stop cleanly, type `quit` in the terminal.

---

## Benchmarks

### Run benchmark suite (Release) + generate plots

```bash
./build/msim_bench --benchmark_format=json --benchmark_out=bench.json
python tools/plot_bench_suite.py bench.json docs
```

### Run dedicated latency plot

```bash
python tools/plot_latency.py bench.json docs/latency_benchmark.png --prefix BM_ProcessMarketOrder
```

---

## Project structure

```text
include/msim/
  agents/
    noise_trader.hpp
    market_maker.hpp
    fundamental_value_agent.hpp     # Glosten-Milgrom informed trader
    momentum_agent.hpp              # MACD trend-follower
    noise_trader_hawkes.hpp         # Hawkes self-exciting noise trader
    market_maker_as.hpp             # Avellaneda-Stoikov optimal MM
    multi_asset_fv_agent.hpp        # Correlated multi-asset informed trader
    vwap_agent.hpp                  # VWAP execution agent
    twap_agent.hpp                  # TWAP execution agent
    is_agent.hpp                    # Almgren-Chriss IS agent
    queue_aware_market_maker.hpp    # MM with queue-position cancel logic
  hawkes_process.hpp                # Hawkes process primitive
  shared_fundamental.hpp            # Correlated OU signal (multi-asset)
  tca.hpp                           # FillRecord, StepSnapshot, AgentTCA
  world.hpp                         # World, IAgent, QueuePosition, CapacityHints
  latency_model.hpp                 # Per-agent latency distributions
  stylized_facts.hpp                # Stylized facts measurer
  book.hpp                          # OrderBook with reserve() and queue_info()
src/
  world.cpp                         # Run loop, TCA, queue tracking, perf reserves
  book.cpp                          # OrderBook incl. queue_info() implementation
  python/
    msim_py.cpp                     # pybind11 bindings
pyproject.toml                      # Python package (scikit-build-core)
python/
  msim/
    __init__.py                     # Public Python API
    analysis.py                     # TCA analysis helpers, dashboard
    execution.py                    # IS computation, compare_strategies
    scenario.py                     # ScenarioRunner, metric extractors
    scenario_perf.py                # fast_config, sweep_config, benchmark
  examples/
    quickstart.py
    exec_quickstart.py
    scenario_quickstart.py
tests/                              # GoogleTest suite + benchmarks
tools/                              # Benchmark plotting scripts
web/                                # Browser UI served by gateway
docs/                               # Generated benchmark plots + summaries
.github/workflows/                  # CI (Linux sanitizers, macOS Debug/Release)
```

---

## Roadmap (next)

1. **Unit tests for new agents and TCA layer**
   Structured tests for `HawkesNoiseTrader`, `MarketMakerAS`, `VWAPAgent`, `ISAgent`, and the TCA computation pipeline. Files: `tests/`.

2. **Gymnasium wrapper for RL research**
   Wrap `World` as a `gym.Env` so RL agents (PPO, SAC) can train against MSIM. The deterministic seeding makes this directly comparable across runs. Files: `python/msim/gym_env.py`.

3. **Fuzz testing the matching engine**
   LibFuzzer target covering the auction uncrossing, FOK atomicity, and circuit breaker transition edge cases. Files: `tests/fuzz_engine.cpp`.

4. **Replace `std::map` price levels with a sorted `std::vector`**
   For a typical LOB with < 200 active price levels, a cache-resident sorted vector beats pointer-chasing through a red-black tree. Files: `include/msim/book.hpp`, `src/book.cpp`.

5. **Replace per-level `std::list<Order>` with a contiguous FIFO**
   Eliminates one heap allocation per resting order. Files: `include/msim/book.hpp`, `src/book.cpp`.
