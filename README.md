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

This keeps the “exchange kernel” small and testable while allowing realistic venue behavior to be layered on without rewriting the matching engine.

---

## Performance (Latency Proof)

MSIM ships with a benchmark suite (Google Benchmark) plus plotting tools that generate latency, throughput, and allocation figures.

### Hot-path microbenchmark (Release, warm book; Google Benchmark)

market order processing on a warm book:

* `BM_ProcessMarketOrder` @ **N=10,000** resting orders: **p50 = 90.7 ns**, **p99 = 94.3 ns** (reps=29)

Per-N tail stats for the same benchmark (from `plot_latency.py`):

| Prefill N | p50 (ns) | p90 (ns) | p99 (ns) | reps |
| --------: | -------: | -------: | -------: | ---: |
|       100 |   91.103 |   95.929 |  268.177 |   29 |
|     1,000 |   91.558 |   97.184 |  297.521 |   29 |
|    10,000 |   90.734 |   92.008 |   94.277 |   29 |

<img width="2420" height="1100" alt="latency_benchmark" src="https://github.com/user-attachments/assets/97da4718-5376-45eb-951a-5948072e6e0c" />
> Interpretation: this is a tight “hot path” benchmark for the matching call with a warmed order book. It is intended to provide objective, reproducible latency evidence (not a full end-to-end trading stack measurement).


### Market-data extraction latency: 
L2 depth snapshot scaling (Top-N). This demonstrates that MSIM can publish depth quickly and predictably (important for UIs and downstream agents).

<img width="2200" height="1000" alt="latency_box_BM_BookDepth_TopN" src="https://github.com/user-attachments/assets/cac89335-77fb-40c8-a8c5-bb3625ed802b" />


### Benchmark suite outputs (latency + throughput + allocs)

The suite produces per-operation plots:

* Market order processing (latency/throughput/allocations)
* Crossing limit IOC processing
* Resting limit insertion
* L2 depth snapshot extraction

Example generated plots (kept under `docs/`):

* `docs/latency_box_BM_ProcessMarketOrder.png`
* `docs/throughput_BM_ProcessMarketOrder.png`
* `docs/allocs_BM_ProcessMarketOrder.png`

---

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

> Note: “allocs/op” plots come from a benchmark-only global allocation counter (see `docs/allocs_*.png`). These plots help validate whether the critical path stays allocation-light under the tested workloads.

---

## Why it’s built this way

* **Deterministic replay**
  Microstructure experiments must be repeatable: the same event stream and seed should produce the same trades and book evolution.

* **Separation of concerns**
  Matching is a narrow, correctness-critical component. Venue rules evolve quickly. MSIM keeps those concerns cleanly separated so new rules don’t destabilize the core engine.

* **Invariants first**
  Exchange logic is easy to break in subtle ways (crossed book, incorrect FIFO, partial fill accounting). MSIM leans heavily on unit tests + CI to protect invariants.

* **Phase-aware market model**
  Real venues are session-driven: continuous trading, auctions (volatility/closing/reopen), and halts. MSIM treats “market phase” as a first-class concept integrated into processing.

* **Multiple “front-ends” for the same engine**
  MSIM supports both:

  * offline simulation (CSV outputs for analysis), and
  * live interaction (web gateway + UI),
    without changing the exchange core.

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

* Structured rejects (reason codes):

  * invalid order
  * market halted (configurable: reject vs queue)
  * tick size violations
  * lot size / min quantity violations
* Reference price tracking (last trade, with mid-price fallback)

### Market phases + auctions

* Phases supported and integrated into processing:

  * Continuous
  * Auction (uncrossing)
  * Trading-at-Last
  * Closing Auction
  * Halted (circuit breaker)
* **Auction uncrossing** at a single clearing price:

  * candidates derived from limit prices
  * maximize executable volume
  * tie-break by closest to reference price

### Stability mechanisms (real-venue inspired)

* **Price bands + volatility interruption foundation**

  * band breach on first execution price → transition to auction
* **Circuit breaker halt + reopening auction**

  * large downward move triggers a halt
  * halt expires into a reopening auction (book liquidity moved into auction queue)

### Self-Trade Prevention (STP)

* STP modes:

  * None
  * CancelTaker
  * CancelMaker

### Agent-driven simulation (World + agents)

* Agent **World** wrapper around the matching engine
* Deterministic stepping with seeded RNG
* Agents implemented:

  * **NoiseTrader** (`msim::agents`) producing random market/limit flow
  * **MarketMaker** (`msim`) quoting around mid with periodic refresh and inventory skew
* CI smoke test verifies deterministic behavior for fixed seed

### Live gateway + web UI

* **`msim_gateway`** executable runs a live simulation loop and exposes a **local HTTP interface**
* A minimal **web UI** (served from `web/`) displays:

  * live top-of-book
  * live trades / price evolution (foundation)
  * ability to send orders into the live book (foundation)
* Designed so the UI talks to the gateway while the exchange core remains unchanged

### Engineering quality

* CMake targets: library + CLI + gateway + tests + benchmarks
* GoogleTest suite
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

### 2) Live exchange gateway (local web UI)

```bash
./build/msim_gateway
```

Open:

* `http://localhost:8080`

To stop the gateway cleanly, type `quit` (or `exit`) in the terminal where it’s running.

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

(Optional outputs):

```bash
python tools/plot_latency.py bench.json docs/latency_benchmark.png --prefix BM_ProcessMarketOrder --out_summary docs/latency_summary.json --out_md docs/latency_snippet.md
```

---

## Project structure

```text
include/msim/         # public headers
  agents/             # agent interfaces + implementations (NoiseTrader, etc.)
src/                  # implementations (engine, world, gateway)
tests/                # gtests + benchmarks
tools/                # benchmark plotting scripts
web/                  # browser UI served by gateway
docs/                 # generated benchmark plots + summaries
.github/workflows/    # CI
```

---

## Roadmap (next)

1. **Zero-allocation depth snapshots (L2)**

* Add `OrderBook::depth_into(side, levels, out_vec)` so the gateway reuses the same vector instead of allocating every poll.
* Files: `include/msim/book.hpp`, `src/book.cpp`, `src/gateway_main.cpp`, `web/app.js`

2. **Pre-reserve + stabilize hash map behavior**

* `loc_` in `OrderBook` and `order_meta_/accounts_` in `World`: call `reserve()` + set `max_load_factor()` to reduce rehash jitter.
* Files: `src/book.cpp` (where loc_ grows) / constructor area, `src/world.cpp`

3. **Replace `std::map` levels with cache-friendlier levels**

* Switch price levels to a `flat_map` (or sorted `std::vector` of levels) for fewer cache misses.
* Files: `include/msim/book.hpp`, `src/book.cpp`

4. **Replace per-level `std::list<Order>` with a contiguous FIFO**

* E.g. an intrusive queue or deque/ring structure (still FIFO per price).
* Files: `include/msim/book.hpp`, `src/book.cpp`

5. **Benchmark methodology hardening**

* Pin benchmark thread/core (optional), print CPU info + compiler flags, store in `docs/`.
* Files: `tests/bench_engine.cpp`, new `docs/benchmark_methodology.md`
