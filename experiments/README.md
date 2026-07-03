# Experiments

Benchmark infrastructure for the CSTPSI paper (IACR ePrint 2026/1322,
https://eprint.iacr.org/2026/1322). The locked M1-M6 + R1/R2
experiment plan is driven end-to-end by a single dispatcher, `run.sh`, on the
**network-binary path** (`cstpsi_sender` + `cstpsi_receiver` over loopback).
The older single-process `cstpsi_cli bench` path is retained for the
fine-grained JSONL instrumentation story (see `docs/INSTRUMENTATION.md`) but is
not what produces the paper's soundness/timing tables.

## The harness (canonical)

| File | Role |
|------|------|
| `run.sh` | **Single entry point.** Subcommands enumerate the locked-plan cells, run each via `lib/run_cell.sh`, drop a per-cell JSON in `results/raw/<exp>/`, then summarize into `results/processed/`. |
| `lib/run_cell.sh` | Runs ONE cell: launches sender + receiver over loopback, polls `ps` for peak RSS of each, scrapes per-step timing + wire bytes from logs, calls `verify.py`, writes one merged JSON. |
| `datagen.py` | Dataset generator: `--tn`/`--tp` counts, sizes `1k..1m` (or a bare integer), fast inverted-index TN non-overlap check. Whole-size skip unless `--force`. |
| `verify.py` | FRR / FAR + Wilson 95% CI, multi-token-round intersection, label-reconstruction check. Emits per-cell JSON. |
| `summarize.py` | Rolls `results/raw/<exp>/*.json` into `results/processed/<exp>_summary.csv` (the hand-off to the paper session). `--frr-audit` powers M5. |

## Quick start

```bash
cd <project-root>

# 1. Generate datasets once (100 TN + 100 TP per cell). Idempotent: existing
#    sizes are skipped entirely (no regeneration without --force).
bash experiments/run.sh gen           # {23bit,16B} for 1k,10k,100k + {32B,64B} at 100k
bash experiments/run.sh gen --full    # adds 1m for bmain/a1 (large/slow)

# 2. Validate the live path first (~1 min):
bash experiments/run.sh m6

# 3. The suite (each resumable; re-run continues where it stopped):
bash experiments/run.sh bmain         # STLPSI vs CSTPSI: D x threads x {23bit,16B}
bash experiments/run.sh blabel        # vs label {32B,64B} at D=100k, thr=8
bash experiments/run.sh a1            # security T=3 sweep across D (run last)
bash experiments/run.sh pooled        # FAR/FRR pooled by (D,T) + FRR audit (instant)

# Chain everything (m6,bmain,blabel,a1,pooled + check): bash experiments/run.sh all
# Rebuild hand-off CSVs anytime:                        bash experiments/run.sh summarize
```

Protocol modes (one switch, `cstpsi_*  --mode`): **CSTPSI** = 1GC + send-once
query caching (default); **STLPSI** = per-round GC + re-send query each round
(baseline). FAR/FRR depend only on (D,T), so every cell contributes samples and
`pooled` accumulates them suite-wide.

Options: `--dry-run` (preview cells, writes nothing), `--force-rerun` (ignore
existing per-cell JSONs and re-run, overwriting on success), `--build-dir DIR`,
`--port N`, `--smoke` (see below).

### Smoke mode (`--smoke`)

End-to-end shakeout of the **real** grid with one data-point per cell (2 TP +
2 TN instead of 1000/200). Use it to confirm every cell/config runs through the
full pipeline before the multi-hour real sweeps. Fully isolated:

- datasets -> `benchmark_datasets/smoke/`
- raw -> `results/raw/smoke/`, summaries -> `results/processed/smoke/`

so it never collides with, overwrites, or skips real runs. Numbers are throwaway
placeholders (single sample); the goal is structural validation + a populated
CSV for every cell.

```bash
bash experiments/run.sh gen --smoke          # smoke datasets, <=100k (add --full for 1m)
bash experiments/run.sh all --smoke          # m6,bmain,blabel,a1,pooled -- every case, 2 samples
# or one experiment at a time: bash experiments/run.sh bmain --smoke
```

`bmain`/`a1 --smoke` skip their 1m cells unless you ran `gen --smoke --full`
(same missing-dataset skip as real runs).

`all --smoke` auto-runs the checker at the end. Run it standalone anytime:

```bash
bash experiments/run.sh check --smoke        # audits results/raw/smoke/
python3 experiments/check.py --raw-dir experiments/results/raw/m1   # any raw dir
```

`check.py` gives one PASS/FAIL verdict: per cell it FAILs on `frr>0`,
`label_mismatch>0`, `n_tp=0`, or `total_online_ms=0`, and WARNs on missing
RSS/byte capture. FAR is reported, never failed (noise at 2 TN). It also prints
per-experiment cell counts vs the expected grid so a crashed/absent cell shows
up. Exit code 0 = all pass, 1 = any fail.

`DYLD_LIBRARY_PATH` is set automatically to `<build>/emp_install/lib` (the
binaries have no rpath); a user-set value wins.

## Cell map (locked suite, 2026-05-24)

Every cell runs 100 TN + 100 TP. Order: D small->large, threads 8->4->1,
labels 23bit/16B->32B/64B.

| Exp | Cells | Sweep |
|-----|-------|-------|
| **bmain** | 48 (+ 1m in full) | D{1k,10k,100k(,1m)} x threads{8,4,1} x label{23bit,16B} x {STLPSI(T1), CSTPSI(T2)} -- the head-to-head |
| **blabel** | 4 | D=100k, thr=8, label{32B,64B} x {STLPSI, CSTPSI} (23bit,16B come from bmain) |
| **a1** | 3 (+1) | D{1k,10k,100k(,1m)} x T=3, 23bit, thr=8, CSTPSI -- security T=3 sweep, run last |
| **pooled** | (no new runs) | FAR/FRR pooled by (D,T) across all cells; T=1 from STLPSI, T=2 from CSTPSI, T=3 from a1 |
| **m6** | 2 | tiny D=10, CSTPSI -- ground-truth: frr==0 and label_mismatch==0 |

## Output

- `results/raw/<exp>/<mode>_D<size>_lbl<label>_T<t>_thr<n>.json` -- one per cell.
- `results/processed/<exp>_summary.csv` -- one row per cell (timing, comm, RSS,
  per-cell FAR/FRR), schema in the first `#` comment line.
- `results/processed/far_frr_pooled.csv` -- FAR/FRR pooled by (D,T) with Wilson
  95% CI (the headline security table). `frr_audit.csv` -- per-cell FRR/label.

`results/` and `benchmark_datasets/` are git-ignored (large / regenerable);
auto-generated `configs/gen_*.json` are git-ignored too.

## Notes / limitations

- Per-component timing is scraped from binary logs (sender/receiver offline,
  GC, FHE query online, wire bytes) plus polled peak RSS. The finer per-round
  FHE sub-step split would require the C++ JSONL work (deferred); see
  `docs/INSTRUMENTATION.md`.
- Per-query TP/TN-split timing is a pending follow-up (receiver instrumentation).
- FAR datasets need only 23-bit -- FAR is label-size-independent -- but bmain
  also runs 16B so the head-to-head has matched cells; pooling uses all of them.
- Sender-side BSGS power caching remains future work; CSTPSI's measured
  optimizations are 1GC + send-once query caching (the `--mode` switch).

## Auxiliary / convenience scripts

Helpers built around the canonical harness. They do not supersede `run.sh`; they
orchestrate it or post-process its output.

| File | Role |
|------|------|
| `analytical_stlpsi.py` | Projects STLPSI online cost from measured CSTPSI per-component instrumentation and validates the projection against directly-measured STLPSI cells. Backs the analytically-computed baseline rows. |

## Soundness harness (FPB1 container: LFW + Deep1B)

A separate, self-contained path measures RSE (soundness) and FRR across five
rungs on real embeddings to show the CSTPSI kernel restores the spurious-accept
rate to the plaintext-matcher floor while STLPSI does not. It is independent of
the network-binary `run.sh` harness above.

`build/flpsi_experiment` is the single binary. It reads one packed container
format, **FPB1**, in two modes:

- **EMBED** -- float32 embeddings (LFW). The harness runs the full
  `lshEncode -> robustTemplate -> subsample` pipeline. Rungs (i) cosine EER,
  (ii) Hamming EER, (iii) plaintext k-of-N (FMR floor), (iv) STLPSI, (v) CSTPSI.
- **BITS** -- pre-made 256-bit LSH codes (Deep1B `nosub`). The codes feed
  `subsample` directly (no LSH step). Rung (i) is N/A (no floats); rungs (ii)-(v)
  run. Ground truth is the shared id (query id X matches enroll id X).

### Data

The LFW and Deep1B `data/*.bin` datasets are provided (fetched by `reproduce.sh`
from the release assets). The public user does not regenerate them; they run
`flpsi_experiment` on the provided `.bin` files as shown below.

```bash
cmake --build build --target flpsi_experiment test_flpsi_wrapper -j
```

### Run

```bash
# LFW baseline (5 rungs):
./build/flpsi_experiment --data experiments/data/lfw_embeddings.bin \
    --db-size 1500 --repeats 3

# Deep1B (BITS): cross-id protocol soundness error, split into real (matcher FMR)
# vs garbage (protocol-added spurious accept); a synthetic random-bit impostor set
# isolates them.
./build/flpsi_experiment --data experiments/data/deep1b_E1000000_Q10000_L256.bin \
    --db-size 10000 --repeats 5 --random-impostors 1000 --tcstpsi 2 \
    --manifest-out deep1b_db10000.manifest.txt

# Reproduce + verify a prior run from its manifest (re-derives config, errors on drift):
./build/flpsi_experiment --data experiments/data/deep1b_E1000000_Q10000_L256.bin \
    --manifest-in deep1b_db10000.manifest.txt

# Larger db sizes (db=100k/1M) are heavy; raise --db-size and re-run the command
# above for each grid point you want.
```

Expected story: rung (iv) STLPSI's garbage accept/query grows with `db`; rungs
(iii) and (v) keep garbage = 0. The random-impostor "real" count stays ~0 for
db <= 100k; at db=1M about `dbSize/F` ~= 12% of garbage hits coincidentally land
in the label range `[0, dbSize)` and are binned as "real" (noted in the source),
which does not affect the STLPSI-vs-CSTPSI garbage comparison itself. STLPSI's
garbage count is stochastic (randomized shares + threaded partitioning) -- use
several `--repeats` for stable numbers; (iii)/(v) are deterministic.

The `--manifest-out` file records, per repeat, the subsample masks and the
selected enroll/query ids (plain text). `--manifest-in` re-derives the run from
the recorded seed and verifies it reproduces those masks/ids exactly.
