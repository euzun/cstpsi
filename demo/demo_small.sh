#!/usr/bin/env bash
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
# In-process CSTPSI demo (single binary, no network).
#
# Generates a small synthetic dataset in the CSTPSI integer-set format (the SAME
# generator the evaluation harness uses), then runs one full PSI pass with
# cstpsi_cli csv_run: for every true-positive query it recovers the matching
# enrolled record, and every true-negative query matches nothing (no false
# accept). Runtime under a minute.
#
#   bash demo/demo_small.sh
#   env: BUILD_DIR (default <repo>/build), D (default 1k), TP/TN (default 5/5)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
export DYLD_LIBRARY_PATH="$BUILD/emp_install/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
export LD_LIBRARY_PATH="$BUILD/emp_install/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

D="${D:-1k}"
TP="${TP:-5}"
TN="${TN:-5}"
LABEL="23bit"                 # single-chunk label; the recovered "label" is the enrolled id
DATA="$HERE/data"
RESULTS="$HERE/results"
PARAMS="$ROOT/parameters/demo_1k.json"

CLI="$BUILD/cstpsi_cli"
ENR="$DATA/enr_${D}_lbl${LABEL}.csv"
QRY="$DATA/qry_${D}.csv"
OUT="$RESULTS/demo_output.csv"

echo "========================================"
echo "CSTPSI In-Process Demo (D=$D, $TP TP + $TN TN)"
echo "========================================"
echo

[[ -x "$CLI" ]] || {
    echo "Error: cstpsi_cli not found at $CLI" >&2
    echo "Build first: cmake -S \"$ROOT\" -B \"$BUILD\" -DCMAKE_BUILD_TYPE=Release && cmake --build \"$BUILD\" -j" >&2
    exit 1; }

mkdir -p "$DATA" "$RESULTS"

echo "[1/2] Generating integer-set dataset ..."
python3 "$ROOT/experiments/datagen.py" --sizes "$D" --labels "$LABEL" \
    --tp "$TP" --tn "$TN" --output-dir "$DATA"
echo

echo "[2/2] Running in-process PSI (cstpsi_cli csv_run) ..."
# JSON dataset paths are relative to repo root; the explicit --db/--queries/--output
# override them so the demo works regardless of the JSON's stored paths.
"$CLI" csv_run \
    --params "$PARAMS" \
    --db "$ENR" \
    --queries "$QRY" \
    --output "$OUT" \
    --verbose
echo

# ---- Summarize against the is_tp ground truth (last column of the query CSV) ----
echo "=== Result ==="
python3 - "$QRY" "$OUT" <<'PY'
import csv, sys
qry_path, out_path = sys.argv[1], sys.argv[2]

is_tp = {}
with open(qry_path) as f:
    r = csv.reader(f); header = next(r)
    for row in r:
        is_tp[row[0]] = row[-1].strip() == "1"

matches = {}
with open(out_path) as f:
    r = csv.reader(f); next(r)             # query_id,match_count,matched_ids
    for row in r:
        matches[row[0]] = int(row[1])

tp = [q for q, t in is_tp.items() if t]
tn = [q for q, t in is_tp.items() if not t]
tp_hit = sum(1 for q in tp if matches.get(q, 0) > 0)
tn_fa  = sum(1 for q in tn if matches.get(q, 0) > 0)

frr = 0.0 if not tp else (len(tp) - tp_hit) / len(tp)
far = 0.0 if not tn else tn_fa / len(tn)
print(f"  true-positive queries recovered a match: {tp_hit}/{len(tp)}   (FRR={frr:.4f})")
print(f"  true-negative queries falsely accepted:  {tn_fa}/{len(tn)}   (FAR={far:.6f})")
print()
ok = (tp_hit == len(tp)) and (tn_fa == 0)
print("  Demo succeeded: all matches recovered, no false accepts."
      if ok else "  Demo ran but FRR/FAR are nonzero (expected 0/0 for this small set).")
PY
echo
echo "Results CSV: $OUT"
echo "Next: try the network split -> bash demo/demo_network.sh"
