import argparse
import json
import os
import re
from collections import defaultdict

import matplotlib.pyplot as plt
import numpy as np


def infer_time_unit(data: dict, default: str = "ns") -> str:
    """
    Infer time unit from Google Benchmark JSON.
    Common locations:
      - data["context"]["time_unit"]
      - each entry: b["time_unit"]
    If missing, default to ns (your suite plots indicate ns output).
    """
    ctx = data.get("context", {})
    if isinstance(ctx, dict):
        tu = ctx.get("time_unit")
        if isinstance(tu, str) and tu.strip():
            return tu.strip()

    benches = data.get("benchmarks", [])
    if isinstance(benches, list):
        for b in benches:
            tu = b.get("time_unit")
            if isinstance(tu, str) and tu.strip():
                return tu.strip()

    return default


def unit_to_seconds(unit: str) -> float:
    unit = unit.lower().strip()
    if unit == "ns":
        return 1e-9
    if unit == "us":
        return 1e-6
    if unit == "ms":
        return 1e-3
    if unit == "s":
        return 1.0
    # unknown -> assume ns
    return 1e-9


def convert(value: float, from_unit: str, to_unit: str) -> float:
    """
    Convert numeric value from from_unit -> to_unit via seconds.
    """
    from_s = unit_to_seconds(from_unit)
    to_s = unit_to_seconds(to_unit)
    # value * from_s = seconds, / to_s = new unit
    return float(value) * (from_s / to_s)


def extract_N(name: str, prefix: str):
    """
    Extract first integer after '<prefix>/'.
    Example: BM_ProcessMarketOrder/1000/...
    """
    m = re.match(rf"^{re.escape(prefix)}/(\d+)", name)
    if not m:
        return None
    return int(m.group(1))


def ensure_dir(path: str):
    d = os.path.dirname(path)
    if d:
        os.makedirs(d, exist_ok=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bench_json", help="Google Benchmark JSON output")
    ap.add_argument("out_png", help="Output PNG path (e.g. docs/latency_benchmark.png)")
    ap.add_argument("--prefix", default="BM_ProcessMarketOrder", help="Benchmark name prefix to plot")
    ap.add_argument("--logy", action="store_true", help="Use log scale on Y axis")
    ap.add_argument("--unit", default="", help="Force output unit: ns/us/ms/s (default: keep JSON unit)")
    ap.add_argument("--out_summary", default="", help="Optional path to write summary JSON")
    ap.add_argument("--out_md", default="", help="Optional path to write a README-ready markdown snippet")
    args = ap.parse_args()

    with open(args.bench_json, "r", encoding="utf-8") as f:
        data = json.load(f)

    benches = data.get("benchmarks", [])
    if not isinstance(benches, list):
        raise SystemExit("Unexpected benchmark JSON structure: expected data['benchmarks'] to be a list.")

    json_unit = infer_time_unit(data, default="ns")
    out_unit = (args.unit.strip().lower() if args.unit else json_unit.lower().strip())

    # Group samples: N -> [real_time in out_unit]
    groups = defaultdict(list)

    for b in benches:
        name = b.get("name", "")
        if not name.startswith(args.prefix + "/"):
            continue

        N = extract_N(name, args.prefix)
        if N is None:
            continue

        rt = b.get("real_time", None)
        if rt is None:
            continue

        # Use per-entry unit if present, else json_unit
        entry_unit = b.get("time_unit", "") or json_unit
        entry_unit = str(entry_unit).strip().lower() if entry_unit else json_unit

        rt_out = convert(float(rt), entry_unit, out_unit)
        groups[N].append(rt_out)

    if not groups:
        raise SystemExit(
            f"No benchmark samples found for prefix '{args.prefix}'. "
            f"Did you run with --benchmark_format=json and repetitions?"
        )

    Ns = sorted(groups.keys())
    samples = [groups[n] for n in Ns]

    ensure_dir(args.out_png)

    # --- Plot ---
    plt.figure(figsize=(11, 5))
    plt.boxplot(samples, tick_labels=[str(n) for n in Ns], showfliers=False)

    plt.title(f"Latency distribution (per repetition) — {args.prefix}")
    plt.xlabel("Prefill resting orders (N)")
    plt.ylabel(f"Time per operation [{out_unit}]")
    plt.grid(True, axis="y", linestyle="--", alpha=0.3)

    if args.logy:
        plt.yscale("log")
        plt.ylabel(f"Time per operation [{out_unit}, log scale]")

    plt.tight_layout()
    plt.savefig(args.out_png, dpi=220)
    print(f"Wrote: {args.out_png}")

    # --- Summary stats ---
    summary = {
        "prefix": args.prefix,
        "unit_plotted": out_unit,
        "original_time_unit": json_unit,
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
                f"min_{out_unit}": mn,
                f"p50_{out_unit}": p50,
                f"p90_{out_unit}": p90,
                f"p99_{out_unit}": p99,
                f"max_{out_unit}": mx,
            }
        )

        print(f"  N={n:5d}  p50={p50:10.3f} {out_unit}  p90={p90:10.3f} {out_unit}  p99={p99:10.3f} {out_unit}  reps={reps}")

    # Optional: write summary json
    if args.out_summary:
        ensure_dir(args.out_summary)
        with open(args.out_summary, "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=2)
        print(f"Wrote: {args.out_summary}")

    # Optional: write a README snippet
    if args.out_md:
        ensure_dir(args.out_md)

        # pick N=1000 if present, else middle
        pick = None
        for s in summary["series"]:
            if s["N"] == 1000:
                pick = s
                break
        if pick is None:
            pick = summary["series"][len(summary["series"]) // 2]

        p50_key = f"p50_{out_unit}"
        p99_key = f"p99_{out_unit}"

        md = []
        md.append("### Latency (Google Benchmark)\n\n")
        md.append(f"![Latency benchmark]({args.out_png})\n\n")
        md.append(
            f"Headline (N={pick['N']}): **p50={pick[p50_key]:.1f} {out_unit}**, "
            f"**p99={pick[p99_key]:.1f} {out_unit}** (reps={pick['reps']}).\n\n"
        )
        md.append(f"| N (prefill) | reps | p50 ({out_unit}) | p90 ({out_unit}) | p99 ({out_unit}) |\n")
        md.append("|---:|---:|---:|---:|---:|\n")
        for s in summary["series"]:
            md.append(
                f"| {s['N']} | {s['reps']} | {s[f'p50_{out_unit}']:.1f} | {s[f'p90_{out_unit}']:.1f} | {s[f'p99_{out_unit}']:.1f} |\n"
            )

        with open(args.out_md, "w", encoding="utf-8") as f:
            f.write("".join(md))
        print(f"Wrote: {args.out_md}")


if __name__ == "__main__":
    main()
