# tools/plot_bench_suite.py
"""
Plot a benchmark "suite" from Google Benchmark JSON output.

What it generates (in out_dir):
  - latency_box_<BENCH>.png        : boxplot of per-iteration latency vs N
  - throughput_<BENCH>.png         : throughput vs N (items/sec if available, else ops/sec)
  - allocs_<BENCH>.png             : allocs/op vs N (if counter exists)
  - bench_summary.md               : markdown summary table (p50/p99 + throughput + allocs/op)

Usage (example):
  python tools/plot_bench_suite.py bench.json docs

Notes:
  - Supports names like:
      BM_Foo/100
      BM_Foo/100/repetition_12
      BM_Foo/100/repeats:50
  - Converts real_time to nanoseconds based on time_unit (ns/us/ms/s).
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
from collections import defaultdict
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

import matplotlib.pyplot as plt

try:
    import numpy as np
except Exception:  # pragma: no cover
    np = None  # type: ignore


# -----------------------------
# Parsing helpers
# -----------------------------

_TIME_UNIT_TO_NS = {
    "ns": 1.0,
    "us": 1_000.0,
    "ms": 1_000_000.0,
    "s": 1_000_000_000.0,
}


def _to_float(x: Any) -> Optional[float]:
    try:
        if x is None:
            return None
        return float(x)
    except Exception:
        return None


def _percentile(values: List[float], q: float) -> float:
    """Compute percentile with numpy if available; else use a simple sorted interpolation."""
    if not values:
        return float("nan")
    if np is not None:
        return float(np.percentile(np.array(values, dtype=float), q))
    # fallback
    xs = sorted(values)
    if len(xs) == 1:
        return float(xs[0])
    pos = (q / 100.0) * (len(xs) - 1)
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    if lo == hi:
        return float(xs[lo])
    w = pos - lo
    return float(xs[lo] * (1.0 - w) + xs[hi] * w)


def _sanitize_filename(s: str) -> str:
    s = re.sub(r"[^A-Za-z0-9_.-]+", "_", s)
    return s.strip("_") or "bench"


def _extract_base_and_n(name: str) -> Tuple[Optional[str], Optional[int]]:
    """
    Extract:
      base benchmark name (e.g. "BM_ProcessMarketOrder")
      N parameter (first integer after base/)

    Handles suffix tokens like "/repetition_12" or "/repeats:50".
    """
    # Split by '/'
    parts = name.split("/")
    if not parts:
        return None, None

    base = parts[0].strip()
    if not base:
        return None, None

    n_val: Optional[int] = None
    if len(parts) >= 2:
        m = re.match(r"^(\d+)", parts[1])
        if m:
            try:
                n_val = int(m.group(1))
            except Exception:
                n_val = None

    return base, n_val


def _is_aggregate_entry(name: str) -> bool:
    # Aggregate entries often include these tokens
    lowered = name.lower()
    return any(tok in lowered for tok in ["_mean", "_median", "_stddev", "_cv", "_p99", "_p95", "_min", "_max"])


@dataclass
class Sample:
    base: str
    n: int
    ns: float                 # latency per iteration in nanoseconds (real_time converted)
    throughput: Optional[float]  # items/sec or ops/sec
    allocs_per_op: Optional[float]


def _get_allocs_per_op(b: Dict[str, Any]) -> Optional[float]:
    # Google Benchmark stores counters either in top-level "counters" dict or in fields.
    counters = b.get("counters", None)
    if isinstance(counters, dict):
        v = _to_float(counters.get("allocs/op"))
        if v is not None:
            return v
    # fallback: sometimes flattened
    v2 = _to_float(b.get("allocs/op"))
    return v2


def _get_throughput(b: Dict[str, Any], ns: float) -> Optional[float]:
    """
    Prefer items_per_second if present, else fall back to ops/sec = 1e9 / ns.
    """
    ips = _to_float(b.get("items_per_second"))
    if ips is not None and ips > 0:
        return ips

    counters = b.get("counters", None)
    if isinstance(counters, dict):
        c_ips = _to_float(counters.get("items/sec"))
        if c_ips is not None and c_ips > 0:
            return c_ips

    # fallback to ops/sec (one iteration == one operation)
    if ns > 0:
        return 1e9 / ns
    return None


def load_samples(path: str, include_re: Optional[re.Pattern], exclude_re: Optional[re.Pattern]) -> List[Sample]:
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)

    benches = data.get("benchmarks", [])
    out: List[Sample] = []

    for b in benches:
        name = str(b.get("name", ""))
        if not name or _is_aggregate_entry(name):
            continue

        base, n = _extract_base_and_n(name)
        if base is None or n is None:
            continue

        if include_re is not None and include_re.search(base) is None:
            continue
        if exclude_re is not None and exclude_re.search(base) is not None:
            continue

        time_unit = str(b.get("time_unit", "ns"))
        mul = _TIME_UNIT_TO_NS.get(time_unit, None)
        rt = _to_float(b.get("real_time", None))
        if rt is None or mul is None:
            continue

        ns = rt * mul

        allocs = _get_allocs_per_op(b)
        thr = _get_throughput(b, ns)

        out.append(Sample(base=base, n=n, ns=ns, throughput=thr, allocs_per_op=allocs))

    return out


# -----------------------------
# Plotting
# -----------------------------

def ensure_dir(p: str) -> None:
    if p:
        os.makedirs(p, exist_ok=True)


def plot_latency_box(out_dir: str, base: str, grouped_ns: Dict[int, List[float]]) -> str:
    Ns = sorted(grouped_ns.keys())
    samples = [grouped_ns[n] for n in Ns]

    plt.figure(figsize=(11, 5))
    plt.boxplot(samples, tick_labels=[str(n) for n in Ns], showfliers=False)
    plt.title(f"Latency distribution (per-iteration) — {base}")
    plt.xlabel("Prefill resting orders (N)")
    plt.ylabel("Latency [ns]")
    plt.grid(True, axis="y", linestyle="--", alpha=0.3)
    plt.tight_layout()

    fn = os.path.join(out_dir, f"latency_box_{_sanitize_filename(base)}.png")
    plt.savefig(fn, dpi=200)
    plt.close()
    return fn


def plot_throughput(out_dir: str, base: str, grouped_thr: Dict[int, List[float]]) -> Optional[str]:
    # Use median throughput per N to avoid noisy lines
    Ns = sorted(grouped_thr.keys())
    if not Ns:
        return None

    ys = []
    for n in Ns:
        vals = grouped_thr[n]
        if not vals:
            ys.append(float("nan"))
        else:
            ys.append(_percentile(vals, 50))

    plt.figure(figsize=(11, 5))
    plt.plot(Ns, ys, marker="o")
    plt.title(f"Throughput — {base}")
    plt.xlabel("Prefill resting orders (N)")
    plt.ylabel("items/sec (if available) else ops/sec")
    plt.grid(True, linestyle="--", alpha=0.3)
    plt.tight_layout()

    fn = os.path.join(out_dir, f"throughput_{_sanitize_filename(base)}.png")
    plt.savefig(fn, dpi=200)
    plt.close()
    return fn


def plot_allocs(out_dir: str, base: str, grouped_allocs: Dict[int, List[float]]) -> Optional[str]:
    Ns = sorted(grouped_allocs.keys())
    if not Ns:
        return None

    ys = []
    for n in Ns:
        vals = grouped_allocs[n]
        if not vals:
            ys.append(float("nan"))
        else:
            ys.append(_percentile(vals, 50))

    plt.figure(figsize=(11, 5))
    plt.plot(Ns, ys, marker="o")
    plt.title(f"allocs/op (median) — {base}")
    plt.xlabel("Prefill resting orders (N)")
    plt.ylabel("allocs/op")
    plt.grid(True, linestyle="--", alpha=0.3)
    plt.tight_layout()

    fn = os.path.join(out_dir, f"allocs_{_sanitize_filename(base)}.png")
    plt.savefig(fn, dpi=200)
    plt.close()
    return fn


def write_summary_md(out_dir: str, by_base: Dict[str, Dict[int, Dict[str, List[float]]]]) -> str:
    """
    Creates a markdown table with p50/p99 latency, p50 throughput, p50 allocs/op for each (base, N).
    """
    lines: List[str] = []
    lines.append("# Benchmark summary\n")
    lines.append("| Benchmark | N | p50 latency (ns) | p99 latency (ns) | p50 throughput (items/sec or ops/sec) | p50 allocs/op | reps |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|")

    for base in sorted(by_base.keys()):
        for n in sorted(by_base[base].keys()):
            g = by_base[base][n]
            ns_list = g.get("ns", [])
            thr_list = g.get("thr", [])
            al_list = g.get("alloc", [])

            p50 = _percentile(ns_list, 50) if ns_list else float("nan")
            p99 = _percentile(ns_list, 99) if ns_list else float("nan")
            t50 = _percentile(thr_list, 50) if thr_list else float("nan")
            a50 = _percentile(al_list, 50) if al_list else float("nan")

            reps = len(ns_list)

            def fmt(x: float, nd: int = 3) -> str:
                if not math.isfinite(x):
                    return "—"
                # keep ns as integer-ish
                if nd == 0:
                    return str(int(round(x)))
                return f"{x:.{nd}f}"

            lines.append(
                f"| `{base}` | {n} | {fmt(p50,0)} | {fmt(p99,0)} | {fmt(t50,3)} | {fmt(a50,6)} | {reps} |"
            )

    fn = os.path.join(out_dir, "bench_summary.md")
    with open(fn, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return fn


# -----------------------------
# Main
# -----------------------------

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("bench_json", help="Google Benchmark JSON output (e.g. bench.json)")
    ap.add_argument("out_dir", help="Output directory for plots (e.g. docs)")
    ap.add_argument("--include", default=r"^BM_", help="Regex to include benchmark base names (default: '^BM_')")
    ap.add_argument("--exclude", default="", help="Regex to exclude benchmark base names")
    ap.add_argument("--max_benches", type=int, default=0, help="Limit number of benchmarks plotted (0 = no limit)")
    args = ap.parse_args()

    include_re = re.compile(args.include) if args.include else None
    exclude_re = re.compile(args.exclude) if args.exclude else None

    ensure_dir(args.out_dir)

    samples = load_samples(args.bench_json, include_re, exclude_re)
    if not samples:
        raise SystemExit("No benchmark samples found. Did you run with --benchmark_format=json and include repetitions?")

    # Structure:
    # by_base[base][n]["ns"|"thr"|"alloc"] -> list
    by_base: Dict[str, Dict[int, Dict[str, List[float]]]] = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))

    for s in samples:
        by_base[s.base][s.n]["ns"].append(float(s.ns))
        if s.throughput is not None:
            by_base[s.base][s.n]["thr"].append(float(s.throughput))
        if s.allocs_per_op is not None:
            by_base[s.base][s.n]["alloc"].append(float(s.allocs_per_op))

    # Optional limit
    bases = sorted(by_base.keys())
    if args.max_benches and args.max_benches > 0:
        bases = bases[: args.max_benches]

    written = []
    for base in bases:
        grouped_ns = {n: by_base[base][n]["ns"] for n in by_base[base]}
        grouped_thr = {n: by_base[base][n]["thr"] for n in by_base[base] if by_base[base][n]["thr"]}
        grouped_alloc = {n: by_base[base][n]["alloc"] for n in by_base[base] if by_base[base][n]["alloc"]}

        written.append(plot_latency_box(args.out_dir, base, grouped_ns))

        tplot = plot_throughput(args.out_dir, base, grouped_thr)
        if tplot:
            written.append(tplot)

        aplot = plot_allocs(args.out_dir, base, grouped_alloc)
        if aplot:
            written.append(aplot)

    summary = write_summary_md(args.out_dir, by_base)
    written.append(summary)

    print("Wrote:")
    for p in written:
        print(" ", p)

    # Console summary (quick, human-readable)
    print("\nQuick stats (p50/p99 ns):")
    for base in bases:
        Ns = sorted(by_base[base].keys())
        if not Ns:
            continue
        # show the largest N as headline
        n = Ns[-1]
        xs = by_base[base][n]["ns"]
        p50 = _percentile(xs, 50)
        p99 = _percentile(xs, 99)
        print(f"  {base:28s} N={n:6d}  p50={p50:7.1f} ns  p99={p99:7.1f} ns  reps={len(xs)}")


if __name__ == "__main__":
    main()
