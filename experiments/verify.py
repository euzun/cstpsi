#!/usr/bin/env python3
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
"""
verify.py -- CSTPSI FRR/FAR verifier with Wilson 95% CI (locked-plan edition).

Consumes the per-round result CSVs written by cstpsi_receiver (--outputDir),
splits queries into TP/TN via the `is_tp` column, and computes:

  FRR  = missed_TP / n_TP          (correctness; expected 0)
  FAR  = false_accept_TN / n_TN    (security; the headline FAR claim)
  FAR Wilson 95% confidence interval

Multi-token-round semantics (matches the protocol): rounds 0..T-1 are token
rounds, rounds T..T+k-1 are label-chunk rounds.  The protocol ACCEPTS a query
iff its pair-level verified-pairs set (findVerifiedPairs: the (partition,i,j)
pairs that reconstruct the token in ALL T token rounds) is non-empty, which is
exactly what reconstructLabels writes to the LABEL rounds.  So acceptance --
hence FAR and FRR -- is read from the label round, NOT a query-level AND of the
token-round CSVs: a TN's spurious token matches sit at different (partition,i,j)
each round, so the pair-level intersection (and thus the label round) is empty
even when every individual token round has some spurious match.

Emits a JSON object (stdout or --out) so summarize.py can roll cells up without
re-parsing free text.  Also writes the legacy two-line FRR=/FAR= file if
--output-txt is given (back-compat with run_cstpsi_bench.sh consumers).

Usage:
  python3 experiments/verify.py \
      --rounds-dir /tmp/rounds --expected expected_1k_lbl23bit.csv \
      --query-csv qry_1k.csv --label-chunks 1 --token-rounds 1 --out cell.json
"""

import argparse
import csv
import json
import math
import os
import sys
from pathlib import Path

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


def load_query_flags(query_csv):
    """row_index (0-based, = protocol query id) -> is_tp."""
    flags = {}
    with open(query_csv, newline="") as fh:
        for i, row in enumerate(csv.DictReader(fh)):
            flags[i] = int(row.get("is_tp", 0))
    return flags


def load_expected_labels(expected_csv):
    expected = {}
    with open(expected_csv, newline="") as fh:
        reader = csv.DictReader(fh)
        cols = sorted((c for c in (reader.fieldnames or []) if c.startswith("lbl_")),
                      key=lambda c: int(c.split("_")[1]))
        for row in reader:
            expected[int(row["query_id"])] = [int(row[c]) for c in cols]
    return expected


def load_round_results(round_csv):
    results = {}
    with open(round_csv, newline="") as fh:
        for row in csv.DictReader(fh):
            qid = int(row["query_id"])
            raw = row.get("matched_ids", "").strip().strip('"')
            mids = [int(x) for x in raw.split() if x] if raw else []
            results[qid] = {"match_count": int(row["match_count"]), "matched_ids": mids}
    return results


def verify(rounds_dir, expected_csv, query_csv, k, t):
    flags = load_query_flags(query_csv)
    expected = load_expected_labels(expected_csv)
    if not flags:
        raise RuntimeError("no queries found in query CSV")

    total_rounds = t + k
    rounds = []
    for r in range(total_rounds):
        path = os.path.join(rounds_dir, f"round_{r}.csv")
        if not os.path.isfile(path):
            raise FileNotFoundError(f"missing round result file: {path}")
        rounds.append(load_round_results(path))

    tp_ids = [q for q, f in flags.items() if f == 1]
    tn_ids = [q for q, f in flags.items() if f == 0]

    # A query is accepted iff the protocol reconstructed at least one label for
    # it -- i.e. its verified-pairs set was non-empty -- which is recorded as a
    # non-zero match_count in the label round. The label rounds are indices
    # t..t+k-1; the first one (index t) reflects the verified-pairs set. For the
    # legacy single-round mode (k == 0, no label rounds) fall back to round 0.
    accept_round = 0 if k == 0 else t

    def accepted(qid):
        return rounds[accept_round].get(qid, {"match_count": 0})["match_count"] > 0

    missed_tp = 0
    label_mismatch = 0
    for qid in tp_ids:
        if not accepted(qid):
            missed_tp += 1
            continue
        # Skip label verification when k==0 (single-round mode, no label chunks to verify)
        if k == 0:
            continue
        if qid in expected and len(expected[qid]) == k:
            recon = []
            for c in range(k):
                mids = rounds[t + c].get(qid, {"matched_ids": []})["matched_ids"]
                if not mids:
                    label_mismatch += 1
                    break
                recon.append(mids[0])
            else:
                if recon != expected[qid]:
                    label_mismatch += 1

    false_accept = sum(1 for qid in tn_ids if accepted(qid))

    n_tp, n_tn = len(tp_ids), len(tn_ids)
    frr = missed_tp / n_tp if n_tp else 0.0
    far = false_accept / n_tn if n_tn else 0.0
    far_lo, far_hi = wilson_ci(false_accept, n_tn)

    return {
        "n_tp": n_tp, "n_tn": n_tn,
        "missed_tp": missed_tp, "false_accept_tn": false_accept,
        "label_mismatch": label_mismatch,
        "frr": frr, "far": far,
        "far_wilson_lo": far_lo, "far_wilson_hi": far_hi,
        "token_rounds": t, "label_chunks": k,
    }


def main():
    p = argparse.ArgumentParser(description="CSTPSI FRR/FAR verifier with Wilson CI")
    p.add_argument("--rounds-dir", required=True)
    p.add_argument("--expected", required=True)
    p.add_argument("--query-csv", required=True)
    p.add_argument("--label-chunks", type=int, required=True)
    p.add_argument("--token-rounds", type=int, default=1)
    p.add_argument("--out", help="write JSON result here (default: stdout)")
    p.add_argument("--output-txt", help="also write legacy FRR=/FAR= two-line file")
    args = p.parse_args()

    for path, name in [(args.rounds_dir, "--rounds-dir"),
                       (args.expected, "--expected"),
                       (args.query_csv, "--query-csv")]:
        if not os.path.exists(path):
            sys.exit(f"ERROR: {name} path does not exist: {path}")

    res = verify(args.rounds_dir, args.expected, args.query_csv,
                 args.label_chunks, args.token_rounds)

    blob = json.dumps(res, indent=2)
    if args.out:
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out).write_text(blob + "\n")
    else:
        print(blob)

    if args.output_txt:
        Path(args.output_txt).parent.mkdir(parents=True, exist_ok=True)
        with open(args.output_txt, "w") as fh:
            fh.write(f"FRR={res['frr']:.6f}\n")
            fh.write(f"FAR={res['far']:.6f}\n")

    # Human-readable summary to stderr (doesn't pollute --out stdout JSON)
    print(f"[verify] T={res['token_rounds']} k={res['label_chunks']}  "
          f"TP={res['n_tp']} TN={res['n_tn']}  "
          f"FRR={res['frr']:.4f} ({res['missed_tp']} missed)  "
          f"FAR={res['far']:.6f} ({res['false_accept_tn']}/{res['n_tn']}, "
          f"95% CI [{res['far_wilson_lo']:.2e}, {res['far_wilson_hi']:.2e}])",
          file=sys.stderr)
    if res["label_mismatch"]:
        print(f"[verify] WARNING: {res['label_mismatch']} label mismatches", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
