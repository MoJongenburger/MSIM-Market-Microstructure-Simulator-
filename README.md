# microstructure-sim
Event-driven **limit order book** + **matching engine** simulator in modern C++ (C++20), with microstructure metrics and execution agents.

## Build & Test
```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure

