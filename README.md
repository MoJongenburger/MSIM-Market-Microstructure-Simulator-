# MSIM — Market Microstructure Simulator (C++20)
<!-- Badges -->
![C++](https://img.shields.io/badge/C%2B%2B-20-blue)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-064F8C)
[![CI](https://github.com/MoJongenburger/MSIM-Market-Microstructure-Simulator-/actions/workflows/ci.yml/badge.svg)](https://github.com/MoJongenburger/MSIM-Market-Microstructure-Simulator-/actions/workflows/ci.yml)
![Latency](https://img.shields.io/badge/Latency-p50%2090.7ns-brightgreen)
![Tail](https://img.shields.io/badge/Tail-p99%2094.3ns-brightgreen)
![Throughput](https://img.shields.io/badge/Throughput-~10.5M%20ops%2Fs-blueviolet)
![License](https://img.shields.io/github/license/MoJongenburger/MSIM-Market-Microstructure-Simulator-)

MSIM is a deterministic, event-driven **limit order book + matching engine** written in modern **C++20**, built as a **microstructure research sandbox** for studying execution mechanics, venue rules, and agent interaction.

The project prioritizes **reproducibility, correctness, and extensibility**. Its architecture deliberately separates:

* **Core exchange mechanics** (order book, matching, trade printing)
* **Rule / policy logic** (admission checks, market phases, auctions, halts)
* **Agent layer** (informed traders, trend-followers, noise traders, market makers)
* **Measurement layer** (stylized facts validation against empirical regularities)

This keeps the "exchange kernel" small and testable while allowing realistic venue behaviour to be layered on without rewriting the matching engine.

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

## Why it's built this way

* **Deterministic replay**
  Microstructure experiments must be repeatable: the same event stream and seed should produce the same trades and book evolution. Every agent uses a splitmix64-derived seed so runs are bit-for-bit reproducible.

* **Separation of concerns**
  Matching is a narrow, correctness-critical component. Venue rules evolve quickly. MSIM keeps those concerns cleanly separated so new rules don't destabilize the core engine.

* **Invariants first**
  Exchange logic is easy to break in subtle ways (crossed book, incorrect FIFO, partial fill accounting). MSIM leans heavily on unit tests + CI to protect invariants.

* **Phase-aware market model**
  Real venues are session-driven: continuous trading, auctions (volatility/closing/reopen), and halts. MSIM treats "market phase" as a first-class concept integrated into processing.

* **Empirical validation built in**
  The stylized facts measurer runs automatically at the end of every simulation. It checks whether the generated microstructure data reproduces known empirical regularities — fat-tailed returns, volatility clustering, order-flow autocorrelation, and Glosten-Milgrom spread predictions — making it straightforward to assess whether a new agent configuration is calibrated to realistic behaviour.

* **Multiple "front-ends" for the same engine**
  MSIM supports both offline simulation (CSV outputs for analysis) and live interaction (web gateway + UI) without changing the exchange core.

---

## Features implemented

### Core book + matching

* Price–time priority **limit order book** (FIFO per price level)
* **Market** and **Limit** orders
* Partial fills and multi-level sweeps
* L2 depth snapshots (top-N levels) + top-of-book tracking
* **Cancel** and **reduce-only modify** (modify can only reduce quantity)

### Order instructions (exchange-style)

* **Time-in-Force:** GTC / IOC / FOK
  * IOC: execute immediately, remainder canceled
  * FOK: atomic — fills completely or does nothing
* **Market-to-Limit:** if a market order partially fills, the remainder becomes a resting limit at the last execution price

### Rule / policy layer (admission + governance)

* Structured rejects (reason codes): invalid order, market halted, tick size violations, lot size / min quantity violations
* Reference price tracking (last trade, with mid-price fallback)

### Market phases + auctions

* Phases: Continuous, Auction (uncrossing), Trading-at-Last, Closing Auction, Halted (circuit breaker)
* **Auction uncrossing** at a single clearing price: candidates derived from limit prices, maximize executable volume, tie-break by closest to reference price

### Stability mechanisms (real-venue inspired)

* **Price bands + volatility interruption:** band breach on first execution price → transition to auction
* **Circuit breaker halt + reopening auction:** large downward move triggers a halt; halt expires into a reopening auction with frozen book liquidity moved into the auction queue

### Self-Trade Prevention (STP)

* STP modes: None, CancelTaker, CancelMaker

### Agent-driven simulation (World + agents)

* Agent **World** wrapper around the matching engine
* Deterministic stepping with splitmix64-derived per-agent seeding
* **Agents implemented:**

  | Agent | File | Description |
  |---|---|---|
  | `NoiseTrader` | `agents/noise_trader.hpp` | Random market/limit order flow |
  | `MarketMaker` | `agents/market_maker.hpp` | Quotes around mid with inventory skew and periodic refresh |
  | `FundamentalValueAgent` | `agents/fundamental_value_agent.hpp` | Glosten-Milgrom informed trader with Ornstein-Uhlenbeck private signal |
  | `MomentumAgent` | `agents/momentum_agent.hpp` | MACD trend-follower with position limits |

* CI smoke test verifies deterministic behaviour for fixed seed

### Fundamental Value Agent (Glosten-Milgrom informed trader)

The `FundamentalValueAgent` models a trader with a private signal about fundamental value. The signal follows a discrete **Ornstein-Uhlenbeck** mean-reverting process:

```
V_{t+1} = V_t + κ(μ − V_t) + σ_v · Z,   Z ~ N(0,1)
```

The agent buys when `V_t − ask > threshold` (asset underpriced) and sells when `bid − V_t > threshold` (asset overpriced), submitting IOC market orders. This reproduces the informed-trader component of the Glosten-Milgrom (1985) spread model: adverse selection from informed order flow widens the equilibrium spread.

```cpp
FundamentalValueConfig cfg;
cfg.kappa     = 0.005;   // mean-reversion speed
cfg.sigma_v   = 1.5;     // signal volatility (ticks/step)
cfg.threshold = 1.0;     // mispricing required to trade (ticks)
cfg.lot_size  = 5;

world.add_agent(std::make_unique<FundamentalValueAgent>(owner_id, cfg));
```

### Momentum Agent (MACD trend-follower)

The `MomentumAgent` computes a MACD signal from exponential moving averages of the mid-price:

```
signal_t = EMA_fast(mid, α_f) − EMA_slow(mid, α_s),   α = 2/(N+1)
```

It enters long when `signal > entry_band`, enters short when `signal < −entry_band`, and flattens when `|signal| < exit_band`. Inventory is capped at `±max_position`. The agent reproduces the order-flow autocorrelation ("herding") component of stylized facts — momentum traders systematically generate autocorrelated trade signs on the same side.

```cpp
MomentumConfig cfg;
cfg.alpha_fast   = 2.0 / 6.0;    // 5-step EMA
cfg.alpha_slow   = 2.0 / 21.0;   // 20-step EMA
cfg.entry_band   = 0.30;
cfg.exit_band    = 0.05;
cfg.lot_size     = 3;
cfg.max_position = 15;

world.add_agent(std::make_unique<MomentumAgent>(owner_id, cfg));
```

### Per-Agent Latency Model

Every agent can be assigned an independent network latency distribution. When enabled, each action is stamped with a sampled delay `δ_i ~ F_i`, and all actions within a step are processed in effective-arrival-time order (`ts + δ_i`), so lower-latency agents win execution priority.

Four distributions are supported:

| Distribution | Use case |
|---|---|
| `FIXED` | HFT colocation — constant sub-microsecond delay |
| `GAUSSIAN` | Easy to calibrate from exchange timestamps |
| `LOG_NORMAL` | Best empirical model for real network jitter |
| `UNIFORM` | Simple worst-case bound |

```cpp
WorldConfig cfg;
cfg.latency_enabled = true;

// Provide one entry per agent, in registration order:
cfg.latency_configs = {
    {LatencyDistType::FIXED,      500.0},           // HFT: 500 ns fixed
    {LatencyDistType::LOG_NORMAL, 50'000.0, 10'000.0}, // retail: ~50 µs
};

// Or use the two-tier factory:
auto wlc = WorldLatencyConfig::two_tier(/*n_fast=*/2, /*n_slow=*/5);
cfg.latency_configs = wlc.agent_configs;
```

When `latency_enabled = false` (the default), behaviour is byte-identical to the original deterministic loop — zero overhead.

### Stylized Facts Measurement

The `StylizedFactsMeasurer` runs automatically at the end of every `World::run()` call (controlled by `WorldConfig::compute_stylized_facts`). It validates the simulation output against five canonical empirical regularities:

| Statistic | Description | Validation criterion |
|---|---|---|
| Excess kurtosis | Fat-tailed return distribution | `excess_kurtosis > 1.0` |
| `\|return\|` autocorrelation | Volatility clustering | `abs_return_ac[lag=1] > 0.05` |
| Trade-sign autocorrelation | Order-flow persistence | `\|sign_flow_ac[lag=1]\| > 0.10` |
| Kyle's lambda | Linear price impact (OLS) | `lambda ≠ 0` |
| Time-weighted spread | Positive bid-ask spread | `tw_spread > 0` |

It also computes Amihud illiquidity, effective/realized spread decomposition, and a log-log power-law impact exponent (theory predicts δ ≈ 0.5).

```cpp
WorldConfig cfg;
cfg.compute_stylized_facts = true;   // default: true

auto result = world.run(seed, horizon_s, cfg);

if (result.sf) {
    std::cout << StylizedFactsMeasurer::summary(*result.sf);
    // Write one-row CSV for batch experiments:
    std::ofstream f("sf.csv");
    f << StylizedFactsMeasurer::to_csv_header();
    f << StylizedFactsMeasurer::to_csv_row(*result.sf);
}
```

Example output:

```
=== MSIM Stylized Facts Report ===

Return Distribution (n=1847):
  Mean:            -0.000031 ticks
  Std dev:         0.00412 ticks
  Skewness:        -0.113
  Excess kurtosis: 3.87  [OK — fat tails]

Autocorrelation (max_lag=20):
  Return AC lag-1:     -0.021
  |Return| AC lag-1:   0.142  [OK — vol clustering]
  Trade-sign AC lag-1: 0.318  [OK — flow autocorr]

Price Impact:
  Kyle's lambda:   0.48 ticks/lot
  R²:              0.31
  Power exponent:  0.53  (theory ~0.5)

Validation:
  Fat tails:       PASS
  Vol clustering:  PASS
  Flow autocorr:   PASS
  Positive spread: PASS
  Nonzero impact:  PASS
```

### FV signal log

When `WorldConfig::record_fv_signals = true`, the private fundamental value `V_t` of every `FundamentalValueAgent` is recorded at each step into `WorldResult::fv_log`. This enables post-hoc analysis of the informed trader's signal relative to realized prices.

### Live gateway + web UI

* **`msim_gateway`** executable runs a live simulation loop and exposes a **local HTTP interface**
* A minimal **web UI** (served from `web/`) displays live top-of-book, live trades/price evolution, and the ability to send orders into the live book
* Designed so the UI talks to the gateway while the exchange core remains unchanged

### Engineering quality

* CMake targets: library + CLI + gateway + tests + benchmarks
* GoogleTest suite (core engine + new agents)
* Google Benchmark harness + plotting scripts
* CI across Linux / Windows / macOS + Linux sanitizers (ASan/UBSan)
* Optional warnings-as-errors builds

---

## Build & Test

### Configure + build

```bash
cmake -S . -B build
cmake --build build
```

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

---

## Run modes

### 1) Offline CLI simulator (CSV outputs)

```bash
# args: <seed> <horizon_seconds>
./build/msim_cli 1 2.0
```

Outputs:

* `trades.csv` — trade prints (id, timestamp, price, qty, maker/taker ids)
* `top.csv` — top-of-book evolution (timestamp, best bid/ask, mid)

### 2) Extended run with all four additions

```bash
./build/run_extended
```

Outputs:

* `trades_extended.csv` — all trade prints
* `tops_extended.csv` — top-of-book evolution
* `fv_signals.csv` — FundamentalValueAgent private signal log (if `record_fv_signals = true`)
* `stylized_facts.csv` — one-row summary for batch experiments

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

### Run dedicated latency plot (Release)

```bash
python tools/plot_latency.py bench.json docs/latency_benchmark.png --prefix BM_ProcessMarketOrder
```

---

## Project structure

```text
include/msim/                   # public headers
  agents/                       # agent interfaces + implementations
    noise_trader.hpp
    market_maker.hpp
    fundamental_value_agent.hpp # NEW: Glosten-Milgrom informed trader
    momentum_agent.hpp          # NEW: MACD trend-follower
  latency_model.hpp             # NEW: per-agent latency distributions
  stylized_facts.hpp            # NEW: stylized facts measurer
  world.hpp                     # World + IAgent + LatencyActionBuffer
src/                            # implementations (engine, world, gateway)
tests/                          # gtests + benchmarks
tools/                          # benchmark plotting scripts
web/                            # browser UI served by gateway
docs/                           # generated benchmark plots + summaries
.github/workflows/              # CI
```

---

## Roadmap (next)

1. **Zero-allocation depth snapshots (L2)**
   We will add `OrderBook::depth_into(side, levels, out_vec)` so the gateway reuses the same vector instead of allocating every poll.
   Files: `include/msim/book.hpp`, `src/book.cpp`, `src/gateway_main.cpp`, `web/app.js`

2. **Pre-reserve + stabilize hash map behaviour**
   `loc_` in `OrderBook` and `order_meta_/accounts_` in `World`: call `reserve()` + set `max_load_factor()` to reduce rehash jitter.
   Files: `src/book.cpp`, `src/world.cpp`

3. **Replace `std::map` levels with cache-friendlier levels**
   Switch price levels to a `flat_map` (or sorted `std::vector` of levels) for fewer cache misses.
   Files: `include/msim/book.hpp`, `src/book.cpp`

4. **Replace per-level `std::list<Order>` with a contiguous FIFO**
   E.g. an intrusive queue or deque/ring structure (still FIFO per price).
   Files: `include/msim/book.hpp`, `src/book.cpp`

5. **Stylized facts calibration loop**
   Add a CLI flag to run N independent seeds and output a CSV table of stylized fact scores per seed, enabling automated calibration of agent parameters to empirical targets.
   Files: `src/msim_cli.cpp`, `include/msim/stylized_facts.hpp`

6. **Benchmark methodology hardening**
   Pin benchmark thread/core (optional), print CPU info + compiler flags, store in `docs/`.
   Files: `tests/bench_engine.cpp`, new `docs/benchmark_methodology.md`
