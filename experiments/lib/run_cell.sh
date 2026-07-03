#!/usr/bin/env bash
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
# run_cell.sh -- run ONE CSTPSI benchmark cell and emit a merged JSON record.
#
# A "cell" = one (config, D, label, T, threads) point.  This launches the
# sender + receiver over loopback, measures peak RSS of each process (by
# polling `ps`, which is portable and avoids macOS SIP stripping DYLD_* from
# /usr/bin/time), scrapes per-step timing + wire bytes from the logs, runs
# verify.py for FRR/FAR + Wilson CI, and writes a single JSON object combining
# all of it.
#
# Required env: DYLD_LIBRARY_PATH (macOS) so the binaries find SEAL/EMP.
#
# Usage (all flags required unless noted):
#   run_cell.sh \
#     --enr FILE --qry FILE --expected FILE --params FILE \
#     --label NAME --label-chunks K --token-rounds T --threads N \
#     --db-label 1k --config cstpsi --mode CSTPSI|STLPSI \
#     --out cell.json [--port 1212] [--build-dir DIR] [--timeout-sec 1800]
#
# Exit: 0 on success (out.json written), non-zero on failure.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXPERIMENTS_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$EXPERIMENTS_DIR/.." && pwd)"

# --- defaults ---------------------------------------------------------------
BUILD_DIR="$PROJECT_ROOT/build"
PORT=1212
MODE="CSTPSI"          # CSTPSI (1GC + send-once) or STLPSI (per-round GC + re-send)
TIMEOUT_SEC=1800
CONFIG="cstpsi"
LABEL="23bit"

# --- parse args -------------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --enr)          ENR="$2"; shift 2 ;;
        --qry)          QRY="$2"; shift 2 ;;
        --expected)     EXPECTED="$2"; shift 2 ;;
        --params)       PARAMS="$2"; shift 2 ;;
        --label)        LABEL="$2"; shift 2 ;;
        --label-chunks) K="$2"; shift 2 ;;
        --token-rounds) T="$2"; shift 2 ;;
        --threads)      THREADS="$2"; shift 2 ;;
        --db-label)     DB_LABEL="$2"; shift 2 ;;
        --config)       CONFIG="$2"; shift 2 ;;
        --mode)         MODE="$2"; shift 2 ;;
        --out)          OUT="$2"; shift 2 ;;
        --port)         PORT="$2"; shift 2 ;;
        --build-dir)    BUILD_DIR="$2"; shift 2 ;;
        --timeout-sec)  TIMEOUT_SEC="$2"; shift 2 ;;
        *) echo "run_cell: unknown arg $1" >&2; exit 2 ;;
    esac
done

for v in ENR QRY EXPECTED PARAMS K T THREADS DB_LABEL OUT; do
    if [[ -z "${!v:-}" ]]; then
        flag=$(echo "$v" | tr '[:upper:]' '[:lower:]')   # bash 3.2 has no ${v,,}
        echo "run_cell: missing --$flag" >&2; exit 2
    fi
done

SENDER_BIN="$BUILD_DIR/cstpsi_sender"
RECEIVER_BIN="$BUILD_DIR/cstpsi_receiver"
for b in "$SENDER_BIN" "$RECEIVER_BIN"; do
    [[ -x "$b" ]] || { echo "run_cell: binary missing: $b" >&2; exit 2; }
done

# The binaries have no rpath to SEAL/EMP, so they need DYLD_LIBRARY_PATH. It
# MUST be set here, in the shell that directly execs them: macOS SIP strips
# DYLD_* when the kernel launches a protected binary (/bin/bash), so an export
# in the parent run.sh is gone by the time we get here.  Setting it now works
# because cstpsi_sender/receiver are not SIP-protected (the var passes through).
export DYLD_LIBRARY_PATH="$BUILD_DIR/emp_install/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
for f in "$ENR" "$QRY" "$EXPECTED" "$PARAMS"; do
    [[ -f "$f" ]] || { echo "run_cell: input missing: $f" >&2; exit 2; }
done

TOTAL_ROUNDS=$(( T + K ))

# Receiver per-query response timeout (ms). The default 30s trips at large D
# (slow eval / large response), and sendQueryWithRetry then RESENDS, which
# desyncs send-once's ZMQ REQ/REP. Give large D plenty of headroom so a query
# never times out under normal operation (genuine hangs still fail, just later).
case "$DB_LABEL" in
    1m)   RX_TIMEOUT_MS=900000 ;;   # 15 min
    500k) RX_TIMEOUT_MS=450000 ;;   # 7.5 min
    100k) RX_TIMEOUT_MS=180000 ;;   # 3 min
    *)    RX_TIMEOUT_MS=120000 ;;   # 2 min
esac

# KEEP_LOGS=1 (env): run sender+receiver with --verbose (per-query x per-round
# logging) and ARCHIVE sender.log/receiver.log/rounds/ next to the cell JSON,
# instead of deleting $WORK. Default (unset/0): quiet + delete, unchanged behavior.
# Measured penalty is negligible (~10 s logging I/O across the whole suite).
KEEP_LOGS="${KEEP_LOGS:-0}"
VFLAG=""
[[ "$KEEP_LOGS" == "1" ]] && VFLAG="--verbose"

# --- scratch ----------------------------------------------------------------
WORK="$(mktemp -d /tmp/cstpsi_cell_XXXXXX)"
SENDER_LOG="$WORK/sender.log"
RECEIVER_LOG="$WORK/receiver.log"
ROUNDS_DIR="$WORK/rounds"; mkdir -p "$ROUNDS_DIR"
SENDER_RSS_KB=0; RECEIVER_RSS_KB=0
SENDER_PID=""; RECEIVER_PID=""; POLL_PID=""

cleanup() {
    [[ -n "$POLL_PID"   ]] && kill "$POLL_PID"   2>/dev/null || true
    [[ -n "$SENDER_PID" ]] && kill "$SENDER_PID" 2>/dev/null || true
    # Preserve logs on ANY failure (cell JSON not written) -- covers the
    # wait_for_sender path too, so every failure is diagnosable.
    if [[ -n "${OUT:-}" && ! -f "$OUT" ]]; then
        local fb; fb="$(dirname "$OUT")/_FAILED_$(basename "$OUT" .json)"
        mkdir -p "$(dirname "$OUT")" 2>/dev/null || true
        [[ -f "$SENDER_LOG"   ]] && cp "$SENDER_LOG"   "${fb}.sender.log"   2>/dev/null || true
        [[ -f "$RECEIVER_LOG" ]] && cp "$RECEIVER_LOG" "${fb}.receiver.log" 2>/dev/null || true
    fi
    # KEEP_LOGS=1: archive the full per-query/per-round logs + round CSVs next to
    # the cell JSON (under the gitignored experiments/results/ tree) for AE.
    if [[ "${KEEP_LOGS:-0}" == "1" && -n "${OUT:-}" ]]; then
        local ld; ld="$(dirname "$OUT")/logs/$(basename "$OUT" .json)"
        mkdir -p "$ld" 2>/dev/null || true
        [[ -f "${SENDER_LOG:-}"   ]] && cp    "$SENDER_LOG"   "$ld/sender.log"   2>/dev/null || true
        [[ -f "${RECEIVER_LOG:-}" ]] && cp    "$RECEIVER_LOG" "$ld/receiver.log" 2>/dev/null || true
        [[ -d "${ROUNDS_DIR:-}"   ]] && cp -R "$ROUNDS_DIR"   "$ld/rounds"       2>/dev/null || true
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT

# Peak-RSS poller.  It reads the two pids from files rather than shell vars: the
# poller is backgrounded (forked) before the receiver launches, so a forked copy
# of $RECEIVER_PID would never update.  Files are visible across the fork.  The
# poller samples the sender from startup (catching its offline DB-load peak) and
# the receiver once its pid file is written; it stops when DONE_FILE appears.
SENDER_RSS_FILE="$WORK/sender_rss"; echo 0 > "$SENDER_RSS_FILE"
RECEIVER_RSS_FILE="$WORK/receiver_rss"; echo 0 > "$RECEIVER_RSS_FILE"
SENDER_PID_FILE="$WORK/sender.pid"; : > "$SENDER_PID_FILE"
RECEIVER_PID_FILE="$WORK/receiver.pid"; : > "$RECEIVER_PID_FILE"
DONE_FILE="$WORK/poll.done"

poll_rss() {
    local smax=0 rmax=0 spid rpid s r
    while [[ ! -f "$DONE_FILE" ]]; do
        spid=$(cat "$SENDER_PID_FILE" 2>/dev/null || true)
        rpid=$(cat "$RECEIVER_PID_FILE" 2>/dev/null || true)
        if [[ -n "$spid" ]] && kill -0 "$spid" 2>/dev/null; then
            s=$(ps -o rss= -p "$spid" 2>/dev/null | tr -d ' ' || true)
            [[ -n "$s" && "$s" -gt "$smax" ]] && { smax=$s; echo "$smax" > "$SENDER_RSS_FILE"; }
        fi
        if [[ -n "$rpid" ]] && kill -0 "$rpid" 2>/dev/null; then
            r=$(ps -o rss= -p "$rpid" 2>/dev/null | tr -d ' ' || true)
            [[ -n "$r" && "$r" -gt "$rmax" ]] && { rmax=$r; echo "$rmax" > "$RECEIVER_RSS_FILE"; }
        fi
        sleep 0.1
    done
}

wait_for_sender() {
    local elapsed=0
    while [[ $elapsed -lt $TIMEOUT_SEC ]]; do
        grep -q "Sender listening" "$SENDER_LOG" 2>/dev/null && return 0
        kill -0 "$SENDER_PID" 2>/dev/null || { echo "run_cell: sender exited early" >&2; return 1; }
        sleep 0.5; elapsed=$((elapsed + 1))
    done
    echo "run_cell: sender not ready within ${TIMEOUT_SEC}s" >&2; return 1
}

# --- launch sender ----------------------------------------------------------
"$SENDER_BIN" \
    --dbFile "$ENR" --paramsFile "$PARAMS" --port "$PORT" \
    --nrofLabelChunks "$K" --nrofTokenRounds "$T" --nrof-online-threads "$THREADS" \
    --mode "$MODE" $VFLAG > "$SENDER_LOG" 2>&1 &
SENDER_PID=$!
echo "$SENDER_PID" > "$SENDER_PID_FILE"

poll_rss & POLL_PID=$!

if ! wait_for_sender; then touch "$DONE_FILE"; cat "$SENDER_LOG" >&2; exit 1; fi

# --- launch receiver (blocks until done) ------------------------------------
"$RECEIVER_BIN" \
    --queryFile "$QRY" --paramsFile "$PARAMS" --senderAddr 127.0.0.1 --port "$PORT" \
    --nrofRounds "$TOTAL_ROUNDS" --nrofTokenRounds "$T" \
    --outputDir "$ROUNDS_DIR" --nrof-online-threads "$THREADS" --timeout "$RX_TIMEOUT_MS" \
    --mode "$MODE" $VFLAG > "$RECEIVER_LOG" 2>&1 &
RECEIVER_PID=$!
echo "$RECEIVER_PID" > "$RECEIVER_PID_FILE"
RECEIVER_EXIT=0
wait "$RECEIVER_PID" || RECEIVER_EXIT=$?

# The sender prints "NET bytes_sent:" at session completion, normally just
# before the receiver exits.  Give it up to ~5s to flush that line (or die)
# so the byte counters aren't lost to a race when we kill it.
if [[ $RECEIVER_EXIT -eq 0 ]]; then
    for _ in $(seq 1 50); do
        grep -q "NET bytes_sent:" "$SENDER_LOG" 2>/dev/null && break
        kill -0 "$SENDER_PID" 2>/dev/null || break
        sleep 0.1
    done
fi

# stop the poller (sentinel) and the sender
touch "$DONE_FILE"
wait "$POLL_PID" 2>/dev/null || true
kill "$SENDER_PID" 2>/dev/null || true
SENDER_RSS_KB=$(cat "$SENDER_RSS_FILE")
RECEIVER_RSS_KB=$(cat "$RECEIVER_RSS_FILE")

if [[ $RECEIVER_EXIT -ne 0 ]]; then
    echo "run_cell: receiver exited $RECEIVER_EXIT" >&2
    # Preserve both logs next to where the cell JSON would go (the trap deletes
    # WORK on exit), so failures are diagnosable after the suite moves on.
    fail_base="$(dirname "$OUT")/_FAILED_$(basename "$OUT" .json)"
    mkdir -p "$(dirname "$OUT")"
    cp "$SENDER_LOG"   "${fail_base}.sender.log"   2>/dev/null || true
    cp "$RECEIVER_LOG" "${fail_base}.receiver.log" 2>/dev/null || true
    echo "run_cell: logs saved to ${fail_base}.{sender,receiver}.log" >&2
    cat "$RECEIVER_LOG" >&2
    exit 1
fi

# --- scrape logs ------------------------------------------------------------
# All scrapes read the file directly via awk (no `grep | head`, which SIGPIPEs
# under `set -o pipefail` when a pattern matches multiple lines -- e.g. STLPSI
# logs "GC blinding complete" once PER ROUND, which previously aborted run_cell).
sum_time_lines()  { awk '/^  Time: [0-9]+ ms/ {s+=$2} END{print s+0}' "$1"; }
# Sum all GC-blinding times: STLPSI logs one per round, CSTPSI logs one.
gc_sum_ms()       { awk '/GC blinding complete/{t=$0; sub(/.*\(/,"",t); sub(/ ms.*/,"",t); s+=t} END{print s+0}' "$1"; }
# First integer field `fn` on the first line containing literal substring `pat`.
field_after()     { awk -v pat="$2" -v fn="$3" 'index($0,pat){print $fn; exit}' "$1" | tr -cd '0-9'; }

sender_offline_ms=$(sum_time_lines "$SENDER_LOG")
receiver_offline_ms=$(sum_time_lines "$RECEIVER_LOG")
gc_online_ms=$(gc_sum_ms "$RECEIVER_LOG"); gc_online_ms=${gc_online_ms:-0}
query_ms=$(awk '/^Total time:/{print $3; exit}' "$RECEIVER_LOG" | tr -cd '0-9'); query_ms=${query_ms:-0}
# Reconstruction-cache instrumentation (microseconds, summed over all queries):
# "Recon us: fvp=<n> other=<n> total=<n>" emitted by the receiver. fvp is the
# per-round pair verification the recon cache derives once instead of per round.
recon_fvp_us=$(awk -F'[= ]' '/^Recon us:/{print $4; exit}' "$RECEIVER_LOG" | tr -cd '0-9'); recon_fvp_us=${recon_fvp_us:-0}
recon_other_us=$(awk -F'[= ]' '/^Recon us:/{print $6; exit}' "$RECEIVER_LOG" | tr -cd '0-9'); recon_other_us=${recon_other_us:-0}
sender_bytes_sent=$(field_after "$SENDER_LOG" "NET bytes_sent:" 3); sender_bytes_sent=${sender_bytes_sent:-0}
receiver_bytes_sent=$(field_after "$RECEIVER_LOG" "NET bytes_sent:" 3); receiver_bytes_sent=${receiver_bytes_sent:-0}

# --- verify -----------------------------------------------------------------
VERIFY_JSON="$WORK/verify.json"
python3 "$EXPERIMENTS_DIR/verify.py" \
    --rounds-dir "$ROUNDS_DIR" --expected "$EXPECTED" --query-csv "$QRY" \
    --label-chunks "$K" --token-rounds "$T" --out "$VERIFY_JSON"

# --- merge into final cell JSON ---------------------------------------------
mkdir -p "$(dirname "$OUT")"
python3 - "$VERIFY_JSON" "$OUT" <<PYEOF
import json, sys
verify = json.load(open(sys.argv[1]))
rec = {
    "config": "$CONFIG",
    "db_label": "$DB_LABEL",
    "label": "$LABEL",
    "label_chunks": int("$K"),
    "token_rounds": int("$T"),
    "total_rounds": int("$TOTAL_ROUNDS"),
    "threads": int("$THREADS"),
    "mode": "$MODE",
    "sender_offline_ms": int("$sender_offline_ms" or 0),
    "receiver_offline_ms": int("$receiver_offline_ms" or 0),
    "gc_online_ms": int("$gc_online_ms" or 0),
    "query_online_ms": int("$query_ms" or 0),
    # Reconstruction-cache instrumentation (microseconds, summed over all queries).
    # recon_fvp_us = per-round pair verification the cache derives once vs per round
    # (the recon-cache saving the ablation step measures); recon_other_us = token
    # findMatches + label reconstructLabels. Both are part of query_online_ms.
    "recon_fvp_us": int("$recon_fvp_us" or 0),
    "recon_other_us": int("$recon_other_us" or 0),
    # CSTPSI: the 1-GC runs BEFORE the timed query loop -> total = gc + query.
    # STLPSI: per-round GC is INSIDE the timed loop ("Total time:"), so it is
    # already counted in query_online_ms -> total = query only. (Matters once
    # KEEP_LOGS/--verbose makes STLPSI emit its GC line; without it gc=0 so the
    # value is unchanged from before.)
    "total_online_ms": (int("$gc_online_ms" or 0) + int("$query_ms" or 0)) if "$MODE" == "CSTPSI" else int("$query_ms" or 0),
    "net_r2s_bytes": int("$receiver_bytes_sent" or 0),
    "net_s2r_bytes": int("$sender_bytes_sent" or 0),
    "net_total_bytes": int("$receiver_bytes_sent" or 0) + int("$sender_bytes_sent" or 0),
    "sender_peak_rss_mb": round(int("$SENDER_RSS_KB" or 0) / 1024.0, 1),
    "receiver_peak_rss_mb": round(int("$RECEIVER_RSS_KB" or 0) / 1024.0, 1),
}
rec.update(verify)
json.dump(rec, open(sys.argv[2], "w"), indent=2)
print(f"[cell] {rec['config']} D={rec['db_label']} lbl={rec['label']} T={rec['token_rounds']} "
      f"thr={rec['threads']}: online={rec['total_online_ms']}ms "
      f"net={rec['net_total_bytes']/1024:.0f}kB rssR={rec['receiver_peak_rss_mb']}MB "
      f"FRR={rec['frr']:.4f} FAR={rec['far']:.6f}", file=sys.stderr)
PYEOF

exit 0
