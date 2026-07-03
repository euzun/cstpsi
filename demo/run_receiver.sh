#!/usr/bin/env bash
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
# Terminal 2 of the two-terminal network demo: run the CSTPSI receiver.
# Connects to a sender started with run_sender.sh, runs the CSTPSI protocol
# (T=2 token rounds + 1 label chunk = 3 wire rounds), writes the per-round
# outputs, then verifies label recovery against the ground truth.
#
#   bash demo/run_receiver.sh [QUERY_CSV] [PARAMS_JSON]
#   env: ADDR (default 127.0.0.1), PORT (default 1212), BUILD_DIR (default <repo>/build),
#        D (default 1k) -- selects which generated query/expected files to use
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
export DYLD_LIBRARY_PATH="$BUILD/emp_install/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
export LD_LIBRARY_PATH="$BUILD/emp_install/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

D="${D:-1k}"
LABEL="23bit"; K=1; T=2; TOTAL_ROUNDS=$((T + K))
QRY="${1:-$HERE/data/qry_${D}.csv}"
PARAMS="${2:-$ROOT/parameters/demo_1k.json}"
EXP="$HERE/data/expected_${D}_lbl${LABEL}.csv"
ADDR="${ADDR:-127.0.0.1}"; PORT="${PORT:-1212}"
ROUNDS_DIR="$HERE/results/rounds"; mkdir -p "$ROUNDS_DIR"

[[ -x "$BUILD/cstpsi_receiver" ]] || { echo "cstpsi_receiver not built. Run 'bash install.sh' first." >&2; exit 1; }
[[ -f "$QRY" ]] || { echo "Query file $QRY missing -- start run_sender.sh first (it generates the demo data)." >&2; exit 1; }

echo "Connecting CSTPSI receiver to $ADDR:$PORT  (T=$T, $TOTAL_ROUNDS wire rounds); rounds -> $ROUNDS_DIR"
"$BUILD/cstpsi_receiver" --queryFile "$QRY" --paramsFile "$PARAMS" \
    --senderAddr "$ADDR" --port "$PORT" --mode CSTPSI \
    --nrofRounds "$TOTAL_ROUNDS" --nrofTokenRounds "$T" \
    --outputDir "$ROUNDS_DIR" --nrof-online-threads 4 --verbose

if [[ -f "$EXP" ]]; then
    echo
    echo "=== Verifying label recovery ==="
    python3 "$ROOT/experiments/verify.py" --rounds-dir "$ROUNDS_DIR" \
        --expected "$EXP" --query-csv "$QRY" --label-chunks "$K" --token-rounds "$T"
fi
