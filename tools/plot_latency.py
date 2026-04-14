import argparse
import json
import os
import re
from collections import defaultdict

import matplotlib.pyplot as plt
import numpy as np


def infer_time_unit(data: dict, default: str = "ns") -> str:
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
    if unit == "ns": return 1e-9
    if unit == "us": return 1e-6
    if unit == "ms": return 1e-3
    if unit == "s":  return 1.0
    return 1e-9


def convert(value: float, from_unit: str, to_unit: str) -> float:
    return float(value) * (unit_to_seconds(from_unit) / unit_to_seconds(to_unit))


def extract_N(name: str, prefix: str):
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
    ap.add_argument("out_png",    help="Output PNG path (e.g. docs/latency_benchmark.png)")
    ap.add_argument("--prefix",     default="BM_ProcessMarketOrder",
                                    help="Benchmark name prefix to plot")
    ap.add_argument("--single_n",   type=int, default=None,
                                    help="Plot only this N value as a single box (e.g. --single_n 10000)")
    ap.add_argument("--logy",       action="store_true", help="Use log scale on Y axis")
    ap.add_argument("--unit",       default="", help="Force output unit: ns/us/ms/s")
    ap.add_argument("--out_summary",default="", help="Optional path to write summary JSON")
    ap.add_argument("--out_md",     default="", help="Optional path to write README markdown snippet")
    args = ap.parse_args()

    with open(args.bench_json, "r", encoding="utf-8") as f:
        data = json.load(f)

    benches  = data.get("benchmarks", [])
    json_unit = infer_time_unit(data, default="ns")
    out_unit  = (args.unit.strip().lower() if args.unit else json_unit.lower().strip())

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
        entry_unit = str(b.get("time_unit", "") or json_unit).strip().lower() or json_unit
        groups[N].append(convert(float(rt), entry_unit, out_unit))

    if not groups:
        raise SystemExit(f"No samples found for prefix '{args.prefix}'.")

    # ── Filter to single N if requested ──────────────────────────────────────
    if args.single_n is not None:
        if args.single_n not in groups:
            available = sorted(groups.keys())
            raise SystemExit(
                f"--single_n {args.single_n} not found. "
                f"Available N values: {available}"
            )
        Ns      = [args.single_n]
        samples = [groups[args.single_n]]
        xlabel  = f"N = {args.single_n} resting orders"
    else:
        Ns      = sorted(groups.keys())
        samples = [groups[n] for n in Ns]
        xlabel  = "Prefill resting orders (N)"

    ensure_dir(args.out_png)

    # ── Plot ──────────────────────────────────────────────────────────────────
    fig_w = 5 if args.single_n is not None else 11
    plt.figure(figsize=(fig_w, 5))
    plt.boxplot(samples, tick_labels=[str(n) for n in Ns], showfliers=False)
    plt.title(f"Latency distribution (per repetition) — {args.prefix}")
    plt.xlabel(xlabel)
    plt.ylabel(f"Time per operation [{out_unit}]")
    plt.grid(True, axis="y", linestyle="--", alpha=0.3)
    if args.logy:
        plt.yscale("log")
        plt.ylabel(f"Time per operation [{out_unit}, log scale]")
    plt.tight_layout()
    plt.savefig(args.out_png, dpi=220)
    print(f"Wrote: {args.out_png}")

    # ── Summary stats ─────────────────────────────────────────────────────────
    summary = {"prefix": args.prefix, "unit_plotted": out_unit,
               "original_time_unit": json_unit, "series": []}
    print("\nSummary:")
    for n in Ns:
        arr = np.array(groups[n], dtype=float)
        p50, p90, p99 = (float(np.percentile(arr, p)) for p in (50, 90, 99))
        reps = int(arr.size)
        summary["series"].append({"N": int(n), "reps": reps,
            f"p50_{out_unit}": p50, f"p90_{out_unit}": p90,
            f"p99_{out_unit}": p99})
        print(f"  N={n:5d}  p50={p50:10.3f} {out_unit}  "
              f"p90={p90:10.3f} {out_unit}  p99={p99:10.3f} {out_unit}  reps={reps}")

    if args.out_summary:
        ensure_dir(args.out_summary)
        with open(args.out_summary, "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=2)
        print(f"Wrote: {args.out_summary}")

    if args.out_md:
        ensure_dir(args.out_md)
        pick = next((s for s in summary["series"] if s["N"] == 1000),
                    summary["series"][len(summary["series"]) // 2])
        md = [f"### Latency (Google Benchmark)\n\n",
              f"![Latency benchmark]({args.out_png})\n\n",
              f"Headline (N={pick['N']}): **p50={pick[f'p50_{out_unit}']:.1f} {out_unit}**, "
              f"**p99={pick[f'p99_{out_unit}']:.1f} {out_unit}** (reps={pick['reps']}).\n\n",
              f"| N | reps | p50 ({out_unit}) | p90 ({out_unit}) | p99 ({out_unit}) |\n",
              "|---:|---:|---:|---:|---:|\n"]
        for s in summary["series"]:
            md.append(f"| {s['N']} | {s['reps']} | {s[f'p50_{out_unit}']:.1f} | "
                      f"{s[f'p90_{out_unit}']:.1f} | {s[f'p99_{out_unit}']:.1f} |\n")
        with open(args.out_md, "w", encoding="utf-8") as f:
            f.write("".join(md))
        print(f"Wrote: {args.out_md}")


if __name__ == "__main__":
    main()
