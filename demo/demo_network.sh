#!/usr/bin/env bash
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
# Automated two-party CSTPSI network demo.
#
# Builds a small synthetic dataset (the SAME generator/format the evaluation
# harness uses), then runs one CSTPSI cell end-to-end: cstpsi_sender and
# cstpsi_receiver exchange over a loopback socket and the receiver recovers the
# label of every matching enrolled row. Correctness is checked with verify.py.
#
#   bash demo/demo_network.sh
#   env: PORT (default 1212), BUILD_DIR (default <repo>/build),
#        TP/TN (default 5/5 queries), D (default 1k), THREADS (default 4)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
PORT="${PORT:-1212}"
THREADS="${THREADS:-4}"
D="${D:-1k}"
TP="${TP:-5}"
TN="${TN:-5}"

LABEL="23bit"   # single-chunk label; the recovered "label" is the enrolled id
K=1             # label chunks for 23bit
T=2             # token rounds (CSTPSI, T=2 -- the paper's construction)
DATA="$HERE/data"
RESULTS="$HERE/results"
PARAMS="$ROOT/parameters/demo_1k.json"

[[ -x "$BUILD/cstpsi_sender" && -x "$BUILD/cstpsi_receiver" ]] || {
    echo "Binaries not built. Run 'bash install.sh' (or use the Docker image) first." >&2; exit 1; }

echo "=== CSTPSI Network Demo (D=$D, CSTPSI T=$T) ==="
echo

mkdir -p "$DATA" "$RESULTS"
echo "[1/3] Generating synthetic dataset ($TP TP + $TN TN queries) ..."
python3 "$ROOT/experiments/datagen.py" --sizes "$D" --labels "$LABEL" \
    --tp "$TP" --tn "$TN" --output-dir "$DATA"

ENR="$DATA/enr_${D}_lbl${LABEL}.csv"
QRY="$DATA/qry_${D}.csv"
EXP="$DATA/expected_${D}_lbl${LABEL}.csv"
OUT="$RESULTS/demo_cell.json"

echo
echo "[2/3] Running sender + receiver over loopback (port $PORT) ..."
bash "$ROOT/experiments/lib/run_cell.sh" \
    --enr "$ENR" --qry "$QRY" --expected "$EXP" --params "$PARAMS" \
    --label "$LABEL" --label-chunks "$K" --token-rounds "$T" --threads "$THREADS" \
    --db-label "$D" --config cstpsi --mode CSTPSI \
    --out "$OUT" --port "$PORT" --build-dir "$BUILD"

echo
echo "[3/3] Result"
python3 - "$OUT" "$TP" "$TN" <<'PY'
import json, sys
r = json.load(open(sys.argv[1])); tp, tn = int(sys.argv[2]), int(sys.argv[3])
frr, far = r.get("frr", 0.0), r.get("far", 0.0)
print(f"  mode={r['mode']}  D={r['db_label']}  T={r['token_rounds']}  threads={r['threads']}")
print(f"  online={r['total_online_ms']} ms   comm={r['net_total_bytes']/1024:.0f} kB")
print(f"  FRR={frr:.4f}  ({tp - round(frr*tp)}/{tp} true-positive queries recovered the correct label)")
print(f"  FAR={far:.6f}  ({round(far*tn)}/{tn} false accepts among true-negatives)")
ok = (frr == 0.0 and far == 0.0)
print()
print("  Demo succeeded: all matches recovered, no false accepts."
      if ok else "  Demo ran but FRR/FAR are nonzero (expected 0/0 for this small set).")
PY
echo
echo "=== Demo Complete (details: $OUT) ==="
