#!/usr/bin/env bash
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
# run.sh -- CSTPSI ACSAC'26 experiment dispatcher (locked suite, 2026-05-24).
#
# Single entry point. Each cell runs cstpsi_sender+receiver over loopback via
# lib/run_cell.sh (--mode CSTPSI|STLPSI), drops a per-cell JSON in
# results/raw/<exp>/, then summarize.py rolls them up into results/processed/.
# Every cell runs 100 TN + 100 TP; FAR/FRR are pooled by (D,T) across all cells.
#
# USER runs the heavy experiments; this script is launch-and-walk-away.
#
#   bash experiments/run.sh gen [--full]      generate datasets (1k,10k,100k [+1m])
#   bash experiments/run.sh m6                ground-truth sanity (D=10)   (~1min)
#   bash experiments/run.sh bmain [--dry-run] STLPSI vs CSTPSI: D x threads x {23bit,16B}
#   bash experiments/run.sh blabel[--dry-run] vs label {32B,64B} at D=100k, thr=8
#   bash experiments/run.sh a1   [--dry-run]  security T=3 sweep across D (run last)
#   bash experiments/run.sh pooled            FAR/FRR pooled by (D,T) + FRR audit (no run)
#   bash experiments/run.sh all               m6,bmain,blabel,a1 then pooled; auto-runs check
#   bash experiments/run.sh check             PASS/FAIL audit over raw cells
#   bash experiments/run.sh summarize         rebuild all processed/ CSVs
#
# Options: --build-dir DIR  --port N  --dry-run  --force-rerun  --smoke
#
# --smoke: end-to-end shakeout of the REAL grid with one data-point per cell
#   (2 TP + 2 TN). Fully isolated -- datasets in benchmark_datasets/smoke/, raw
#   in results/raw/smoke/, summaries in results/processed/smoke/ -- so it never
#   touches or skips real runs. `gen --smoke` defaults to <=100k; add --full for
#   1m. Numbers are throwaway placeholders; use it to confirm the suite works
#   before the real, statistically-pooled runs.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RUN_CELL="$SCRIPT_DIR/lib/run_cell.sh"
DATAGEN="$SCRIPT_DIR/datagen.py"
SUMMARIZE="$SCRIPT_DIR/summarize.py"
CHECK="$SCRIPT_DIR/check.py"
DATA_DIR="$SCRIPT_DIR/benchmark_datasets"
CFG_DIR="$SCRIPT_DIR/configs"
RAW_DIR="$SCRIPT_DIR/results/raw"
PROC_DIR="$SCRIPT_DIR/results/processed"

BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build}"
PORT="${PORT:-1212}"
DRY_RUN=0
FORCE_RERUN=0
SMOKE=0
# Query counts written by `gen`. Real runs use 100 TN + 100 TP per cell (FAR/FRR
# are pooled by (D,T) across all cells, so samples accumulate suite-wide).
# --smoke uses 2/2 to validate the full suite end-to-end fast.
GEN_TN="${GEN_TN:-10}"
GEN_TP="${GEN_TP:-10}"

# Binaries need SEAL/EMP on the dyld path (no rpath baked in). A user-set
# DYLD_LIBRARY_PATH wins; otherwise we derive it from BUILD_DIR in main() (after
# --build-dir is parsed).
USER_DYLD="${DYLD_LIBRARY_PATH:-}"

# label name -> chunk count (must match datagen.LABEL_CONFIGS)
label_chunks() { case "$1" in 23bit) echo 1;; 16byte) echo 6;; 32byte) echo 12;; 64byte) echo 23;; 512byte) echo 179;; *) echo 1;; esac; }

# --- params templating: one JSON per D (protocol shape is D-independent) -----
ensure_params() {  # $1 = db_label, $2 = db_size_int ; echoes path
    local sl="$1" n="$2" out="$CFG_DIR/gen_${1}.json"
    if [[ ! -f "$out" ]]; then
        local bits; bits=$(python3 -c "print(max(1,($n-1).bit_length()))")
        # partitionDB is O((D/splits)^2)-per-split; cap each split at ~25k records
        # so 1M partitions in seconds (was ~50 min single-split). splits is a
        # function of D only (NOT threads) so n_part/FAR stays thread-consistent;
        # total partition count changes only by ~splits padded partitions (<<n_part).
        local splits; splits=$(python3 -c "import math;print(max(1, math.ceil($n/25000)))")
        cat > "$out" <<JSON
{
  "name": "gen_${sl}",
  "description": "Auto-generated network-bench config, D=${sl}",
  "protocol_params": { "N": 64 },
  "database_params": { "nrof_que_ids": 1200 },
  "performance_params": {
    "m": 4096, "partition_size": 32, "nrof_splits": ${splits},
    "nrof_collisions": 1, "nrof_online_threads": 1
  },
  "seal_params": { "poly_modulus_degree": 4096, "plain_modulus": 8519681 },
  "dataset": {
    "format": "csv",
    "enrollment_path": "experiments/benchmark_datasets/enr_${sl}_lbl23bit.csv",
    "query_path": "experiments/benchmark_datasets/qry_${sl}.csv",
    "output_path": "experiments/results/raw/out_${sl}.csv",
    "enr_bits": ${bits}, "enr_total": ${n}
  }
}
JSON
    fi
    echo "$out"
}

db_size_int() { case "$1" in 1k)echo 1000;;5k)echo 5000;;10k)echo 10000;;50k)echo 50000;;100k)echo 100000;;500k)echo 500000;;1m)echo 1000000;;d*)echo "${1#d}";; *)echo 0;; esac; }

# --- run one cell (or print it under --dry-run) -----------------------------
# args: exp db_label label token_rounds threads mode(CSTPSI|STLPSI)
do_cell() {
    local exp="$1" sl="$2" label="$3" T="$4" threads="$5" mode="$6"
    local config; config=$(echo "$mode" | tr '[:upper:]' '[:lower:]')
    local n; n=$(db_size_int "$sl")
    local k; k=$(label_chunks "$label")
    local enr="$DATA_DIR/enr_${sl}_lbl${label}.csv"
    local qry="$DATA_DIR/qry_${sl}.csv"
    local exp_csv="$DATA_DIR/expected_${sl}_lbl${label}.csv"
    # RUN_TAG (env, optional): suffix so repeated passes ACCUMULATE distinct JSONs
    # in the same raw dir instead of being skipped -- summarize --pool-far-frr then
    # pools FAR/FRR across all passes automatically. Empty = single-run behavior.
    local out="$RAW_DIR/$exp/${config}_D${sl}_lbl${label}_T${T}_thr${threads}${RUN_TAG:+_${RUN_TAG}}.json"
    local tag="[$exp] $config D=$sl lbl=$label T=$T thr=$threads (K=$k, rounds=$((T+k)))"

    if [[ $DRY_RUN -eq 1 ]]; then echo "DRY: $tag"; return 0; fi
    # Smoke is a fast structural check; 1m's O(D^2) partitioning is ~50 min and
    # defeats that purpose. Never run 1m under --smoke.
    if [[ $SMOKE -eq 1 && "$sl" == "1m" ]]; then echo "SKIP (1m excluded from smoke): $tag"; return 0; fi
    if [[ $FORCE_RERUN -eq 0 && -f "$out" ]]; then echo "SKIP (done): $tag"; return 0; fi
    [[ $FORCE_RERUN -eq 1 && -f "$out" ]] && echo "RERUN (overwrite on success): $tag"
    for f in "$enr" "$qry" "$exp_csv"; do
        [[ -f "$f" ]] || { echo "MISSING $f -- run 'run.sh gen' first; skipping $tag" >&2; return 0; }
    done
    # params written only for real runs (not dry-run): protocol shape is D-only
    local params; params=$(ensure_params "$sl" "$n")
    echo "RUN: $tag"
    # A single cell failure must NOT abort the whole suite (set -e): log + continue.
    if ! "$RUN_CELL" --enr "$enr" --qry "$qry" --expected "$exp_csv" --params "$params" \
        --label "$label" --label-chunks "$k" --token-rounds "$T" --threads "$threads" \
        --db-label "$sl" --config "$config" --mode "$mode" --out "$out" \
        --port "$PORT" --build-dir "$BUILD_DIR"; then
        echo "CELL FAILED: $tag (logs in $(dirname "$out")/_FAILED_*.log); continuing" >&2
    fi
    sleep 1  # let the loopback socket close before the next cell
}

summarize_exp() {  # $1 = exp name
    [[ $DRY_RUN -eq 1 ]] && return 0
    python3 "$SUMMARIZE" --raw-dir "$RAW_DIR/$1" --out "$PROC_DIR/$1_summary.csv" --exp "$1" || true
}

# ---------------------------------------------------------------------------
# Experiment definitions (locked suite, 2026-05-24).
# Every cell runs 100 TN + 100 TP; FAR/FRR are pooled by (D,T) across all cells.
# STLPSI = baseline (per-round GC + re-send, T=1); CSTPSI = optimized (1GC +
# send-once). Order: D small->large, threads 8->4->1, labels 23bit/16B->32B/64B.
# 1M is DEFERRED (online eval is memory-bound at 1M -> minutes/query; needs the
# streaming-coeffs footprint cut or a dedicated big-RAM box -- post-ACSAC). The
# million-scale claim is carried analytically (FAR proportional to D + the
# empirical curve up to 100k). To re-enable 1M post-ACSAC: add 1m back to the two
# size arrays below, run `gen --full`, and apply the streaming-coeffs fix.
# ---------------------------------------------------------------------------
# BMAIN_SIZES/A1_SIZES (env) override the D-grid, e.g. BMAIN_SIZES="1k 10k" for a
# light re-run that skips the slow large-D cells. Default = full {1k,10k,100k}.
BMAIN_SIZES=(${BMAIN_SIZES:-1k 10k 100k})
# THREADS_LIST (env) overrides the thread sweep. Default {8,4,1} = the scaling
# table; set THREADS_LIST="N" to run each cell once at N threads (e.g. an
# accuracy-only run where FAR/FRR/comm are thread-independent, so no sweep needed).
BMAIN_THREADS=(${THREADS_LIST:-8 4 1})
BMAIN_LABELS=(23bit 16byte)
BLABEL_LABELS=(32byte 64byte)
A1_SIZES=(${A1_SIZES:-1k 10k 100k})

exp_bmain() {  # head-to-head STLPSI(T1) vs CSTPSI(T2): D x threads x {23bit,16B}
    for sl in "${BMAIN_SIZES[@]}"; do
      for thr in "${BMAIN_THREADS[@]}"; do
        for label in "${BMAIN_LABELS[@]}"; do
            do_cell bmain "$sl" "$label" 1 "$thr" STLPSI
            do_cell bmain "$sl" "$label" 2 "$thr" CSTPSI
        done
      done
    done
    summarize_exp bmain
}
exp_blabel() {  # vs label at thr=8: {32B,64B} (23bit,16B come from bmain).
    # BLABEL_SIZES (env) overrides the D-grid; default 100k keeps the locked suite.
    for sl in "${BLABEL_SIZES[@]:-100k}"; do
      for label in "${BLABEL_LABELS[@]}"; do
        do_cell blabel "$sl" "$label" 1 8 STLPSI
        do_cell blabel "$sl" "$label" 2 8 CSTPSI
      done
    done
    summarize_exp blabel
}
exp_a1() {  # security T=3 sweep (T1/T2 pooled from bmain); 23bit, thr=8, CSTPSI. Run last.
    for sl in "${A1_SIZES[@]}"; do do_cell a1 "$sl" 23bit 3 8 CSTPSI; done
    summarize_exp a1
}
exp_m6() {  # ground-truth sanity: tiny D=10, full label reconstruction check
    if [[ $DRY_RUN -eq 1 ]]; then echo "DRY: [m6] generate D=10 + verify label reconstruction"; return 0; fi
    echo "[m6] generating tiny D=10 dataset ..."
    python3 "$DATAGEN" --sizes 10 --labels 23bit,64byte --tp 10 --tn 10 --force \
        --output-dir "$DATA_DIR" >/dev/null
    do_cell m6 d10 23bit 1 1 CSTPSI
    do_cell m6 d10 64byte 1 1 CSTPSI
    summarize_exp m6
    echo "[m6] PASS criterion: frr==0, label_mismatch==0."
}
exp_pooled() {  # FAR/FRR pooled by (D,T) across ALL raw cells + per-cell FRR audit
    [[ $DRY_RUN -eq 1 ]] && { echo "DRY: [pooled] FAR/FRR by (D,T) across all raw cells"; return 0; }
    python3 "$SUMMARIZE" --raw-dir "$RAW_DIR" --recursive --pool-far-frr --out "$PROC_DIR/far_frr_pooled.csv"
    python3 "$SUMMARIZE" --raw-dir "$RAW_DIR" --recursive --frr-audit --out "$PROC_DIR/frr_audit.csv"
    # Project STLPSI time+comm from the measured CSTPSI cells (Q-normalized, so a
    # low-query STLPSI validation cell suffices); cross-checks vs any measured STLPSI.
    python3 "$SCRIPT_DIR/analytical_stlpsi.py" --raw-dir "$RAW_DIR" --recursive \
        --out "$PROC_DIR/stlpsi_analytical.csv" || true
}

cmd_gen() {
    local full=0; for a in "$@"; do [[ "$a" == "--full" ]] && full=1; done
    local sizes="1k,10k,100k"; [[ $full -eq 1 ]] && sizes="$sizes,1m"
    if [[ $DRY_RUN -eq 1 ]]; then
        echo "DRY: would generate {23bit,16byte} for [$sizes] + {32byte,64byte} for [100k] ($GEN_TN TN + $GEN_TP TP each) -> $DATA_DIR"
        echo "DRY: existing files are skipped per-size (no regeneration without --force)"
        return 0
    fi
    # B-main + A1 sweep both D and threads at {23bit,16byte} across all sizes.
    echo "Generating {23bit,16byte} for: $sizes ($GEN_TN TN + $GEN_TP TP) -> $DATA_DIR ..."
    python3 "$DATAGEN" --sizes "$sizes" --labels 23bit,16byte --tn "$GEN_TN" --tp "$GEN_TP" --output-dir "$DATA_DIR"
    # B-label sweeps the larger labels at D=100k only.
    echo "Generating {32byte,64byte} for B-label: 100k ..."
    python3 "$DATAGEN" --sizes "100k" --labels 32byte,64byte --tn "$GEN_TN" --tp "$GEN_TP" --output-dir "$DATA_DIR"
    echo "Done. (use --full to add 1m for B-main/A1, which is large/slow)"
}

# ---------------------------------------------------------------------------
main() {
    [[ $# -lt 1 ]] && { sed -n '2,30p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 1; }
    local cmd="$1"; shift || true
    local REST=()
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --dry-run)     DRY_RUN=1; shift ;;
            --force-rerun) FORCE_RERUN=1; shift ;;
            --smoke)       SMOKE=1; shift ;;
            --build-dir)   BUILD_DIR="$2"; shift 2 ;;
            --port)        PORT="$2"; shift 2 ;;
            *)             REST+=("$1"); shift ;;
        esac
    done
    # User-set DYLD wins; else derive from the (possibly --build-dir-overridden) BUILD_DIR.
    if [[ -n "$USER_DYLD" ]]; then export DYLD_LIBRARY_PATH="$USER_DYLD"
    else export DYLD_LIBRARY_PATH="$BUILD_DIR/emp_install/lib"; fi
    # --smoke: isolated namespace (datasets/raw/processed under smoke/) + tiny
    # query counts. Keeps the real grid but produces one data-point per cell, so
    # the full suite runs end-to-end without touching real datasets or results.
    if [[ $SMOKE -eq 1 ]]; then
        DATA_DIR="$SCRIPT_DIR/benchmark_datasets/smoke"
        RAW_DIR="$SCRIPT_DIR/results/raw/smoke"
        PROC_DIR="$SCRIPT_DIR/results/processed/smoke"
        GEN_TN=2; GEN_TP=2
        echo "[smoke] isolated namespace; 2 TP + 2 TN per cell; outputs under .../smoke/"
    fi
    mkdir -p "$RAW_DIR" "$PROC_DIR" "$CFG_DIR" "$DATA_DIR"

    case "$cmd" in
        gen)       cmd_gen ${REST[@]+"${REST[@]}"} ;;
        m6)        exp_m6 ;;
        bmain)     exp_bmain ;;
        blabel)    exp_blabel ;;
        a1)        exp_a1 ;;
        pooled)    exp_pooled ;;
        all)       exp_m6; exp_bmain; exp_blabel; exp_a1   # perf first, A1(T=3) last
                   exp_pooled
                   [[ $DRY_RUN -eq 0 ]] && { echo; python3 "$CHECK" --raw-dir "$RAW_DIR" || true; } ;;
        check)     python3 "$CHECK" --raw-dir "$RAW_DIR" ;;
        summarize) for e in bmain blabel a1 m6; do [[ -d "$RAW_DIR/$e" ]] && summarize_exp "$e"; done; exp_pooled ;;
        *)         echo "unknown command: $cmd" >&2; exit 1 ;;
    esac
}
main "$@"
