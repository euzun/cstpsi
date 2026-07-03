#!/usr/bin/env python3
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
"""
summarize.py -- roll per-cell JSON (results/raw/<exp>/*.json) into one tidy CSV
in results/processed/, the hand-off interface to the paper session.

Two modes:
  default      one row per cell; columns = stable preferred order + extras.
               First line is a '#'-comment documenting schema + provenance.
  --frr-audit  walk --raw-dir recursively; one row per cell with its FRR;
               prints PASS/FAIL (all FRR must be 0) to stderr.  Used by M5.

Usage:
  python3 experiments/summarize.py --raw-dir results/raw/m1 --out results/processed/m1_summary.csv --exp m1
  python3 experiments/summarize.py --raw-dir results/raw --recursive --frr-audit --out results/processed/m5_frr_audit.csv
"""

import argparse
import csv
import glob
import json
import math
import os
import sys
from datetime import datetime, timezone

D_OF = {"1k": 1000, "5k": 5000, "10k": 10000, "50k": 50000,
        "100k": 100000, "500k": 500000, "1m": 1000000}

Z95 = 1.959963984540054  # standard normal 97.5th percentile


def wilson_ci(x, n, z=Z95):
    """Wilson score interval for a binomial proportion. Returns (lo, hi)."""
    if n == 0:
        return (0.0, 0.0)
    p = x / n
    z2 = z * z
    denom = 1.0 + z2 / n
    center = (p + z2 / (2 * n)) / denom
    half = (z * math.sqrt(p * (1 - p) / n + z2 / (4 * n * n))) / denom
    return (max(0.0, center - half), min(1.0, center + half))


def d_int(label):
    if label in D_OF:
        return D_OF[label]
    if label.startswith("d") and label[1:].isdigit():
        return int(label[1:])
    return 0


# Preferred leading columns; any other keys are appended alphabetically.
PREFERRED = [
    "exp", "config", "mode", "db_label", "D", "label", "label_chunks",
    "token_rounds", "total_rounds", "threads",
    "n_tp", "n_tn", "missed_tp", "false_accept_tn", "label_mismatch",
    "frr", "far", "far_wilson_lo", "far_wilson_hi",
    "sender_offline_ms", "receiver_offline_ms", "gc_online_ms",
    "query_online_ms", "total_online_ms", "per_query_online_ms",
    "net_r2s_bytes", "net_s2r_bytes", "net_total_bytes",
    "sender_peak_rss_mb", "receiver_peak_rss_mb",
]


def load_cells(raw_dir, recursive):
    pattern = os.path.join(raw_dir, "**", "*.json") if recursive else os.path.join(raw_dir, "*.json")
    rows = []
    for path in sorted(glob.glob(pattern, recursive=recursive)):
        # Skip the isolated smoke namespace when recursing real results/raw, so
        # 2-sample smoke cells don't pollute the real pooled/summary numbers.
        # (When raw_dir IS .../smoke, relpath won't start with 'smoke/', so kept.)
        if os.path.relpath(path, raw_dir).split(os.sep)[0] == "smoke":
            continue
        try:
            rec = json.load(open(path))
        except Exception as e:
            print(f"[summarize] skip {path}: {e}", file=sys.stderr)
            continue
        rec.setdefault("D", d_int(rec.get("db_label", "")))
        # per-query amortized online latency (folds in the old B3 overlay)
        nq = (rec.get("n_tp", 0) or 0) + (rec.get("n_tn", 0) or 0)
        if nq and rec.get("query_online_ms") is not None:
            rec["per_query_online_ms"] = round(rec["query_online_ms"] / nq, 4)
        rec.setdefault("_src", os.path.basename(path))
        rec.setdefault("exp", os.path.basename(os.path.dirname(path)))
        rows.append(rec)
    return rows


def sort_key(r):
    return (r.get("config", ""), r.get("D", 0), r.get("label", ""),
            r.get("token_rounds", 0), r.get("threads", 0))


def write_csv(rows, out, exp):
    if not rows:
        print(f"[summarize] no cells found for {exp}; nothing written.", file=sys.stderr)
        return
    extra = sorted({k for r in rows for k in r if k not in PREFERRED and not k.startswith("_")})
    cols = [c for c in PREFERRED if any(c in r for r in rows)] + extra
    rows.sort(key=sort_key)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", newline="") as fh:
        ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        fh.write(f"# {exp} summary | generated {ts} | {len(rows)} cells | "
                 f"schema: one row per cell; ms=milliseconds, bytes=wire bytes, "
                 f"rss=peak resident MB, far_wilson_*=95% Wilson CI\n")
        w = csv.DictWriter(fh, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow(r)
    print(f"[summarize] wrote {out} ({len(rows)} cells, {len(cols)} cols)", file=sys.stderr)


def frr_audit(rows, out):
    os.makedirs(os.path.dirname(out), exist_ok=True)
    cols = ["exp", "config", "db_label", "token_rounds", "threads",
            "n_tp", "missed_tp", "frr", "label_mismatch", "_src"]
    fails = 0
    rows.sort(key=lambda r: (r.get("exp", ""), sort_key(r)))
    with open(out, "w", newline="") as fh:
        fh.write("# M5 FRR audit | all cells must have frr==0 and label_mismatch==0\n")
        w = csv.DictWriter(fh, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow(r)
            if (r.get("frr", 0) or 0) > 0 or (r.get("label_mismatch", 0) or 0) > 0:
                fails += 1
                print(f"[m5] FAIL: {r.get('_src')} frr={r.get('frr')} "
                      f"label_mismatch={r.get('label_mismatch')}", file=sys.stderr)
    total = len(rows)
    if total == 0:
        print("[m5] no cells found -- run M1/M2/M3 first.", file=sys.stderr)
    elif fails == 0:
        print(f"[m5] PASS: {total}/{total} cells FRR=0 and labels exact.", file=sys.stderr)
    else:
        print(f"[m5] {fails}/{total} cells FAILED FRR/label check.", file=sys.stderr)
    print(f"[summarize] wrote {out}", file=sys.stderr)


def pool_far_frr(rows, out):
    """Pool FAR/FRR by (D, token_rounds) across ALL cells. FAR is (D,T)-only and
    optimization/label/thread-independent, so every cell at a given (D,T)
    contributes TN/TP samples -- they accumulate suite-wide. The headline
    security table."""
    groups = {}  # (D, T) -> accumulators
    for r in rows:
        key = (r.get("D", 0), r.get("token_rounds", 0))
        g = groups.setdefault(key, {"n_cells": 0, "tn": 0, "fa": 0, "tp": 0,
                                     "missed": 0, "lblmis": 0})
        g["n_cells"] += 1
        g["tn"] += r.get("n_tn", 0) or 0
        g["fa"] += r.get("false_accept_tn", 0) or 0
        g["tp"] += r.get("n_tp", 0) or 0
        g["missed"] += r.get("missed_tp", 0) or 0
        g["lblmis"] += r.get("label_mismatch", 0) or 0

    cols = ["D", "token_rounds", "n_cells", "total_tn", "false_accept",
            "far", "far_wilson_lo", "far_wilson_hi",
            "total_tp", "missed_tp", "frr", "frr_wilson_lo", "frr_wilson_hi",
            "label_mismatch"]
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", newline="") as fh:
        ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        fh.write(f"# FAR/FRR pooled by (D,T) | generated {ts} | "
                 f"far=false_accept/total_tn, frr=missed_tp/total_tp, "
                 f"*_wilson_*=95% Wilson CI; pooled over all cells at each (D,T)\n")
        w = csv.DictWriter(fh, fieldnames=cols)
        w.writeheader()
        for (D, T) in sorted(groups):
            g = groups[(D, T)]
            far = g["fa"] / g["tn"] if g["tn"] else 0.0
            flo, fhi = wilson_ci(g["fa"], g["tn"])
            frr = g["missed"] / g["tp"] if g["tp"] else 0.0
            rlo, rhi = wilson_ci(g["missed"], g["tp"])
            w.writerow({"D": D, "token_rounds": T, "n_cells": g["n_cells"],
                        "total_tn": g["tn"], "false_accept": g["fa"],
                        "far": f"{far:.6g}", "far_wilson_lo": f"{flo:.3e}",
                        "far_wilson_hi": f"{fhi:.3e}", "total_tp": g["tp"],
                        "missed_tp": g["missed"], "frr": f"{frr:.6g}",
                        "frr_wilson_lo": f"{rlo:.3e}", "frr_wilson_hi": f"{rhi:.3e}",
                        "label_mismatch": g["lblmis"]})
    print(f"[summarize] wrote {out} ({len(groups)} (D,T) cells pooled "
          f"from {len(rows)} raw cells)", file=sys.stderr)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--raw-dir", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--exp", default="exp")
    p.add_argument("--recursive", action="store_true")
    p.add_argument("--frr-audit", action="store_true")
    p.add_argument("--pool-far-frr", action="store_true")
    args = p.parse_args()

    rows = load_cells(args.raw_dir, args.recursive or args.frr_audit or args.pool_far_frr)
    if args.frr_audit:
        frr_audit(rows, args.out)
    elif args.pool_far_frr:
        pool_far_frr(rows, args.out)
    else:
        write_csv(rows, args.out, args.exp)


if __name__ == "__main__":
    main()
