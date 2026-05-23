#!/usr/bin/env python3
"""Run the OptFSST(12) buildDP-variant compression-speed benchmark over data/refined.

Pins all binaries to a single core (taskset) and runs them sequentially on
the same input file so that timings are comparable. Each binary emits one CSV
row per file to stdout; rows are appended to the output CSV.

Four variants are measured:
    opt      : 8-bit OptFSST  with the production unrolled+hinted buildDP
    naive    : 8-bit OptFSST  with the plain-loop buildDP (BUILDDP_NAIVE=1)
    opt12    : 12-bit OptFSST12 with the production unrolled+hinted buildDP
    naive12  : 12-bit OptFSST12 with the plain-loop buildDP

After the long-form CSV is written, a second CSV with the per-file speedup
(naive_total_ms / opt_total_ms) is derived for plotting -- one entry per file
for each of OptFSST and OptFSST12.

Usage:
    run_bench.py --root /home/hedi/btrfsst/data/refined \\
                 --bench-opt      ../build-dp-bench/dp_bench_opt \\
                 --bench-naive    ../build-dp-bench/dp_bench_naive \\
                 --bench-opt12    ../build-dp-bench/dp_bench_opt12 \\
                 --bench-naive12  ../build-dp-bench/dp_bench_naive12 \\
                 --out            csv/dp_buildDP_variants.csv \\
                 --speedup-out    csv/dp_buildDP_speedup.csv \\
                 [--reps 5] [--max-bytes 268435456] [--cpu 0]
"""
from __future__ import annotations

import argparse
import csv
import shlex
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional

CSV_HEADER = [
    "variant",
    "file",
    "n_lines",
    "raw_bytes",
    "compressed_bytes",
    "create_ms_mean",
    "compress_ms_mean",
    "total_ms_mean",
    "create_mb_per_s_mean",
    "compress_mb_per_s_mean",
    "total_mb_per_s_mean",
]

SKIP_SUFFIXES = {".pdf", ".png", ".pptx", ".mp4", ".db", ".csv", ".fsst"}

# Pair-up table for the speedup derivation. Each entry is
# (naive_variant_tag, opt_variant_tag, plot_label).
SPEEDUP_PAIRS = [
    ("naive",   "opt",   "OptFSST"),
    ("naive12", "opt12", "OptFSST12"),
]


def discover_corpora(root: Path) -> list[Path]:
    out: list[Path] = []
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        if path.suffix.lower() in SKIP_SUFFIXES:
            continue
        out.append(path)
    return out


def run_one(bench_bin: Path, input_file: Path, variant_tag: str, reps: int,
            cpu: Optional[int], timeout_s: int) -> Optional[str]:
    cmd = [str(bench_bin), str(input_file), variant_tag, str(reps)]
    if cpu is not None:
        cmd = ["taskset", "-c", str(cpu)] + cmd
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout_s)
    except subprocess.TimeoutExpired:
        sys.stderr.write(f"  TIMEOUT {variant_tag} on {input_file}\n")
        return None
    if proc.returncode != 0:
        sys.stderr.write(
            f"  FAIL {variant_tag} on {input_file}: rc={proc.returncode}\n"
            f"    cmd={' '.join(shlex.quote(x) for x in cmd)}\n"
            f"    stderr={proc.stderr.strip()[:400]}\n"
        )
        return None
    lines = proc.stdout.strip().splitlines()
    if not lines:
        return None
    return lines[-1]


def derive_speedup(variants_csv: Path, speedup_csv: Path) -> None:
    """Pivot the per-(variant, file) CSV into a per-(codec, file) speedup CSV.

    Output columns: configuration, Speedup, file -- where Speedup is
    naive_total_ms / opt_total_ms (>1 means the optimisation makes the
    end-to-end compression faster).
    """
    by_variant_file: dict[tuple[str, str], dict[str, str]] = {}
    with variants_csv.open(newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            by_variant_file[(row["variant"], row["file"])] = row

    out_rows: list[tuple[str, float, str]] = []
    files = sorted({k[1] for k in by_variant_file})
    for file in files:
        for naive_tag, opt_tag, label in SPEEDUP_PAIRS:
            naive_row = by_variant_file.get((naive_tag, file))
            opt_row = by_variant_file.get((opt_tag, file))
            if naive_row is None or opt_row is None:
                continue
            try:
                naive_total = float(naive_row["total_ms_mean"])
                opt_total = float(opt_row["total_ms_mean"])
            except (KeyError, ValueError):
                continue
            if opt_total <= 0.0:
                continue
            out_rows.append((label, naive_total / opt_total, file))

    speedup_csv.parent.mkdir(parents=True, exist_ok=True)
    with speedup_csv.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["configuration", "Speedup", "file"])
        for label, speedup, file in out_rows:
            writer.writerow([label, f"{speedup:.6f}", file])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True, type=Path)
    ap.add_argument("--bench-opt", required=True, type=Path)
    ap.add_argument("--bench-naive", required=True, type=Path)
    ap.add_argument("--bench-opt12", required=True, type=Path)
    ap.add_argument("--bench-naive12", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--speedup-out", required=True, type=Path)
    ap.add_argument("--reps", type=int, default=5)
    ap.add_argument(
        "--max-bytes", type=int, default=256 * 1024 * 1024,
        help="skip files larger than this (guardrail against pathological inputs)",
    )
    ap.add_argument("--cpu", type=int, default=0,
                    help="pin to this logical CPU (use -1 to disable)")
    ap.add_argument("--timeout", type=int, default=600)
    ap.add_argument("--limit", type=int, default=0,
                    help="only run first N files (0 = all)")
    args = ap.parse_args()

    binaries = [
        (args.bench_opt,     "opt"),
        (args.bench_naive,   "naive"),
        (args.bench_opt12,   "opt12"),
        (args.bench_naive12, "naive12"),
    ]
    for path, _ in binaries:
        if not path.exists():
            sys.exit(f"binary not found: {path}")

    corpora = discover_corpora(args.root)
    if args.limit:
        corpora = corpora[: args.limit]
    sys.stderr.write(f"discovered {len(corpora)} corpora under {args.root}\n")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    write_header = not args.out.exists() or args.out.stat().st_size == 0
    cpu = None if args.cpu < 0 else args.cpu

    mismatch_count = 0
    with args.out.open("a", newline="") as f:
        writer = csv.writer(f)
        if write_header:
            writer.writerow(CSV_HEADER)
            f.flush()

        for i, path in enumerate(corpora, start=1):
            size = path.stat().st_size
            tag = f"[{i}/{len(corpora)}] {path} ({size} B)"
            if size == 0:
                sys.stderr.write(f"  SKIP {tag} (empty)\n")
                continue
            if size > args.max_bytes:
                sys.stderr.write(f"  SKIP {tag} (>{args.max_bytes} B)\n")
                continue
            sys.stderr.write(f"{tag}\n")

            rows: dict[str, list[str]] = {}
            t0 = time.time()
            for bin_path, variant in binaries:
                row = run_one(bin_path, path, variant, args.reps, cpu, args.timeout)
                if row is None:
                    continue
                fields = row.split(",")
                writer.writerow(fields)
                f.flush()
                rows[variant] = fields

            # compressed_bytes must match within each codec (column index 4 in
            # CSV_HEADER): the buildDP variants compute the same DP solution.
            for naive_tag, opt_tag, _label in SPEEDUP_PAIRS:
                if naive_tag in rows and opt_tag in rows:
                    if rows[naive_tag][4] != rows[opt_tag][4]:
                        mismatch_count += 1
                        sys.stderr.write(
                            f"  CF MISMATCH on {path}: {naive_tag}="
                            f"{rows[naive_tag][4]} {opt_tag}={rows[opt_tag][4]}\n"
                        )
            sys.stderr.write(f"  done in {time.time()-t0:.1f}s\n")

    # Always (re)write the speedup pivot CSV from the long-form data.
    derive_speedup(args.out, args.speedup_out)
    sys.stderr.write(f"speedup CSV written to {args.speedup_out}\n")

    if mismatch_count:
        sys.stderr.write(
            f"WARNING: {mismatch_count} files produced different compressed sizes "
            "between variants -- buildDP variants must be functionally equivalent.\n"
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
