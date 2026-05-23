# FSST+ vs OptFSST+ benchmark

This benchmark builds two variants of [FSST+](https://github.com/cwida/fsst_plus)
that differ only in their underlying FSST library:

* **FSST+** — upstream FSST (https://github.com/cwida/fsst) as the inner symbol-table codec.
* **OptFSST+** — the OptFSST sources sitting at the root of this repository, with
  DP training + the third counter + symbol pruning enabled during table
  construction and DP encoding enabled during compression.

The on-disk format produced by FSST+ is unchanged, so any decompressor can read
both variants' output. Only the inner FSST codec's behaviour differs.

## How the swap is done

No source file in either FSST+ or OptFSST is modified. The OptFSST library
already exports `Optfsst_create` / `Optfsst_compress` alongside the legacy
`fsst_create` / `fsst_compress` symbols. When linking the OptFSST+ binary,
`fsst_opt_wrap.cpp` provides `__wrap_fsst_create` / `__wrap_fsst_compress` and
the link line passes `-Wl,--wrap=fsst_create -Wl,--wrap=fsst_compress` so the
linker rewrites every reference (including the inline calls inside FSST+ to
build the symbol table) to route through OptFSST with all flags on.

## Inputs and outputs

* Input corpus: `data/refined/` (one UTF-8 string per line per file). The
  setup script will fail loudly if that directory is missing. Binary
  `.fsst`-compressed files and other non-text extensions are skipped.
* Output CSV: `benchmarking/csv/cf_speed_fsst_plus_vs_optfsst_plus.csv` with
  one row per `(variant, dataset, column)`. Columns:
  `variant, dataset, column, n_strings, raw_bytes, compressed_bytes,
   compression_factor, compress_ms_mean, decompress_ms_mean,
   compress_mb_per_s_mean, decompress_mb_per_s_mean`.

## Metric conventions

To stay apples-to-apples with `benchmarking/runner.cpp` (paper benches):

* `raw_bytes` is the **file size on disk** (newlines included), not the sum
  of stripped line lengths.
* `compressed_bytes` is the **literal byte count** FSST+ produced in memory:
  `(data_end − data_start) + serialized_symbol_table`. No hypothetical
  bitpacking-savings subtraction. This matches `file_size(output)` for the
  upstream FSST CLI and is the size FSST+ would write to disk verbatim.
* `compress_mb_per_s_mean` and `decompress_mb_per_s_mean` use **decimal MB**
  (`bytes / 1_000_000 / seconds`).

The full pipeline (compression includes symbol-table training, cleaving,
sizing, and writing; decompression replicates FSST+'s `DecompressBlock` path)
is timed end-to-end. `--reps 5` is used by default and the arithmetic mean
of per-rep times is reported. Every run is pinned to one CPU via
`taskset -c 0` (this is the one intentional departure from `runner.cpp`,
which does not pin — pinning reduces timing variance without changing the
metric).

## Running

The benchmark is invoked automatically by the repository's top-level
`./run.sh`. To run it stand-alone:

```
bash benchmarking/fsst_plus_bench/setup_and_run.sh
```

Tunables (environment variables):
* `FSST_PLUS_BENCH_REPS` — repetitions per (variant, file). Default `5`.
* `FSST_PLUS_BENCH_CPU` — logical CPU to pin to (`-1` disables). Default `0`.
* `FSST_PLUS_BENCH_MAX_BYTES` — skip files larger than this. Default 256 MiB
  (acts as a guardrail; oversized or otherwise pathological files are reported
  as `FAIL` by the orchestrator and excluded from the CSV).

The script clones FSST+ and upstream FSST into `build-fsst-plus-bench/`,
downloads the prebuilt `libduckdb` from the official duckdb v1.4.3 release,
and writes both binaries under `build-fsst-plus-bench/fsst_plus/build*/`.
