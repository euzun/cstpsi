#!/usr/bin/env bash
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
# =============================================================================
# reproduce.sh -- single, self-contained reproduction harness for the CSTPSI
# (WAHC-2026) paper. One file: it generates synthetic data, runs the network
# (timing/comm/FAR) and soundness (Deep1B/LFW) harnesses, and processes the
# outputs into the paper's tables and figures. It calls NO other shell script.
# (Python helpers datagen.py / verify.py / summarize.py / analytical_stlpsi.py /
# ablation_factors.py are the analysis layer, invoked as data tools.)
#
# Runs SEQUENTIALLY: never two multithreaded cells at once (core contention
# pollutes timing). Resumable: a cell whose JSON already exists is skipped.
#
# QUICK START
#   bash experiments/reproduce.sh                 # reproduce everything, defaults
#   bash experiments/reproduce.sh --list          # list targets + what each makes
#   bash experiments/reproduce.sh --select        # interactive target menu
#   bash experiments/reproduce.sh --help          # full flag reference
#   bash experiments/reproduce.sh --target headtohead --sizes 100k --labels 64byte --threads 8
#   DRY_RUN=1 bash experiments/reproduce.sh --target all   # print the plan only
#
# See --help for every flag and its default.
# =============================================================================
set -uo pipefail   # NOT -e: a failed cell is logged and the suite continues.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ----------------------------------------------------------------------------
# Defaults (every one overridable by a flag or env var; unset => "all/auto").
# ----------------------------------------------------------------------------
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
DATA_DIR="$SCRIPT_DIR/benchmark_datasets"     # synthetic enr/qry/expected (timing grid)
FAR_DIR="$SCRIPT_DIR/benchmark_datasets/far"  # synthetic, high-TN, for the RSE curve
CFG_DIR="$SCRIPT_DIR/configs"
RAW_DIR="$SCRIPT_DIR/results/raw"
PROC_DIR="$SCRIPT_DIR/results/processed"
DATAGEN="$SCRIPT_DIR/datagen.py"
VERIFY="$SCRIPT_DIR/verify.py"
SUMMARIZE="$SCRIPT_DIR/summarize.py"
ANALYTICAL="$SCRIPT_DIR/analytical_stlpsi.py"
ABLATION="$SCRIPT_DIR/ablation_factors.py"
DEEP1B_BIN="${DEEP1B_BIN:-$SCRIPT_DIR/data/deep1b_E1000000_Q10000_L256.bin}"
LFW_BIN="${LFW_BIN:-$SCRIPT_DIR/data/lfw_embeddings.bin}"
# Artifact clones ship the Deep1B/LFW datasets as GitHub Release assets, not in
# the repo. ensure_data fetches a missing .bin from the latest public release;
# it is a no-op when the file is already present (override host with ARTIFACT_RELEASE).
ARTIFACT_RELEASE="${ARTIFACT_RELEASE:-https://github.com/euzun/cstpsi/releases/latest/download}"
ensure_data(){  # $1 = path to a data .bin
  local f="$1"
  [[ -f "$f" ]] && return 0
  command -v curl >/dev/null 2>&1 || return 0
  mkdir -p "$(dirname "$f")"
  echo "[reproduce] fetching $(basename "$f") from release assets..."
  curl -fsSL "$ARTIFACT_RELEASE/$(basename "$f")" -o "$f" || rm -f "$f"
}
SENDER_BIN="$BUILD_DIR/cstpsi_sender"
RECEIVER_BIN="$BUILD_DIR/cstpsi_receiver"
FLPSI_BIN="$BUILD_DIR/flpsi_experiment"

# selection
TARGETS=""           # space list of {headtohead far ablation deep1b lfw}; empty+!SELECT => all
SELECT=0; LIST=0; SHOW_HELP=0
# cell-dimension filters (the rows/columns of the tables). Empty => the target's default set.
F_SIZES="${SIZES:-}"          # e.g. "1k 10k 100k 1m"
F_LABELS="${LABELS:-}"        # e.g. "23bit 16byte 32byte 64byte"
F_THREADS="${THREADS:-}"      # e.g. "8 4 1"
F_PROTOCOLS="${PROTOCOLS:-}"  # e.g. "STLPSI CSTPSI"
F_ROUNDS="${ROUNDS:-}"        # token-rounds T list, e.g. "1 2 3"
# per-cell counts (empty => auto/default)
REPEATS="${REPEATS:-}"        # passes per network cell / repeats per soundness cell (default 10)
TP="${TP:-}"; TN="${TN:-}"    # positive / negative queries per cell (default: auto, see qcount())
# soundness
DEEP1B_DBS="${DEEP1B_DBS:-}"  # default 1000000 100000 10000 1000 (longest first)
LFW_DB="${LFW_DB:-1500}"; LFW_IMP="${LFW_IMP:-100}"
SEED="${SEED:-12345}"
CORES=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 8)
SND_THREADS="${SND_THREADS:-$(( CORES > 2 ? CORES - 2 : 1 ))}"
# Offline threads = the sender flag --nrof-offline-threads (DB partitioning + the
# T+K coefficient build, the offline cost at large D). Left EMPTY so the binary's
# own default applies -- params.cc defaults nrof_offline_threads to ALL cores, since
# the offline runs alone. Set PREP_THREADS=N to override.
PREP_THREADS="${PREP_THREADS:-}"
# behavior
DRY_RUN="${DRY_RUN:-0}"; FORCE="${FORCE:-0}"; PORT="${PORT:-1212}"
TIMEOUT_SEC="${TIMEOUT_SEC:-3600}"

STAMP="$(date +%Y%m%d_%H%M%S 2>/dev/null || echo manual)"
GIT_SHA="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"

usage() {
cat <<'EOF'
reproduce.sh -- one-file CSTPSI paper reproduction.

USAGE
  bash experiments/reproduce.sh [--target T ...] [filters] [counts] [flags]

TARGETS (--target may be repeated; default = all)
  headtohead   tab:headtohead  (+ fig:perf-speedup, fig:comm-saving)  [network timing/comm]
  far          fig:far-empirical (the synthetic RSE-vs-D curve)        [network FAR, high TN]
  ablation     tab:ablation     (caching breakdown + recon on/off)     [network grid + analysis]
  deep1b       tab:deep1b       (Deep1B kernel-soundness err-rate)      [soundness, real data]
  lfw          tab:lfw          (LFW real-data accuracy + FAR/FRR)      [soundness, real data]
  all          everything above

SELECTION
  --select            interactive menu of targets (when you do not pass --target)
  --list              print the targets + what each reproduces, then exit
  --dry-run           print the exact plan, run nothing  (or DRY_RUN=1)

CELL-DIMENSION FILTERS  (these ARE the table rows/columns; unset => the target's full set)
  --sizes   "1k 10k 100k 1m"          database sizes D            (default: all)
  --labels  "23bit 16byte 32byte 64byte"  label sizes            (default: all)
  --threads "8 4 1"                    thread counts              (default: 8 + the 8/4/1 sweep)
  --protocols "STLPSI CSTPSI"          protocols                  (default: both)
  --rounds  "1 2 3"                    token rounds T             (default: per target)
  e.g. one cell:  --target headtohead --sizes 100k --labels 64byte --threads 8 --protocols CSTPSI

PER-CELL COUNTS  (single-cell grain; unset => auto)
  --repeats N    passes per network cell / repeats per soundness cell   (default: 10)
  --tp N         positive (genuine) queries per cell                     (default: auto*)
  --tn N         negative (impostor) queries per cell                    (default: auto*)
    *auto timing: per-D count at 8 threads (1m/500k/100k=5, 10k=10, 1k=20);
       the thread-scaling sweep (1- and 4-thread cells) uses TS_TP+TS_TN=2+2
       since only the per-query ratio matters; STLPSI-long cells use STL_TP+STL_TN=1+1.
       (a network cell runs ONCE; --repeats applies only to soundness re-draws.)
     auto FAR:    --tn 100 at 23-bit (FAR is label-independent; needs samples)
     auto sound:  100 genuine + 100 random  (Deep1B), 100 impostors (LFW)
  --deep1b-dbs "1000000 100000 10000 1000"   Deep1B DB sizes (longest first)
  --lfw-db N --lfw-imp N    LFW database + impostor counts

FLAGS
  --force        re-run cells whose JSON already exists (default: skip = resumable)
  --port N       loopback base port (default 1212)
  --build-dir D  build directory (default <root>/build)
  --seed N       RNG seed (default 12345)
  PREP_THREADS=N (env) the sender's --nrof-offline-threads (DB partition + coeff build,
                 the offline cost). Unset => the binary default of ALL cores (params.cc).

DATA
  Synthetic enr/qry/expected CSVs are generated automatically if missing.
  Deep1B + LFW .bin files are SHIPPED with the artifact and must be present:
    experiments/data/deep1b_E1000000_Q10000_L256.bin
    experiments/data/lfw_embeddings.bin
  (override with DEEP1B_BIN= / LFW_BIN=). Soundness targets skip with a warning if absent.

OUTPUTS  (a MANIFEST mapping every paper element -> file is written at the end)
  network:   results/processed/{bmain_summary,blabel_summary,far_frr_pooled,stlpsi_analytical}.csv
  ablation:  results/processed/ablation_report.txt
  soundness: results/release_<stamp>/soundness/{deep1b/db*/run.txt, lfw/run.txt}
EOF
}

# ----------------------------------------------------------------------------
# Arg parsing
# ----------------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
  case "$1" in
    --target)     TARGETS="$TARGETS $2"; shift 2 ;;
    --sizes)      F_SIZES="$2"; shift 2 ;;
    --labels)     F_LABELS="$2"; shift 2 ;;
    --threads)    F_THREADS="$2"; shift 2 ;;
    --protocols)  F_PROTOCOLS="$2"; shift 2 ;;
    --rounds)     F_ROUNDS="$2"; shift 2 ;;
    --repeats)    REPEATS="$2"; shift 2 ;;
    --tp)         TP="$2"; shift 2 ;;
    --tn)         TN="$2"; shift 2 ;;
    --deep1b-dbs) DEEP1B_DBS="$2"; shift 2 ;;
    --lfw-db)     LFW_DB="$2"; shift 2 ;;
    --lfw-imp)    LFW_IMP="$2"; shift 2 ;;
    --seed)       SEED="$2"; shift 2 ;;
    --port)       PORT="$2"; shift 2 ;;
    --build-dir)  BUILD_DIR="$2"; SENDER_BIN="$BUILD_DIR/cstpsi_sender"; RECEIVER_BIN="$BUILD_DIR/cstpsi_receiver"; FLPSI_BIN="$BUILD_DIR/flpsi_experiment"; shift 2 ;;
    --force)      FORCE=1; shift ;;
    --dry-run)    DRY_RUN=1; shift ;;
    --select)     SELECT=1; shift ;;
    --list)       LIST=1; shift ;;
    -h|--help)    SHOW_HELP=1; shift ;;
    *) echo "unknown arg: $1  (try --help)" >&2; exit 2 ;;
  esac
done
[[ "$SHOW_HELP" == 1 ]] && { usage; exit 0; }

REPEATS="${REPEATS:-10}"
DEEP1B_DBS="${DEEP1B_DBS:-1000000 100000 10000 1000}"
ALL_TARGETS="headtohead far ablation deep1b lfw"
export DYLD_LIBRARY_PATH="$BUILD_DIR/emp_install/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"

target_blurb() { case "$1" in
  headtohead) echo "tab:headtohead (+ fig:perf-speedup, fig:comm-saving)" ;;
  far)        echo "fig:far-empirical (synthetic RSE-vs-D curve)" ;;
  ablation)   echo "tab:ablation (caching breakdown + recon on/off)" ;;
  deep1b)     echo "tab:deep1b (Deep1B kernel soundness)" ;;
  lfw)        echo "tab:lfw (LFW real-data accuracy + FAR/FRR)" ;;
esac; }

if [[ "$LIST" == 1 ]]; then
  echo "Targets (pass with --target, or run with no target to do all):"
  for t in $ALL_TARGETS; do printf "  %-11s -> %s\n" "$t" "$(target_blurb "$t")"; done
  exit 0
fi

if [[ "$SELECT" == 1 && -z "${TARGETS// /}" ]]; then
  echo "Select target(s) to reproduce (space-separated numbers, or 'a' for all):"
  i=1; declare -a MENU=()
  for t in $ALL_TARGETS; do printf "  %d) %-11s %s\n" "$i" "$t" "$(target_blurb "$t")"; MENU[$i]="$t"; i=$((i+1)); done
  printf "  a) all\n> "; read -r ans
  if [[ "$ans" == "a" || -z "$ans" ]]; then TARGETS="$ALL_TARGETS"
  else for n in $ans; do TARGETS="$TARGETS ${MENU[$n]:-}"; done; fi
fi
[[ -z "${TARGETS// /}" ]] && TARGETS="$ALL_TARGETS"

# ----------------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------------
say(){ echo "[reproduce $(date +%H:%M:%S 2>/dev/null || echo --)] $*"; }
run(){ if [[ "$DRY_RUN" == 1 ]]; then echo "    DRY: $*"; return 0; fi; eval "$*"; }
has(){ for x in $2; do [[ "$x" == "$1" ]] && return 0; done; return 1; }
db_int(){ case "$1" in 1k)echo 1000;;5k)echo 5000;;10k)echo 10000;;50k)echo 50000;;100k)echo 100000;;500k)echo 500000;;1m)echo 1000000;;d*)echo "${1#d}";;*)echo 0;; esac; }
label_k(){ case "$1" in 23bit)echo 1;;16byte)echo 6;;32byte)echo 12;;64byte)echo 23;;512byte)echo 179;;*)echo 1;; esac; }
# per-D timing query count (TP and TN each). The synthetic qry is per-D and shared
# across labels and protocols, so one count serves both STLPSI and CSTPSI at a D;
# we favor the long cells (few queries) since the slowest label sets the cost.
# Override globally with --tp/--tn, or split protocols across runs for finer control
# (e.g. --protocols STLPSI --sizes 1m --tp 1 --tn 1). The FAR target overrides TN.
qd_tp(){ [[ -n "$TP" ]] && { echo "$TP"; return; }; case "$1" in 1m|500k|100k)echo 5;;10k)echo 10;;*)echo 20;; esac; }
qd_tn(){ [[ -n "$TN" ]] && { echo "$TN"; return; }; case "$1" in 1m|500k|100k)echo 5;;10k)echo 10;;*)echo 20;; esac; }
# a cell is "long" (expensive) when D is large or the label has many chunks.
is_long(){ case "$1" in 100k|500k|1m) return 0;; esac; case "$2" in 32byte|64byte) return 0;; esac; return 1; }
# STLPSI-long query subset (TP,TN). STLPSI re-runs GC every round so long cells are
# slow; we run it on a small subset of the shared qry. Default 1+1 (your spec),
# overridable with STL_TP / STL_TN, or with --tp/--tn (which set the whole grid).
STL_TP="${STL_TP:-1}"; STL_TN="${STL_TN:-1}"
# thread-scaling query subset (TP,TN) for the non-8-thread cells. The 1- and 4-thread
# cells exist only to report the 8/4/1 scaling ratio, which is a per-query time, so a
# couple of queries suffice; the slow single-thread cells thus stay cheap. The 8-thread
# cells (the headline timing/comm) keep the full per-D count. Override TS_TP / TS_TN.
TS_TP="${TS_TP:-2}"; TS_TN="${TS_TN:-2}"
mn(){ [ "$1" -lt "$2" ] && echo "$1" || echo "$2"; }   # numeric min
# write a TP+TN subset of a qry CSV (header + first N TP rows + first N TN rows; the
# is_tp flag is the last column). echoes the path to use (full qry under --dry-run).
make_qry(){  # $1=full_qry $2=tp $3=tn $4=out
  local full="$1" tp="$2" tn="$3" out="$4"
  { [[ "$DRY_RUN" == 1 ]] || [[ ! -f "$full" ]]; } && { echo "$full"; return; }
  head -1 "$full" > "$out"
  awk -F, 'NR>1 && $NF+0==1' "$full" | head -n "$tp" >> "$out"
  awk -F, 'NR>1 && $NF+0==0' "$full" | head -n "$tn" >> "$out"
  echo "$out"
}

# params JSON per D (protocol shape is D-only). Mirrors the locked template.
ensure_params(){  # $1=db_label $2=db_int ; echoes path
  local sl="$1" n="$2"; local out="$CFG_DIR/gen_${sl}.json"
  if [[ ! -f "$out" ]]; then
    mkdir -p "$CFG_DIR"
    local bits splits
    bits=$(python3 -c "print(max(1,($n-1).bit_length()))")
    splits=$(python3 -c "import math;print(max(1, math.ceil($n/25000)))")
    cat > "$out" <<JSON
{
  "name": "gen_${sl}",
  "description": "reproduce.sh network-bench config, D=${sl}",
  "protocol_params": { "N": 64 },
  "database_params": { "nrof_que_ids": 1200 },
  "performance_params": { "m": 4096, "partition_size": 32, "nrof_splits": ${splits},
    "nrof_collisions": 1, "nrof_online_threads": 1 },
  "seal_params": { "poly_modulus_degree": 4096, "plain_modulus": 8519681 },
  "dataset": { "format": "csv",
    "enrollment_path": "experiments/benchmark_datasets/enr_${sl}_lbl23bit.csv",
    "query_path": "experiments/benchmark_datasets/qry_${sl}.csv",
    "output_path": "experiments/results/raw/out_${sl}.csv",
    "enr_bits": ${bits}, "enr_total": ${n} }
}
JSON
  fi
  echo "$out"
}

# HARNESS A: synthetic dataset generation (idempotent; only gens if missing).
gen_dataset(){  # $1=db_label $2=labels_csv $3=tp $4=tn $5=outdir
  local sl="$1" labels="$2" tp="$3" tn="$4" outdir="$5"
  local gs="$sl"; [[ "$sl" == d* ]] && gs="${sl#d}"
  # already present with the right query count? (qry is shared across labels per D)
  local qry="$outdir/qry_${sl}.csv" need=0
  [[ -f "$qry" ]] || need=1
  if [[ "$need" == 0 && "$FORCE" != 1 ]]; then
    for l in ${labels//,/ }; do [[ -f "$outdir/enr_${sl}_lbl${l}.csv" ]] || need=1; done
  fi
  [[ "$FORCE" == 1 ]] && need=1
  if [[ "$need" == 1 ]]; then
    run "python3 '$DATAGEN' --sizes $gs --labels $labels --tn $tn --tp $tp --output-dir '$outdir' $([[ "$FORCE" == 1 ]] && echo --force)"
  else
    [[ "$DRY_RUN" == 1 ]] && echo "    DRY: (datasets for $sl present in $outdir -- skip gen)"
  fi
}

# HARNESS B: run ONE network cell (loopback sender+receiver) -> merged JSON.
# Faithful port of lib/run_cell.sh: peak-RSS poll, log scrape (incl recon_fvp),
# verify.py FRR/FAR, single JSON object. Sequential; one cell at a time.
run_network_cell(){  # $1=exp $2=db_label $3=label $4=T $5=threads $6=MODE $7=out $8=enr $9=qry ${10}=expected
  local EXP="$1" DBL="$2" LBL="$3" T="$4" THREADS="$5" MODE="$6" OUT="$7" ENR="$8" QRY="$9" EXPECTED="${10}"
  local K; K=$(label_k "$LBL"); local TOTAL_ROUNDS=$(( T + K ))
  local config; config=$(echo "$MODE" | tr '[:upper:]' '[:lower:]')
  if [[ "$FORCE" != 1 && -f "$OUT" ]]; then say "  SKIP (done): $EXP $config D=$DBL lbl=$LBL T=$T thr=$THREADS"; return 0; fi
  local PARAMS; PARAMS=$(ensure_params "$DBL" "$(db_int "$DBL")")
  for f in "$ENR" "$QRY" "$EXPECTED" "$PARAMS"; do
    [[ -f "$f" ]] || { say "  MISSING $f -- skip $EXP $config D=$DBL lbl=$LBL"; return 0; }
  done
  say "  RUN: $EXP $config D=$DBL lbl=$LBL T=$T thr=$THREADS (K=$K rounds=$TOTAL_ROUNDS)"
  if [[ "$DRY_RUN" == 1 ]]; then echo "    DRY: loopback $MODE cell -> $OUT (port $PORT)"; return 0; fi

  local RX_TIMEOUT_MS; case "$DBL" in 1m)RX_TIMEOUT_MS=1800000;;500k)RX_TIMEOUT_MS=900000;;100k)RX_TIMEOUT_MS=180000;;*)RX_TIMEOUT_MS=120000;; esac
  local WORK SLOG RLOG RND
  WORK="$(mktemp -d /tmp/cstpsi_cell_XXXXXX)"; SLOG="$WORK/sender.log"; RLOG="$WORK/receiver.log"; RND="$WORK/rounds"; mkdir -p "$RND"
  local SRSS="$WORK/srss"; echo 0 >"$SRSS"; local RRSS="$WORK/rrss"; echo 0 >"$RRSS"
  local SPF="$WORK/s.pid"; :>"$SPF"; local RPF="$WORK/r.pid"; :>"$RPF"; local DONE="$WORK/done"
  local SENDER_PID="" RECEIVER_PID="" POLL_PID=""
  _cell_cleanup(){
    [[ -n "$POLL_PID" ]] && kill "$POLL_PID" 2>/dev/null || true
    [[ -n "$SENDER_PID" ]] && kill "$SENDER_PID" 2>/dev/null || true
    if [[ -n "$OUT" && ! -f "$OUT" ]]; then local fb; fb="$(dirname "$OUT")/_FAILED_$(basename "$OUT" .json)"; mkdir -p "$(dirname "$OUT")" 2>/dev/null||true
      [[ -f "$SLOG" ]]&&cp "$SLOG" "${fb}.sender.log" 2>/dev/null||true; [[ -f "$RLOG" ]]&&cp "$RLOG" "${fb}.receiver.log" 2>/dev/null||true; fi
    rm -rf "$WORK"; }
  trap _cell_cleanup RETURN
  # peak-RSS poller (reads pids from files; survives the fork)
  ( smax=0; rmax=0
    while [[ ! -f "$DONE" ]]; do
      spid=$(cat "$SPF" 2>/dev/null||true); rpid=$(cat "$RPF" 2>/dev/null||true)
      [[ -n "$spid" ]]&&kill -0 "$spid" 2>/dev/null&&{ s=$(ps -o rss= -p "$spid" 2>/dev/null|tr -d ' '||true); [[ -n "$s" && "$s" -gt "$smax" ]]&&{ smax=$s; echo "$smax">"$SRSS"; }; }
      [[ -n "$rpid" ]]&&kill -0 "$rpid" 2>/dev/null&&{ r=$(ps -o rss= -p "$rpid" 2>/dev/null|tr -d ' '||true); [[ -n "$r" && "$r" -gt "$rmax" ]]&&{ rmax=$r; echo "$rmax">"$RRSS"; }; }
      sleep 0.1
    done ) & POLL_PID=$!
  # sender
  "$SENDER_BIN" --dbFile "$ENR" --paramsFile "$PARAMS" --port "$PORT" \
    --nrofLabelChunks "$K" --nrofTokenRounds "$T" --nrof-online-threads "$THREADS" \
    ${PREP_THREADS:+--nrof-offline-threads "$PREP_THREADS"} \
    --mode "$MODE" > "$SLOG" 2>&1 & SENDER_PID=$!; echo "$SENDER_PID" > "$SPF"
  local el=0; until grep -q "Sender listening" "$SLOG" 2>/dev/null; do
    kill -0 "$SENDER_PID" 2>/dev/null || { say "    sender exited early"; touch "$DONE"; cat "$SLOG" >&2; return 1; }
    sleep 0.5; el=$((el+1)); [[ $el -ge $(( TIMEOUT_SEC*2 )) ]] && { say "    sender not ready"; touch "$DONE"; return 1; }
  done
  # receiver (blocks)
  "$RECEIVER_BIN" --queryFile "$QRY" --paramsFile "$PARAMS" --senderAddr 127.0.0.1 --port "$PORT" \
    --nrofRounds "$TOTAL_ROUNDS" --nrofTokenRounds "$T" --outputDir "$RND" --nrof-online-threads "$THREADS" \
    --timeout "$RX_TIMEOUT_MS" --mode "$MODE" > "$RLOG" 2>&1 & RECEIVER_PID=$!; echo "$RECEIVER_PID" > "$RPF"
  local REXIT=0; wait "$RECEIVER_PID" || REXIT=$?
  [[ $REXIT -eq 0 ]] && for _ in $(seq 1 50); do grep -q "NET bytes_sent:" "$SLOG" 2>/dev/null && break; kill -0 "$SENDER_PID" 2>/dev/null || break; sleep 0.1; done
  touch "$DONE"; wait "$POLL_PID" 2>/dev/null || true; kill "$SENDER_PID" 2>/dev/null || true
  local SRK RRK; SRK=$(cat "$SRSS"); RRK=$(cat "$RRSS")
  if [[ $REXIT -ne 0 ]]; then say "    receiver exited $REXIT (logs -> _FAILED_*)"; cat "$RLOG" >&2; return 1; fi
  # scrape
  local soff roff gcms qms r2s s2r rfvp roth
  soff=$(awk '/^  Time: [0-9]+ ms/{s+=$2} END{print s+0}' "$SLOG")
  roff=$(awk '/^  Time: [0-9]+ ms/{s+=$2} END{print s+0}' "$RLOG")
  gcms=$(awk '/GC blinding complete/{t=$0;sub(/.*\(/,"",t);sub(/ ms.*/,"",t);s+=t} END{print s+0}' "$RLOG")
  qms=$(awk '/^Total time:/{print $3; exit}' "$RLOG" | tr -cd '0-9'); qms=${qms:-0}
  rfvp=$(awk -F'[= ]' '/^Recon us:/{print $4; exit}' "$RLOG" | tr -cd '0-9'); rfvp=${rfvp:-0}
  roth=$(awk -F'[= ]' '/^Recon us:/{print $6; exit}' "$RLOG" | tr -cd '0-9'); roth=${roth:-0}
  r2s=$(awk 'index($0,"NET bytes_sent:"){print $3; exit}' "$RLOG" | tr -cd '0-9'); r2s=${r2s:-0}
  s2r=$(awk 'index($0,"NET bytes_sent:"){print $3; exit}' "$SLOG" | tr -cd '0-9'); s2r=${s2r:-0}
  # verify + merge
  local VJ="$WORK/verify.json"
  python3 "$VERIFY" --rounds-dir "$RND" --expected "$EXPECTED" --query-csv "$QRY" \
    --label-chunks "$K" --token-rounds "$T" --out "$VJ" 2>/dev/null || echo '{}' > "$VJ"
  mkdir -p "$(dirname "$OUT")"
  python3 - "$VJ" "$OUT" <<PYEOF
import json,sys
v=json.load(open(sys.argv[1]))
rec={"config":"$config","db_label":"$DBL","label":"$LBL","label_chunks":int("$K"),
 "token_rounds":int("$T"),"total_rounds":int("$TOTAL_ROUNDS"),"threads":int("$THREADS"),"mode":"$MODE",
 "sender_offline_ms":int("$soff" or 0),"receiver_offline_ms":int("$roff" or 0),
 "gc_online_ms":int("$gcms" or 0),"query_online_ms":int("$qms" or 0),
 "recon_fvp_us":int("$rfvp" or 0),"recon_other_us":int("$roth" or 0),
 "total_online_ms":(int("$gcms" or 0)+int("$qms" or 0)) if "$MODE"=="CSTPSI" else int("$qms" or 0),
 "net_r2s_bytes":int("$r2s" or 0),"net_s2r_bytes":int("$s2r" or 0),
 "net_total_bytes":int("$r2s" or 0)+int("$s2r" or 0),
 "sender_peak_rss_mb":round(int("$SRK" or 0)/1024.0,1),"receiver_peak_rss_mb":round(int("$RRK" or 0)/1024.0,1)}
rec.update(v); json.dump(rec,open(sys.argv[2],"w"),indent=2)
print(f"[cell] {rec['config']} D={rec['db_label']} lbl={rec['label']} T={rec['token_rounds']} thr={rec['threads']}: "
      f"online={rec['total_online_ms']}ms net={rec['net_total_bytes']/1024:.0f}kB "
      f"FRR={rec.get('frr',-1):.4f} FAR={rec.get('far',-1):.6f}",file=sys.stderr)
PYEOF
  trap - RETURN; _cell_cleanup
}

# HARNESS C: soundness via flpsi_experiment (Deep1B / LFW).
run_deep1b(){ # $1=db $2=outdir
  local db="$1" od="$2/deep1b/db${db}"; run "mkdir -p '$od'"
  if [[ "$FORCE" != 1 && -s "$od/run.txt" ]]; then say "  SKIP (done): Deep1B db=$db"; return 0; fi
  local g="${TP:-100}" ra="${TN:-100}"
  say "  Deep1B db=$db (repeats=$REPEATS genuine=$g random=$ra)"
  run "'$FLPSI_BIN' --data '$DEEP1B_BIN' --db-size $db --repeats $REPEATS --max-gen-queries $g \
        --random-impostors $ra --tcstpsi 2 --seed $SEED --nrof-online-threads $SND_THREADS \
        --manifest-out '$od/manifest.txt' --log-queries '$od/qlog.csv' > '$od/run.txt' 2>&1 \
        || echo '    Deep1B db=$db FAILED (see $od/run.txt)'"
}
run_lfw(){ # $1=outdir
  local od="$1/lfw"; run "mkdir -p '$od'"
  if [[ "$FORCE" != 1 && -s "$od/run.txt" ]]; then say "  SKIP (done): LFW"; return 0; fi
  say "  LFW db=$LFW_DB impostors=$LFW_IMP (repeats=$REPEATS)"
  run "'$FLPSI_BIN' --data '$LFW_BIN' --db-size $LFW_DB --impostors $LFW_IMP --repeats $REPEATS \
        --tcstpsi 2 --seed $SEED --nrof-online-threads $SND_THREADS > '$od/run.txt' 2>&1 || echo '    LFW FAILED'"
}

# ----------------------------------------------------------------------------
# Target drivers
# ----------------------------------------------------------------------------
# resolve filters -> concrete lists (longest D first for the heavy work)
SIZES_DEF="1m 100k 10k 1k"; LABELS_DEF="23bit 16byte 32byte 64byte"
PROTS_DEF="STLPSI CSTPSI"
sizes(){ echo "${F_SIZES:-$SIZES_DEF}"; }
labels(){ echo "${F_LABELS:-$LABELS_DEF}"; }
prots(){ echo "${F_PROTOCOLS:-$PROTS_DEF}"; }
# threads: default sweep 8/4/1 for all D, including 1m. CSTPSI runs the full
# 8/4/1 sweep everywhere; STLPSI at 1m/500k is projected analytically for the
# 1/4-thread cells (see stlpsi_analytical.csv), so do_headtohead skips its
# non-8-thread cells at those sizes. The 1/4-thread cells auto-reduce to the
# TS_TP/TS_TN (2+2) query subset, since they report only a per-query scaling.
threads_for(){ local d="$1"; [[ -n "$F_THREADS" ]] && { echo "$F_THREADS"; return; }
  echo "8 4 1"; }

do_headtohead(){  # tab:headtohead + fig:perf-speedup + fig:comm-saving
  say "TARGET headtohead -> tab:headtohead (+ fig:perf-speedup, fig:comm-saving)"
  local Trounds="${F_ROUNDS:-}"
  for D in $(sizes); do
    gen_dataset "$D" "$(labels | tr ' ' ,)" "$(qd_tp "$D")" "$(qd_tn "$D")" "$DATA_DIR"
    local FULLQ="$DATA_DIR/qry_${D}.csv"
    for L in $(labels); do
      for THR in $(threads_for "$D"); do
        for P in $(prots); do
          # STLPSI at 1m/500k is projected analytically for 1/4 threads
          # (stlpsi_analytical.csv); run it 8-thread only. CSTPSI sweeps 8/4/1.
          { [[ "$P" == STLPSI && ( "$D" == 1m || "$D" == 500k ) && "$THR" != 8 ]]; } && continue
          # 1m/500k 1/4-thread CSTPSI: measure only the cheap labels (23bit K=1,
          # 16byte K=6); 32/64byte online time is affine in K (T+K rounds, ~equal
          # per-round cost), so headtohead_table.py extrapolates them from these
          # two points per thread config (validated against the 8-thread grid).
          { [[ ( "$D" == 1m || "$D" == 500k ) && "$THR" != 8 && ( "$L" == 32byte || "$L" == 64byte ) ]]; } && continue
          # STLPSI runs at T=1, CSTPSI at T=2 (the head-to-head pairing), unless --rounds overrides.
          local Tlist; if [[ -n "$Trounds" ]]; then Tlist="$Trounds"; elif [[ "$P" == STLPSI ]]; then Tlist="1"; else Tlist="2"; fi
          # query budget for this cell: full per-D, reduced for the thread-scaling
          # sweep (THR!=8) and for slow STLPSI-long cells; take the smallest. Explicit
          # --tp/--tn disable the auto reductions.
          local tpl tnl; tpl=$(qd_tp "$D"); tnl=$(qd_tn "$D")
          if [[ -z "$TP" && -z "$TN" ]]; then
            [[ "$THR" != 8 ]] && { tpl=$(mn "$tpl" "$TS_TP"); tnl=$(mn "$tnl" "$TS_TN"); }
            { [[ "$P" == STLPSI ]] && is_long "$D" "$L"; } && { tpl=$(mn "$tpl" "$STL_TP"); tnl=$(mn "$tnl" "$STL_TN"); }
          fi
          local QRY; QRY=$(make_qry "$FULLQ" "$tpl" "$tnl" "$DATA_DIR/qry_${D}_${tpl}x${tnl}.csv")
          for T in $Tlist; do
            local cfg; cfg=$(echo "$P"|tr '[:upper:]' '[:lower:]')
            run_network_cell bmain "$D" "$L" "$T" "$THR" "$P" \
              "$RAW_DIR/bmain/${cfg}_D${D}_lbl${L}_T${T}_thr${THR}_p1.json" \
              "$DATA_DIR/enr_${D}_lbl${L}.csv" "$QRY" "$DATA_DIR/expected_${D}_lbl${L}.csv"
          done
        done
      done
    done
  done
  run "python3 '$SUMMARIZE' --raw-dir '$RAW_DIR/bmain' --out '$PROC_DIR/bmain_summary.csv' --exp bmain || true"
}

do_far(){  # fig:far-empirical -- 23bit (label-independent FAR), high TN, all D, T=1 & T=2.
  say "TARGET far -> fig:far-empirical (synthetic RSE curve)"
  local tn="${TN:-100}" tp="${TP:-100}"
  for D in $(sizes); do
    gen_dataset "$D" 23bit "$tp" "$tn" "$FAR_DIR"
    for P in $(prots); do
      local cfg; cfg=$(echo "$P"|tr '[:upper:]' '[:lower:]'); local T; [[ "$P" == STLPSI ]] && T=1 || T=2
      run_network_cell far "$D" 23bit "$T" 8 "$P" \
        "$RAW_DIR/far/${cfg}_D${D}_lbl23bit_T${T}_thr8_p1.json" \
        "$FAR_DIR/enr_${D}_lbl23bit.csv" "$FAR_DIR/qry_${D}.csv" "$FAR_DIR/expected_${D}_lbl23bit.csv"
    done
  done
}

do_ablation(){  # tab:ablation -- timing grid cells (reused) + recon on/off pair.
  say "TARGET ablation -> tab:ablation (caching breakdown)"
  # ensure the grid cells ablation_factors.py reads exist (100k 64/23/16byte CSTPSI T2 thr8, 1m 16/23bit).
  for D in 100k 1m 10k 1k; do has "$D" "$(sizes)" || continue
    gen_dataset "$D" "23bit,16byte,32byte,64byte" "$(qd_tp "$D")" "$(qd_tn "$D")" "$DATA_DIR"
    for L in 23bit 16byte 64byte; do
      run_network_cell bmain "$D" "$L" 2 8 CSTPSI \
        "$RAW_DIR/$([[ "$L" == 64byte || "$L" == 32byte ]] && echo blabel || echo bmain)/cstpsi_D${D}_lbl${L}_T2_thr8_p1.json" \
        "$DATA_DIR/enr_${D}_lbl${L}.csv" "$DATA_DIR/qry_${D}.csv" "$DATA_DIR/expected_${D}_lbl${L}.csv"
    done
  done
  # recon on/off pair (100k/23bit). Same cell, inverse cache forced off via env.
  if has 100k "$(sizes)"; then
    say "  recon on/off (100k/23bit, T=2, 8 threads)"
    local rv="$RAW_DIR/_recon_validate"; run "mkdir -p '$rv'"
    # export (not prefix): the flag is read by the receiver child via getenv.
    export CSTPSI_DISABLE_INV_CACHE=""
    run_network_cell recon 100k 23bit 2 8 CSTPSI \
      "$rv/cstpsi_D100k_lbl23bit_T2_thr8_invON_p1.json" \
      "$DATA_DIR/enr_100k_lbl23bit.csv" "$DATA_DIR/qry_100k.csv" "$DATA_DIR/expected_100k_lbl23bit.csv"
    export CSTPSI_DISABLE_INV_CACHE=1
    run_network_cell recon 100k 23bit 2 8 CSTPSI \
      "$rv/cstpsi_D100k_lbl23bit_T2_thr8_invOFF_p1.json" \
      "$DATA_DIR/enr_100k_lbl23bit.csv" "$DATA_DIR/qry_100k.csv" "$DATA_DIR/expected_100k_lbl23bit.csv"
    unset CSTPSI_DISABLE_INV_CACHE
  fi
  run "python3 '$ABLATION' > '$PROC_DIR/ablation_report.txt' 2>&1 || echo '    ablation_factors.py FAILED'"
}

do_deep1b(){
  say "TARGET deep1b -> tab:deep1b (Deep1B kernel soundness)"
  ensure_data "$DEEP1B_BIN"
  [[ -f "$DEEP1B_BIN" ]] || { say "  SKIP: $DEEP1B_BIN missing (ship the artifact dataset)."; return 0; }
  local dbs="$DEEP1B_DBS"; [[ -n "$F_SIZES" ]] && { dbs=""; for s in $F_SIZES; do dbs="$dbs $(db_int "$s")"; done; }
  for db in $dbs; do run_deep1b "$db" "$SND_OUT"; done
}
do_lfw(){
  say "TARGET lfw -> tab:lfw (LFW real-data accuracy)"
  ensure_data "$LFW_BIN"
  [[ -f "$LFW_BIN" ]] || { say "  SKIP: $LFW_BIN missing (ship the artifact dataset)."; return 0; }
  run_lfw "$SND_OUT"
}

# ----------------------------------------------------------------------------
# Dispatch
# ----------------------------------------------------------------------------
SND_OUT="$SCRIPT_DIR/results/release_$STAMP/soundness"
MANIFEST="$SCRIPT_DIR/results/release_$STAMP/MANIFEST.txt"

say "==== CSTPSI reproduction ===="
say "git=$GIT_SHA targets=[$(echo $TARGETS|xargs)]  sizes=[$(sizes)] labels=[$(labels)] protocols=[$(prots)] repeats=$REPEATS"
say "threads: online=[per-cell sweep]  offline(--nrof-offline-threads)=${PREP_THREADS:-all cores (binary default)}  soundness=$SND_THREADS"
[[ "$DRY_RUN" == 1 ]] && say "(DRY RUN -- printing the plan, executing nothing)"
for b in cstpsi_sender cstpsi_receiver flpsi_experiment; do
  [[ -x "$BUILD_DIR/$b" ]] || { say "MISSING $BUILD_DIR/$b -- build first: cmake --build build -j"; [[ "$DRY_RUN" == 1 ]] || exit 1; }
done
echo

# network targets first (longest D first inside each), then soundness.
NEEDS_POOL=0
for t in headtohead far ablation; do has "$t" "$TARGETS" && { do_$t; NEEDS_POOL=1; echo; }; done
for t in deep1b lfw; do has "$t" "$TARGETS" && { do_$t; echo; }; done

# FAR pool + STLPSI projection (network analysis) if any network target ran.
if [[ "$NEEDS_POOL" == 1 ]]; then
  say "process: pool FAR/FRR + project STLPSI"
  run "python3 '$SUMMARIZE' --raw-dir '$RAW_DIR' --recursive --pool-far-frr --out '$PROC_DIR/far_frr_pooled.csv' || true"
  run "python3 '$ANALYTICAL' --raw-dir '$RAW_DIR' --recursive --out '$PROC_DIR/stlpsi_analytical.csv' || true"
fi

# manifest
if [[ "$DRY_RUN" != 1 ]]; then
  mkdir -p "$(dirname "$MANIFEST")"
  { echo "CSTPSI reproduction $STAMP  git=$GIT_SHA  targets=[$(echo $TARGETS|xargs)]"
    echo "sizes=[$(sizes)] labels=[$(labels)] protocols=[$(prots)] repeats=$REPEATS tp=${TP:-auto} tn=${TN:-auto}"
    echo
    has headtohead "$TARGETS" && echo "tab:headtohead, fig:perf-speedup, fig:comm-saving -> $PROC_DIR/{bmain,blabel}_summary.csv, stlpsi_analytical.csv"
    has far        "$TARGETS" && echo "fig:far-empirical (RSE curve)                  -> $PROC_DIR/far_frr_pooled.csv"
    has ablation   "$TARGETS" && echo "tab:ablation                                   -> $PROC_DIR/ablation_report.txt"
    has deep1b     "$TARGETS" && echo "tab:deep1b                                     -> $SND_OUT/deep1b/db*/run.txt"
    has lfw        "$TARGETS" && echo "tab:lfw                                        -> $SND_OUT/lfw/run.txt"
    echo; echo "Verify against docs/paper + experiments/PAPER_NUMBERS.md, then update the .tex."
  } > "$MANIFEST"
  say "manifest -> $MANIFEST"
fi
say "==== DONE ===="
