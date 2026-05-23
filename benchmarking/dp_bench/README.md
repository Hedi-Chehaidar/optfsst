# buildDP-variant compression-speed benchmark

This benchmark compares the compression speed of OptFSST against an otherwise
identical build where two micro-optimizations are stripped from
`SymbolTable::buildDP` (`libfsst.hpp`):

* the manually unrolled 8-deep cascade that walks the suffix trie one byte at
  a time, and
* the `likely` / `unlikely` (`__builtin_expect`) branch hints inside that
  cascade.

`buildDP` is the inner loop of OptFSST's DP parser. It is invoked once per
sample line during symbol-table training (`FSST_OPT_DP_TRAIN`) and once per
511-byte chunk during encoding (`FSST_OPT_DP_ENCODE`), so changing it touches
both `Optfsst_create` and `Optfsst_compress`.

## How the variants are built

A single compile-time switch, `BUILDDP_NAIVE`, selects the variant inside
`SymbolTable::buildDP`. The production build is unaffected (the macro is only
defined for the naive library target). Both library targets are compiled from
exactly the same source tree:

* `fsst_opt`   — production, no extra defines.
* `fsst_naive` — same sources, compiled with `-DBUILDDP_NAIVE`.

Each binary (`dp_bench_opt`, `dp_bench_naive`) links one of the two libraries
and runs the full `Optfsst_create` + `Optfsst_compress` pipeline with the
OptFSST flag set
`FSST_OPT_DP_TRAIN | FSST_OPT_TRIPLES | FSST_OPT_PRUNE | FSST_OPT_DP_ENCODE`.

## Inputs and outputs

* Input corpus: every regular file under `data/refined/` (one UTF-8 string per
  line). Files with extensions `.pdf, .png, .pptx, .mp4, .db, .csv, .fsst` are
  skipped.
* Output CSV: `benchmarking/csv/dp_buildDP_variants.csv` with one row per
  `(variant, file)`:
  `variant, file, n_lines, raw_bytes, compressed_bytes,
   create_ms_mean, compress_ms_mean, total_ms_mean,
   create_mb_per_s_mean, compress_mb_per_s_mean, total_mb_per_s_mean`.

For each file both variants are timed in turn on the same pinned CPU. The
driver also asserts that both variants produce the same `compressed_bytes`
(buildDP variants must be functionally equivalent — they compute the same DP
solution); any mismatch exits non-zero and is logged.

`--reps 5` is the default; the arithmetic mean is reported. Every binary is
pinned to one CPU via `taskset -c 0` to keep timings comparable.

## Running

```
bash benchmarking/dp_bench/setup_and_run.sh
```

Tunables (environment variables):

* `DP_BENCH_REPS` — repetitions per (variant, file). Default `5`.
* `DP_BENCH_CPU` — logical CPU to pin to (`-1` disables). Default `0`.
* `DP_BENCH_MAX_BYTES` — skip files larger than this. Default 256 MiB.

The build output lives in `build-dp-bench/`. To rebuild from scratch, delete
that directory.
