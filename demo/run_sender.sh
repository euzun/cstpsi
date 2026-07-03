#!/usr/bin/env bash
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
# Terminal 1 of the two-terminal network demo: start the CSTPSI sender.
# The sender holds the enrolled database and serves the CSTPSI protocol
# (T=2 token rounds, 1 label chunk for the 23bit label). It stays up until
# Ctrl-C; connect a receiver from another terminal with run_receiver.sh.
#
#   bash demo/run_sender.sh [DB_CSV] [PARAMS_JSON]
#   env: PORT (default 1212), BUILD_DIR (default <repo>/build),
#        D (default 1k), TP/TN (default 5/5) -- only used to auto-generate data
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
export DYLD_LIBRARY_PATH="$BUILD/emp_install/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
export LD_LIBRARY_PATH="$BUILD/emp_install/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

D="${D:-1k}"; TP="${TP:-5}"; TN="${TN:-5}"
LABEL="23bit"; K=1; T=2                       # 23bit -> 1 label chunk; CSTPSI T=2
DB="${1:-$HERE/data/enr_${D}_lbl${LABEL}.csv}"
PARAMS="${2:-$ROOT/parameters/demo_1k.json}"
PORT="${PORT:-1212}"

[[ -x "$BUILD/cstpsi_sender" ]] || { echo "cstpsi_sender not built. Run 'bash install.sh' first." >&2; exit 1; }
[[ -f "$DB" ]] || {
    echo "Enrollment CSV missing; generating demo data ..."
    python3 "$ROOT/experiments/datagen.py" --sizes "$D" --labels "$LABEL" \
        --tp "$TP" --tn "$TN" --output-dir "$HERE/data"; }

echo "Starting CSTPSI sender on port $PORT (db=$DB, T=$T). Press Ctrl-C to stop."
exec "$BUILD/cstpsi_sender" --dbFile "$DB" --paramsFile "$PARAMS" --port "$PORT" \
    --nrofLabelChunks "$K" --nrofTokenRounds "$T" --mode CSTPSI --verbose
