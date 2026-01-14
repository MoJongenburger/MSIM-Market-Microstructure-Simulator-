import argparse
import json
import os
import re
from collections import defaultdict

import matplotlib.pyplot as plt
import numpy as np


def _infer_time_unit(benches, default="us"):
    """
    Try to infer the time unit from the JSON.
    Google Benchmark JSON may include: time_unit: "ns"/"us"/"ms"/"s".
    If absent, assume default (your harness uses microseconds).
    """
    # Top-level sometimes has "context": {"time_unit": "..."} depending on version
    ctx = benches.get("context") if isinstance(benches, dict) else None
    if isinstance(ctx, dict) and "time_unit" in ctx:
        return ctx["time_unit"]

    # Or each benchmark entry may have "time_unit"
    if isinstance(benches, dict) and "benchmarks" in benches:
        for b in benches["benchmarks"]:
            if "time_unit" in b and b["time_unit"]:
                return b["time_unit"]

    return default


def _unit_to_us_multiplier(unit: str) -> float:
    unit = unit.lower().strip()
    if unit == "ns":
        return 1.0 / 1000.0
    if unit == "us":
        return 1.0
    if unit == "ms":
        return 1000.0
    if unit == "s":
        return 1_000_000.0
    # unknown -> assume us
    return 1.0


def _extract_N(name: str, prefix: str):
    # Extract first integer after "<prefix>/"
    m = re.match(rf"^{re.escape(prefix)}/(\d+)", name)
    if not m:
        return None
    return int(m.group(1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bench_json", help="Google Benchmark JSON output")
    ap.add_argument("out_png", help="Output PNG path (e.g. docs/latency_benchmark.png)")
    ap.add_argument("--prefix", default="BM_ProcessMarketOrder", help="Benchmark name prefix to plot")
    ap.add_argument("--logy", action="store_true", help="Use log scale on Y axis")
    ap.add_argument("--out_summary", default="", help="Optional path to write summary JSON")
    ap.add_argument("--out_md", default="", help="Optional path to write a README-ready markdown snippet")
    args = ap.parse_args()

    with open(args.bench_json, "r", encoding="utf-8") as f:
        data = json.load(f)

    benches = data.get("benchmarks", [])
    if not isinstance(benches, list):
        raise SystemExit("Unexpected benchmark JSON structure: expected data['benchmarks'] to be a list.")

    # Determine unit (fallback = us as in your benchmark config)
    unit = data.get("context", {}).get("time_unit", "us")
    # Some benchmark JSON versions might not include it there — infer
    if not unit:
        unit = "us"
    mult_to_us = _unit_to_us_multiplier(unit)

    # Group samples: N -> [real_time_us]
    groups = defaultdict(list)

    for b in benches:
        name = b.get("name", "")
        if not name.startswith(args.prefix + "/"):
            continue

        N = _extract_N(name, args.prefix)
        if N is None:
            continue

        rt = b.get("real_time", None)
        if rt is None:
            continue

        # Convert whatever unit the JSON is in into microseconds for plotting
        rt_us = float(rt) * mult_to_us
        groups[N].append(rt_us)

    if not groups:
        raise SystemExit(
            f"No benchmark samples found for prefix '{args.prefix}'. "
            f"Did you run with --benchmark_format=json and repetitions?"
        )

    Ns = sorted(groups.keys())
    samples = [groups[n] for n in Ns]

    # Ensure output dir exists
    out_dir = os.path.dirname(args.out_png)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    # --- Plot ---
    plt.figure(figsize=(11, 5))
    # Matplotlib 3.9+: "labels" renamed to "tick_labels"
    plt.boxplot(samples, tick_labels=[str(n) for n in Ns], showfliers=False)

    plt.title(f"Latency distribution (per repetition) — {args.prefix}")
    plt.xlabel("Prefill resting orders (N)")
    plt.ylabel("Time per engine.process() [microseconds]")
    plt.grid(True, axis="y", linestyle="--", alpha=0.3)

    if args.logy:
        plt.yscale("log")
        plt.ylabel("Time per engine.process() [microseconds, log scale]")

    plt.tight_layout()
    plt.savefig(args.out_png, dpi=220)
    print(f"Wrote: {args.out_png}")

    # --- Summary stats ---
    summary = {
        "prefix": args.prefix,
        "unit_plotted": "us",
        "original_time_unit": unit,
        "series": [],
    }

    print("\nSummary:")
    for n in Ns:
        arr = np.array(groups[n], dtype=float)
        p50 = float(np.percentile(arr, 50))
        p90 = float(np.percentile(arr, 90))
        p99 = float(np.percentile(arr, 99))
        mn = float(np.min(arr))
        mx = float(np.max(arr))
        reps = int(arr.size)

        summary["series"].append(
            {
                "N": int(n),
                "reps": reps,
                "min_us": mn,
                "p50_us": p50,
                "p90_us": p90,
                "p99_us": p99,
                "max_us": mx,
            }
        )

        print(f"  N={n:5d}  p50={p50:8.3f} us  p90={p90:8.3f} us  p99={p99:8.3f} us  reps={reps}")

    # Optional: write summary json
    if args.out_summary:
        out_dir2 = os.path.dirname(args.out_summary)
        if out_dir2:
            os.makedirs(out_dir2, exist_ok=True)
        with open(args.out_summary, "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=2)
        print(f"Wrote: {args.out_summary}")

    # Optional: write a README snippet
    if args.out_md:
        out_dir3 = os.path.dirname(args.out_md)
        if out_dir3:
            os.makedirs(out_dir3, exist_ok=True)

        # choose the "middle" N for headline stats (or N=1000 if present)
        pick = None
        for s in summary["series"]:
            if s["N"] == 1000:
                pick = s
                break
        if pick is None:
            pick = summary["series"][len(summary["series"]) // 2]

        md = []
        md.append(f"### Latency (Google Benchmark, hot-path)\n")
        md.append(f"![Latency benchmark]({args.out_png})\n")
        md.append(
            f"Headline (N={pick['N']}): **p50={pick['p50_us']:.3f} µs**, "
            f"**p99={pick['p99_us']:.3f} µs** (reps={pick['reps']}).\n"
        )
        md.append("\n")
        md.append("| N (prefill) | reps | p50 (µs) | p90 (µs) | p99 (µs) |\n")
        md.append("|---:|---:|---:|---:|---:|\n")
        for s in summary["series"]:
            md.append(
                f"| {s['N']} | {s['reps']} | {s['p50_us']:.3f} | {s['p90_us']:.3f} | {s['p99_us']:.3f} |\n"
            )

        with open(args.out_md, "w", encoding="utf-8") as f:
            f.write("".join(md))
        print(f"Wrote: {args.out_md}")


if __name__ == "__main__":
    main()
