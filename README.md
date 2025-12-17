# MSIM — Market Microstructure Simulator (C++20)

MSIM is a deterministic, event-driven **limit order book + matching engine** written in modern **C++20**, built as a **microstructure research sandbox** for studying execution mechanics, venue rules, and agent interaction.

The project prioritizes **reproducibility, correctness, and extensibility**. Its architecture deliberately separates:
- **Core exchange mechanics** (order book, matching, trade printing)
- **Rule / policy logic** (admission checks, market phases, auctions, halts)

This keeps the “exchange kernel” small and testable while allowing realistic venue behavior to be layered on without rewriting the matching engine.

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

### Agent-driven simulation (step 16+)
- Agent **World** wrapper around the matching engine
- Deterministic stepping with seeded RNG
- Agents implemented:
  - **NoiseTrader** (`msim::agents`) producing random market/limit flow
  - **MarketMaker** (`msim`) quoting around mid with periodic refresh
- CI smoke test verifies deterministic behavior for fixed seed

### Engineering quality
- CMake targets: library + CLI + tests
- GoogleTest suite
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

### Run the CLI simulator

```bash
# args: <seed> <horizon_seconds>
./build/msim_cli 1 2.0
```

Outputs:

* `trades.csv` — trade prints (id, timestamp, price, qty, maker/taker ids)
* `top.csv` — top-of-book evolution (timestamp, best bid/ask, mid)

---

## Project structure

```text
include/msim/         # public headers
  agents/             # agent interfaces + implementations (NoiseTrader, etc.)
src/                  # implementations
tests/                # gtests
.github/workflows/    # CI
```

---

## Roadmap (next)

* O(1) cancel/modify via order-id index (locator map)
* Latency model + message rate limits
* Stop / stop-limit orders
* Iceberg orders
* Benchmarks + profiling + performance charts

```
::contentReference[oaicite:0]{index=0}
```
