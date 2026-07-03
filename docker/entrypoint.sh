#!/usr/bin/env bash
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
# Entrypoint for the CSTPSI Docker image: a thin command dispatcher.
set -euo pipefail

# Shared demo parameters (single- and multi-container demos agree on these).
DEMO_D="${D:-1k}"; DEMO_LABEL="23bit"; DEMO_K=1; DEMO_T=2
DEMO_TP="${TP:-5}"; DEMO_TN="${TN:-5}"; DEMO_THREADS="${THREADS:-4}"
DEMO_PORT="${PORT:-1212}"
DEMO_DATA="demo/data"; DEMO_PARAMS="parameters/demo_1k.json"

cmd="${1:-help}"
shift || true

case "$cmd" in
  verify)
    exec ctest --test-dir build --output-on-failure "$@"
    ;;
  repro)
    exec bash claims/repro-table/run.sh "$@"
    ;;
  smoke)
    bash experiments/run.sh gen --smoke
    exec bash experiments/run.sh all --smoke
    ;;
  demo)
    # Single-container two-party network demo (sender + receiver over loopback).
    exec bash demo/demo_network.sh "$@"
    ;;

  # --- distributed (compose) demo: sender + receiver in separate containers ---
  demo-sender)
    mkdir -p "$DEMO_DATA"
    echo "[sender] generating shared dataset ..."
    python3 experiments/datagen.py --sizes "$DEMO_D" --labels "$DEMO_LABEL" \
        --tp "$DEMO_TP" --tn "$DEMO_TN" --output-dir "$DEMO_DATA"
    echo "[sender] listening on :$DEMO_PORT (CSTPSI, T=$DEMO_T) ..."
    exec ./build/cstpsi_sender \
        --dbFile "$DEMO_DATA/enr_${DEMO_D}_lbl${DEMO_LABEL}.csv" \
        --paramsFile "$DEMO_PARAMS" --port "$DEMO_PORT" \
        --nrofLabelChunks "$DEMO_K" --nrofTokenRounds "$DEMO_T" \
        --threads "$DEMO_THREADS" --prep-threads "$DEMO_THREADS" --mode CSTPSI
    ;;
  demo-receiver)
    addr="${ADDR:-sender}"
    qry="$DEMO_DATA/qry_${DEMO_D}.csv"
    exp="$DEMO_DATA/expected_${DEMO_D}_lbl${DEMO_LABEL}.csv"
    echo "[receiver] waiting for shared dataset ..."
    until [ -f "$qry" ] && [ -f "$exp" ]; do sleep 1; done
    # EMP's NetIO (the garbled-circuit channel on port+1) connects by IP and does
    # NOT resolve hostnames, so resolve the compose service name to an IP here and
    # pass the IP to --senderAddr (ZMQ accepts an IP too).
    ip="$(getent hosts "$addr" | awk '{print $1; exit}')"; ip="${ip:-$addr}"
    echo "[receiver] waiting for sender $addr ($ip):$DEMO_PORT ..."
    until python3 -c "import socket,sys; s=socket.socket(); s.settimeout(1); \
        sys.exit(0 if s.connect_ex(('$ip',$DEMO_PORT))==0 else 1)" 2>/dev/null; do sleep 1; done
    sleep 1
    addr="$ip"
    rounds="$(mktemp -d)"
    echo "[receiver] querying sender ..."
    ./build/cstpsi_receiver --queryFile "$qry" --paramsFile "$DEMO_PARAMS" \
        --senderAddr "$addr" --port "$DEMO_PORT" \
        --nrofRounds "$((DEMO_T + DEMO_K))" --nrofTokenRounds "$DEMO_T" \
        --outputDir "$rounds" --threads "$DEMO_THREADS" --timeout 120000 --mode CSTPSI
    echo "[receiver] verifying recovered labels ..."
    python3 experiments/verify.py --rounds-dir "$rounds" --expected "$exp" \
        --query-csv "$qry" --label-chunks "$DEMO_K" --token-rounds "$DEMO_T"
    echo "[receiver] done."
    ;;

  shell)
    exec bash "$@"
    ;;
  help|*)
    cat <<'EOF'
CSTPSI Docker image -- usage:

  docker run --rm        cstpsi verify   re-run the test suite (ctest)
  docker run --rm        cstpsi repro    reproduce the performance table
  docker run --rm        cstpsi smoke    fast structural run of the full suite
  docker run --rm        cstpsi demo     two-party network demo (single container)
  docker run --rm -it    cstpsi shell    interactive shell

Reproduce at higher fidelity / wider grid (see claims/repro-table/claim.txt):
  docker run --rm -e TP=20 -e TN=20 cstpsi repro

Distributed two-container demo (separate sender/receiver):
  docker compose up
EOF
    ;;
esac
