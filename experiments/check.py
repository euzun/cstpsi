#!/usr/bin/env python3
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
"""
check.py -- PASS/FAIL auditor over per-cell result JSONs.

Reads results/raw/<...>/*.json (recursively) and gives one verdict so you don't
have to eyeball every cell. Designed for the --smoke shakeout (point it at
results/raw/smoke) but works on real runs too.

Structural checks only -- "did every cell run and capture usable data":
  FAIL  total_online_ms == 0     (crash or scrape failure -> no usable data)
  FAIL  no queries recorded
  WARN  receiver_peak_rss_mb == 0  (RSS not captured)
  WARN  net_total_bytes == 0      (wire bytes not captured)

FAR / FRR / label_mismatch are REPORTED per cell, never judged: the reader
interprets them against the expected outcome (e.g. T=1 FAR rises with D; T>=2
drives it to ~0). This auditor only confirms the suite ran and produced data.

Crashed cells write no JSON, so absence is detected by comparing the per-exp
cell count against the expected grid (a reference; m1 is 5 at <=100k, 7 with
--full).

Exit: 0 if no FAILs, 1 otherwise.

Usage:
  python3 experiments/check.py --raw-dir experiments/results/raw/smoke
"""

import argparse
import glob
import json
import os
import sys

# Reference cell counts per experiment (for completeness, not a hard fail
# except when zero). m1 is 5 (<=100k) or 7 (--full); r1 16-thread etc.
EXPECTED = {"m6": 2, "m1": 5, "m2": 9, "m3": 16, "r1": 5, "r2": 4}


def check_cell(rec):
    """Return (fails, warns). STRUCTURAL only: did the cell run and capture data?
    FAR / FRR / label_mismatch are REPORTED (see data_line), never judged here --
    the reader interprets them against the expected outcome."""
    fails, warns = [], []
    if (rec.get("total_online_ms", 0) or 0) == 0:
        fails.append("total_online_ms=0 (crash or scrape failure -> no usable data)")
    if ((rec.get("n_tp", 0) or 0) + (rec.get("n_tn", 0) or 0)) == 0:
        fails.append("no queries recorded")
    if (rec.get("receiver_peak_rss_mb", 0) or 0) == 0:
        warns.append("receiver RSS not captured")
    if (rec.get("net_total_bytes", 0) or 0) == 0:
        warns.append("net bytes not captured")
    return fails, warns


def data_line(rec):
    """One-line factual report of a cell's measured outcome (no judgment)."""
    return (f"T={rec.get('token_rounds')} thr={rec.get('threads')} "
            f"online={rec.get('total_online_ms')}ms "
            f"rssR={rec.get('receiver_peak_rss_mb')}MB "
            f"net={(rec.get('net_total_bytes',0) or 0)//1024}kB | "
            f"frr={rec.get('frr')} far={rec.get('far')} "
            f"label_mismatch={rec.get('label_mismatch')}")


def main():
    p = argparse.ArgumentParser(description="PASS/FAIL auditor for cell result JSONs")
    p.add_argument("--raw-dir", default="experiments/results/raw/smoke")
    p.add_argument("--quiet", action="store_true", help="only print FAIL/WARN cells + verdict")
    args = p.parse_args()

    if not os.path.isdir(args.raw_dir):
        print(f"check: no such dir: {args.raw_dir} (run the suite first)", file=sys.stderr)
        return 1

    paths = sorted(glob.glob(os.path.join(args.raw_dir, "**", "*.json"), recursive=True))
    if not paths:
        print(f"check: no cell JSONs under {args.raw_dir}", file=sys.stderr)
        return 1

    per_exp = {}      # exp -> count
    n_fail = n_warn = n_pass = 0
    for path in paths:
        # Skip the isolated smoke namespace when auditing real results/raw.
        if os.path.relpath(path, args.raw_dir).split(os.sep)[0] == "smoke":
            continue
        exp = os.path.basename(os.path.dirname(path))
        per_exp[exp] = per_exp.get(exp, 0) + 1
        try:
            rec = json.load(open(path))
        except Exception as e:
            print(f"  FAIL {os.path.relpath(path, args.raw_dir)}: unreadable JSON ({e})")
            n_fail += 1
            continue
        fails, warns = check_cell(rec)
        name = os.path.relpath(path, args.raw_dir)
        if fails:
            n_fail += 1
            print(f"  FAIL {name}: {'; '.join(fails)}  [{data_line(rec)}]")
        elif warns:
            n_warn += 1
            if not args.quiet:
                print(f"  WARN {name}: {'; '.join(warns)}  [{data_line(rec)}]")
        else:
            n_pass += 1
            if not args.quiet:
                print(f"  ok   {name}: {data_line(rec)}")

    print("\n--- per-experiment cell counts ---")
    for exp in sorted(per_exp):
        exp_key = exp if exp in EXPECTED else exp.rstrip("0123456789")
        ref = EXPECTED.get(exp_key)
        flag = ""
        if ref and per_exp[exp] < ref:
            flag = f"  <-- only {per_exp[exp]}/{ref} expected (missing/crashed cells?)"
        print(f"  {exp:6} {per_exp[exp]:>3} cells"
              + (f" (expected ~{ref})" if ref else "") + flag)

    # Crashed cells write no JSON, only _FAILED_<cell>.{sender,receiver}.log.
    # Count them as failures so the verdict is honest.
    failed_logs = [p for p in sorted(glob.glob(os.path.join(args.raw_dir, "**", "_FAILED_*.sender.log"),
                                               recursive=True))
                   if os.path.relpath(p, args.raw_dir).split(os.sep)[0] != "smoke"]
    n_crashed = len(failed_logs)
    if n_crashed:
        print(f"\n--- {n_crashed} CRASHED cell(s) (no JSON; see _FAILED_*.log) ---")
        for lg in failed_logs:
            cell = os.path.basename(lg)[len("_FAILED_"):-len(".sender.log")]
            exp = os.path.basename(os.path.dirname(lg))
            print(f"  CRASH {exp}/{cell}")

    total = n_pass + n_warn + n_fail
    ok = (n_fail == 0 and n_crashed == 0)
    print(f"\n=== {'PASS' if ok else 'FAIL'}: "
          f"{n_pass} ok, {n_warn} warn, {n_fail} fail, {n_crashed} crashed "
          f"of {total + n_crashed} cells in {args.raw_dir} ===")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
