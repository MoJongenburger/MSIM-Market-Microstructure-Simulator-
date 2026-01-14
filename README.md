# MSIM — Market Microstructure Simulator (C++20)

MSIM is a deterministic, event-driven **limit order book + matching engine** written in modern **C++20**, built as a **microstructure research sandbox** for studying execution mechanics, venue rules, and agent interaction.

The project prioritizes **reproducibility, correctness, and extensibility**. Its architecture deliberately separates:

- **Core exchange mechanics** (order book, matching, trade printing)
- **Rule / policy logic** (admission checks, market phases, auctions, halts)

This keeps the “exchange kernel” small and testable while allowing realistic venue behavior to be layered on without rewriting the matching engine.

---

## Performance (Latency Proof)

MSIM ships with a benchmark suite (Google Benchmark) plus plotting tools that generate latency, throughput, and allocation figures.

### Hot-path microbenchmark (Release, warm book; Google Benchmark)

Headline (from the benchmark suite on a warm book):

- `engine.process()` median (**p50**) ≈ **~90–95 ns**  
- `engine.process()` tail (**p99**) ≈ **~94–110 ns**  
- Measured with **N = 100–10,000** prefilled resting orders (multiple repetitions)

<img width="2420" height="1100" alt="latency_benchmark" src="https://github.com/user-attachments/assets/7b499e66-8139-47e6-90e8-3ccc1af73379" />

> Interpretation: this is a tight “hot path” benchmark for the matching call with a warmed order book. It is intended to provide objective, reproducible latency evidence (not a full end-to-end trading stack measurement).

### Benchmark suite outputs (latency + throughput + allocs)

The suite produces per-operation plots:

- Market order processing (latency/throughput/allocations)
- Crossing limit IOC processing
- Resting limit insertion
- L2 depth snapshot extraction

Example generated plots (kept under `docs/`):

- `docs/latency_box_BM_ProcessMarketOrder.png`
- `docs/throughput_BM_ProcessMarketOrder.png`
- `docs/allocs_BM_ProcessMarketOrder.png`

---

## Why it’s built this way

- **Deterministic replay**  
  Microstructure experiments must be repeatable: the same event stream and seed should produce the same trades and book evolution.

- **Separation of concerns**  
  Matching is a narrow, correctness-critical component. Venue rules evolve quickly. MSIM keeps those concerns cleanly separated so new rules don’t destabilize the core engine.

- **Invariants first**  
  Exchange logic is easy to break in subtle ways (crossed book, incorrect FIFO, partial fill accounting). MSIM leans heavily on unit tests + CI to protect invariants.

- **Phase-aware market model**  
  Real venues are session-driven: continuous trading, auctions (volatility/closing/reopen), and halts. MSIM treats “market phase” as a first-class concept integrated into processing.

- **Multiple “front-ends” for the same engine**  
  MSIM supports both:
  - offline simulation (CSV outputs for analysis), and
  - live interaction (web gateway + UI),
  without changing the exchange core.

---

## Features implemented

### Core book + matching
- Price–time priority **limit order book** (FIFO per price level)
- **Market** and **Limit** orders
- Partial fills and multi-level sweeps
- L2 depth snapshots (top-N levels) + top-of-book tracking
- **Cancel** and **reduce-only modify** (modify can only reduce quantity)

### Order instructions (exchange-style)
- **Time-in-Force:** GTC / IOC / FOK  
  - IOC: execute immediately, remainder canceled  
  - FOK: atomic — fills completely or does nothing
- **Market-to-Limit:** if a market order partially fills, the remainder becomes a resting limit at the last execution price

### Rule / policy layer (admission + governance)
- Structured rejects (reason codes):
  - invalid order
  - market halted (configurable: reject vs queue)
  - tick size violations
  - lot size / min quantity violations
- Reference price tracking (last trade, with mid-price fallback)

### Market phases + auctions
- Phases supported and integrated into processing:
  - Continuous
  - Auction (uncrossing)
  - Trading-at-Last
  - Closing Auction
  - Halted (circuit breaker)
- **Auction uncrossing** at a single clearing price:
  - candidates derived from limit prices
  - maximize executable volume
  - tie-break by closest to reference price

### Stability mechanisms (real-venue inspired)
- **Price bands + volatility interruption foundation**
  - band breach on first execution price → transition to auction
- **Circuit breaker halt + reopening auction**
  - large downward move triggers a halt
  - halt expires into a reopening auction (book liquidity moved into auction queue)

### Self-Trade Prevention (STP)
- STP modes:
  - None
  - CancelTaker
  - CancelMaker

### Agent-driven simulation (World + agents)
- Agent **World** wrapper around the matching engine
- Deterministic stepping with seeded RNG
- Agents implemented:
  - **NoiseTrader** (`msim::agents`) producing random market/limit flow
  - **MarketMaker** (`msim`) quoting around mid with periodic refresh and inventory skew
- CI smoke test verifies deterministic behavior for fixed seed

### Live gateway + web UI
- **`msim_gateway`** executable runs a live simulation loop and exposes a **local HTTP interface**
- A minimal **web UI** (served from `web/`) displays:
  - live top-of-book
  - live trades / price evolution (foundation)
  - ability to send orders into the live book (foundation)
- Designed so the UI talks to the gateway while the exchange core remains unchanged

### Engineering quality
- CMake targets: library + CLI + gateway + tests + benchmarks
- GoogleTest suite
- Google Benchmark harness + plotting scripts
- CI across Linux / Windows / macOS + Linux sanitizers (ASan/UBSan)
- Optional warnings-as-errors builds

---

## Build & Test

### Configure + build
```bash
cmake -S . -B build
cmake --build build
````

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

* Live candlestick chart + multiple time windows (1D / 1W / 1M / 3M / 6M / 1Y)
* WebSocket streaming (push updates instead of polling)
* User orders: track open orders, fills, and per-user PnL in the UI
* Latency model + message rate limits
* Stop / stop-limit orders
* Iceberg orders
* Additional benchmarks + profiling + performance charts

