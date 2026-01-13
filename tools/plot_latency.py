import argparse
import json
import os
import re
from collections import defaultdict

import matplotlib.pyplot as plt

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bench_json", help="Google Benchmark JSON output")
    ap.add_argument("out_png", help="Output PNG path (e.g. docs/latency_benchmark.png)")
    ap.add_argument("--prefix", default="BM_ProcessMarketOrder", help="Benchmark name prefix to plot")
    args = ap.parse_args()

    with open(args.bench_json, "r", encoding="utf-8") as f:
        data = json.load(f)

    # Google benchmark repetition entries look like:
    # "BM_ProcessMarketOrder/100/repeats:50"
    # or "BM_ProcessMarketOrder/100/repetition_12"
    # depending on version/flags.
    benches = data.get("benchmarks", [])
    groups = defaultdict(list)  # N -> [real_time_us]

    for b in benches:
        name = b.get("name", "")
        if not name.startswith(args.prefix + "/"):
            continue

        # Extract N as the first integer after prefix/
        m = re.match(rf"^{re.escape(args.prefix)}/(\d+)", name)
        if not m:
            continue
        N = int(m.group(1))

        # real_time is in the unit specified; we asked microseconds in C++ harness
        rt = b.get("real_time", None)
        if rt is None:
            continue

        groups[N].append(float(rt))

    if not groups:
        raise SystemExit(f"No benchmark samples found for prefix '{args.prefix}'. "
                         f"Did you run with --benchmark_format=json and repetitions?")

    Ns = sorted(groups.keys())
    samples = [groups[n] for n in Ns]

    # Ensure output dir exists
    out_dir = os.path.dirname(args.out_png)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    plt.figure(figsize=(11, 5))
    plt.boxplot(samples, labels=[str(n) for n in Ns], showfliers=False)
    plt.title(f"Latency distribution (per repetition) — {args.prefix}")
    plt.xlabel("Prefill resting orders (N)")
    plt.ylabel("Time per engine.process() [microseconds]")
    plt.grid(True, axis="y", linestyle="--", alpha=0.3)
    plt.tight_layout()
    plt.savefig(args.out_png, dpi=200)
    print(f"Wrote: {args.out_png}")

    # Print quick stats (median, p99)
    import numpy as np
    print("\nSummary:")
    for n in Ns:
        arr = np.array(groups[n], dtype=float)
        p50 = float(np.percentile(arr, 50))
        p99 = float(np.percentile(arr, 99))
        print(f"  N={n:5d}  p50={p50:8.3f} us  p99={p99:8.3f} us  reps={len(arr)}")

if __name__ == "__main__":
    main()
