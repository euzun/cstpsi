#!/usr/bin/env bash
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
# claims/repro-table/run.sh -- regenerate the head-to-head performance table.
#
# Pipeline (single command):
#   1. generate benchmark databases for every (D, label) cell;
#   2. run EVERY cell of the table: D x label x threads x {STLPSI, CSTPSI};
#   3. collect per-cell measurements (summarize.py);
#   4. render the table in the paper's format (text + LaTeX).
#
# Defaults run 2 true-positive + 2 true-negative queries per cell (fast). The
# query counts and the swept grid are overridable via environment variables:
#
#   TP=2 TN=2                 queries per cell (raise for tighter timing/FAR)
#   SIZES="1k 10k 100k"       database sizes  (set FULL=1 to also run 1m -- slow)
#   LABELS="23bit 16byte 32byte 64byte"
#   THREADS="1 4 8"
#   BUILD_DIR=<path>          default <repo>/build
#
# Example, a tighter run:   TP=20 TN=20 bash claims/repro-table/run.sh
# Example, full incl. 1m:   FULL=1 bash claims/repro-table/run.sh
#
# Numbers will differ slightly run-to-run (random data) and across machines
# (absolute timing is hardware-dependent). The reproduced claims are the
# qualitative ones: CSTPSI is faster than the STLPSI baseline with the gap
# growing in label size, CSTPSI sends less data, and FRR/FAR are as reported.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
EXP="$ROOT/experiments"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
export DYLD_LIBRARY_PATH="$BUILD_DIR/emp_install/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"

TP="${TP:-2}"; TN="${TN:-2}"
THREADS_LIST="${THREADS:-1 4 8}"
LABELS="${LABELS:-23bit 16byte 32byte 64byte}"
SIZES="${SIZES:-1k 10k 100k}"
[[ "${FULL:-0}" == "1" ]] && SIZES="$SIZES 1m"
PORT="${PORT:-1212}"

[[ -x "$BUILD_DIR/cstpsi_sender" ]] || { echo "Binaries not found in $BUILD_DIR. Run 'bash install.sh' first." >&2; exit 1; }

DATA="$EXP/benchmark_datasets/repro"
RAW="$EXP/results/raw/repro"
PROC="$EXP/results/processed"
CFG="$EXP/configs"
rm -rf "$RAW"; mkdir -p "$DATA" "$RAW" "$PROC" "$CFG"

db_int() { case "$1" in 1k) echo 1000;; 10k) echo 10000;; 100k) echo 100000;; 1m) echo 1000000;; *) echo 0;; esac; }
chunks() { case "$1" in 23bit) echo 1;; 16byte) echo 6;; 32byte) echo 12;; 64byte) echo 23;; *) echo 1;; esac; }

ensure_params() {  # $1=size-label $2=db-size-int ; echoes path to a params JSON
    local out="$CFG/repro_$1.json"
    if [[ ! -f "$out" ]]; then
        local bits splits
        bits=$(python3 -c "print(max(1,($2-1).bit_length()))")
        splits=$(python3 -c "import math;print(max(1,math.ceil($2/25000)))")
        cat > "$out" <<JSON
{ "name":"repro_$1","protocol_params":{"N":64},"database_params":{"nrof_que_ids":1200},
  "performance_params":{"m":4096,"partition_size":32,"nrof_splits":$splits,"nrof_collisions":1,"nrof_online_threads":1},
  "seal_params":{"poly_modulus_degree":4096,"plain_modulus":8519681},
  "dataset":{"format":"csv","enrollment_path":"x","query_path":"x","output_path":"x","enr_bits":$bits,"enr_total":$2} }
JSON
    fi
    echo "$out"
}

echo "[repro] TP=$TP TN=$TN  sizes=[$SIZES]  labels=[$LABELS]  threads=[$THREADS_LIST]"
echo "[repro] override via env: TP TN SIZES LABELS THREADS (FULL=1 adds 1m, slow)"

# 1. datasets ---------------------------------------------------------------
labels_csv="$(echo "$LABELS" | tr ' ' ',')"
for sl in $SIZES; do
    echo "[repro] generating data: D=$sl labels=$labels_csv ($TP TP + $TN TN)"
    python3 "$EXP/datagen.py" --sizes "$sl" --labels "$labels_csv" --tp "$TP" --tn "$TN" --force --output-dir "$DATA" >/dev/null
done

# 2. run every cell ---------------------------------------------------------
P=$PORT
for sl in $SIZES; do
    n=$(db_int "$sl"); params=$(ensure_params "$sl" "$n")
    for label in $LABELS; do
        k=$(chunks "$label")
        enr="$DATA/enr_${sl}_lbl${label}.csv"; qry="$DATA/qry_${sl}.csv"; exp="$DATA/expected_${sl}_lbl${label}.csv"
        for thr in $THREADS_LIST; do
            for spec in "stlpsi:STLPSI:1" "cstpsi:CSTPSI:2"; do
                cfg="${spec%%:*}"; rest="${spec#*:}"; mode="${rest%%:*}"; T="${rest##*:}"
                P=$((P+1))
                out="$RAW/${cfg}_D${sl}_lbl${label}_T${T}_thr${thr}.json"
                echo "[repro] $mode D=$sl lbl=$label thr=$thr T=$T"
                bash "$EXP/lib/run_cell.sh" \
                    --enr "$enr" --qry "$qry" --expected "$exp" --params "$params" \
                    --label "$label" --label-chunks "$k" --token-rounds "$T" --threads "$thr" \
                    --db-label "$sl" --config "$cfg" --mode "$mode" --out "$out" \
                    --port "$P" --build-dir "$BUILD_DIR" \
                    || echo "[repro] CELL FAILED (continuing): $mode D=$sl lbl=$label thr=$thr"
                sleep 1
            done
        done
    done
done

# 3. collect + 4. render ----------------------------------------------------
python3 "$EXP/summarize.py" --raw-dir "$RAW" --out "$PROC/repro_summary.csv" --exp repro
echo
echo "================ REPRODUCED HEAD-TO-HEAD TABLE ================"
python3 "$HERE/render_table.py" --summary "$PROC/repro_summary.csv" \
    --md "$HERE/repro_table.md" --tex "$HERE/repro_table.tex"
echo "=============================================================="
echo
echo "[repro] per-cell CSV   : $PROC/repro_summary.csv"
echo "[repro] Markdown table : $HERE/repro_table.md"
echo "[repro] LaTeX table    : $HERE/repro_table.tex"
echo "[repro] paper values for cell-by-cell comparison: claims/repro-table/expected/expected_table.md"
