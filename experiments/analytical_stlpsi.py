#!/usr/bin/env python3
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
"""
analytical_stlpsi.py -- project STLPSI online cost AND communication from CSTPSI
components, and cross-check the projection against directly-measured STLPSI cells.

WHY. STLPSI (the no-caching baseline) re-runs the full bundled garbled-circuit
(GC) blinding AND re-sends/re-evaluates the query on EVERY protocol round, while
CSTPSI runs the GC ONCE per query (1-GC) and uploads the query ONCE (send-once),
paying only the per-round FHE token evaluation thereafter. At realistic query
counts the STLPSI cells are dominated by that repeated GC and are very slow to
run (~minutes/query). Rather than burn time measuring the heavy STLPSI grid, we
MEASURE the fast CSTPSI grid and ANALYTICALLY project the STLPSI cost from the
per-component CSTPSI instrumentation. This script computes that projection and,
where a measured STLPSI cell exists, reports projection/measurement ratios so the
model is validated in the paper rather than asserted.

MODEL (rounds = token_rounds T + label_chunks K; STLPSI is T=1, CSTPSI is T=2/3).
All CSTPSI timing/comm columns are ACCUMULATED over the cell's Q = n_tp+n_tn
queries, so we first reduce to PER-QUERY costs (divide by Q). This makes the
projection and the pred/meas ratios independent of query count -- a 1-2 query
STLPSI validation run can therefore be checked against a full-Q CSTPSI grid.

  Per query, CSTPSI measures:
    gc_pq    = gc_online_ms    / Q   -- ONE garbled-circuit pass (reused across rounds)
    query_pq = query_online_ms / Q   -- FHE eval + match, accumulated over (T_c+K) rounds
    r2s_pq   = net_r2s_bytes   / Q   -- query upload, sent ONCE in CSTPSI
    s2r_pq   = net_s2r_bytes   / Q   -- responses, accumulated over (T_c+K) rounds

  STLPSI repeats BOTH compute AND comm every round over its (1+K) rounds:
    s_rounds = 1 + K,   c_rounds = T_c + K
    TIME : pred_pq      = s_rounds*gc_pq + (s_rounds/c_rounds)*query_pq
    COMM : pred_r2s_pq  = s_rounds*r2s_pq                 # query re-uploaded every round
           pred_s2r_pq  = (s_rounds/c_rounds)*s2r_pq      # per-round response x STLPSI rounds
           pred_net_pq  = pred_r2s_pq + pred_s2r_pq
  GC dominates the time term (~95%+), so pred_time ~ (1+K)*gc -- the locked
  "fat-STLPSI = CSTPSI components x rounds" argument. COMM is deterministic, so its
  projection is exact (validated to the byte against measured STLPSI).

Inputs are the raw per-pass JSONs; cells are pass-aggregated (median) first, so
the projection uses the same numbers the tables report. CSTPSI and STLPSI cells
are matched on (db_label, label, threads).

Usage:
  python3 experiments/analytical_stlpsi.py \
      --raw-dir experiments/results/raw --recursive \
      --out experiments/results/processed/stlpsi_analytical.csv
"""

import argparse
import csv
import glob
import json
import os
import statistics
import sys
from datetime import datetime, timezone

D_OF = {"1k": 1000, "5k": 5000, "10k": 10000, "50k": 50000,
        "100k": 100000, "500k": 500000, "1m": 1000000}

AVG = ["gc_online_ms", "query_online_ms", "total_online_ms",
       "net_r2s_bytes", "net_s2r_bytes", "net_total_bytes"]


def d_int(label):
    if label in D_OF:
        return D_OF[label]
    if label and label.startswith("d") and label[1:].isdigit():
        return int(label[1:])
    return 0


def q_count(rec):
    """Queries per cell = n_tp + n_tn (timing/comm columns are accumulated over
    these). Falls back to 1 if absent so a malformed JSON does not blow up."""
    tp, tn = rec.get("n_tp"), rec.get("n_tn")
    if tp is None and tn is None:
        return rec.get("n_queries", 1) or 1
    return (tp or 0) + (tn or 0) or 1


def load_and_average(raw_dir, recursive):
    """Load raw per-pass JSONs and take the MEDIAN of the timing/comm columns per
    cell (robust to transient run-to-run outliers). Cell key =
    (mode, db_label, label, token_rounds, threads); RUN_TAG ignored. Query count
    is carried so accumulated columns can be reduced to per-query."""
    pattern = os.path.join(raw_dir, "**", "*.json") if recursive else os.path.join(raw_dir, "*.json")
    groups = {}
    for path in sorted(glob.glob(pattern, recursive=recursive)):
        if os.path.relpath(path, raw_dir).split(os.sep)[0] == "smoke":
            continue
        try:
            r = json.load(open(path))
        except Exception as e:
            print(f"[analytical] skip {path}: {e}", file=sys.stderr)
            continue
        key = (r.get("mode"), r.get("db_label"), r.get("label"),
               r.get("token_rounds"), r.get("threads"))
        groups.setdefault(key, []).append(r)

    cells = {}
    for key, recs in groups.items():
        c = {"mode": key[0], "db_label": key[1], "label": key[2],
             "token_rounds": key[3], "threads": key[4],
             "D": d_int(key[1] or ""), "label_chunks": recs[0].get("label_chunks"),
             "n_passes": len(recs),
             "q_count": int(statistics.median([q_count(r) for r in recs]))}
        for col in AVG:
            vals = [r.get(col) for r in recs if r.get(col) is not None]
            c[col] = statistics.median(vals) if vals else None
        cells[key] = c
    return cells


def project(cells):
    """For each CSTPSI cell, project STLPSI time AND comm from per-query
    components; match a measured STLPSI cell (same db_label,label,threads) when
    present and report Q-independent pred/meas ratios."""
    stlpsi = {(c["db_label"], c["label"], c["threads"]): c
              for c in cells.values() if c["mode"] == "STLPSI"}
    out = []
    for c in cells.values():
        if c["mode"] != "CSTPSI":
            continue
        K = c.get("label_chunks")
        gc, q = c.get("gc_online_ms"), c.get("query_online_ms")
        r2s, s2r = c.get("net_r2s_bytes"), c.get("net_s2r_bytes")
        if K is None or gc is None or q is None:
            continue
        Qc = c["q_count"]
        # per-query CSTPSI components
        gc_pq, q_pq = gc / Qc, q / Qc
        r2s_pq = (r2s / Qc) if r2s is not None else None
        s2r_pq = (s2r / Qc) if s2r is not None else None

        s_rounds, c_rounds = 1 + K, c["token_rounds"] + K
        # TIME projection (per query, then scaled back to the cell's Q for the table)
        pred_time_pq = s_rounds * gc_pq + (s_rounds / c_rounds) * q_pq
        gc_term_pq = s_rounds * gc_pq
        # COMM projection (deterministic -> exact): query re-uploaded every round,
        # responses are per-round x STLPSI rounds.
        pred_net_pq = None
        if r2s_pq is not None and s2r_pq is not None:
            pred_net_pq = s_rounds * r2s_pq + (s_rounds / c_rounds) * s2r_pq

        row = {
            "D": c["D"], "db_label": c["db_label"], "label": c["label"],
            "label_chunks": K, "threads": c["threads"],
            "cstpsi_T": c["token_rounds"], "stlpsi_rounds": s_rounds,
            "Qc": Qc, "n_passes_cstpsi": c["n_passes"],
            "cstpsi_gc_ms_pq": round(gc_pq, 2), "cstpsi_query_ms_pq": round(q_pq, 2),
            "cstpsi_total_ms_pq": round((c.get("total_online_ms") or (gc + q)) / Qc, 2),
            "pred_stlpsi_ms_pq": round(pred_time_pq, 2),
            "pred_stlpsi_total_ms": round(pred_time_pq * Qc, 1),
            "gc_share_of_pred": f"{gc_term_pq / pred_time_pq:.3f}" if pred_time_pq else "",
            "cstpsi_net_MiB_pq": round((c.get("net_total_bytes") or 0) / Qc / 2**20, 3),
            "pred_stlpsi_net_MiB_pq": round(pred_net_pq / 2**20, 3) if pred_net_pq else "",
        }
        # cross-check vs a measured STLPSI cell (Q-independent: compare per-query)
        m = stlpsi.get((c["db_label"], c["label"], c["threads"]))
        if m and m.get("total_online_ms"):
            Qs = m["q_count"]
            meas_time_pq = m["total_online_ms"] / Qs
            row["meas_stlpsi_ms_pq"] = round(meas_time_pq, 2)
            row["n_passes_stlpsi"] = m["n_passes"]
            row["pred_over_meas_time"] = f"{pred_time_pq / meas_time_pq:.3f}" if meas_time_pq else ""
            if pred_net_pq and m.get("net_total_bytes"):
                meas_net_pq = m["net_total_bytes"] / Qs
                row["meas_stlpsi_net_MiB_pq"] = round(meas_net_pq / 2**20, 3)
                row["pred_over_meas_comm"] = f"{pred_net_pq / meas_net_pq:.3f}" if meas_net_pq else ""
            else:
                row["meas_stlpsi_net_MiB_pq"] = ""
                row["pred_over_meas_comm"] = ""
        else:
            row.update(meas_stlpsi_ms_pq="", n_passes_stlpsi="", pred_over_meas_time="",
                       meas_stlpsi_net_MiB_pq="", pred_over_meas_comm="")
        out.append(row)
    return out


def write_csv(rows, out):
    if not rows:
        print("[analytical] no CSTPSI cells found; nothing written.", file=sys.stderr)
        return
    cols = ["D", "db_label", "label", "label_chunks", "threads",
            "cstpsi_T", "stlpsi_rounds", "Qc", "n_passes_cstpsi", "n_passes_stlpsi",
            "cstpsi_gc_ms_pq", "cstpsi_query_ms_pq", "cstpsi_total_ms_pq",
            "pred_stlpsi_ms_pq", "pred_stlpsi_total_ms", "gc_share_of_pred",
            "meas_stlpsi_ms_pq", "pred_over_meas_time",
            "cstpsi_net_MiB_pq", "pred_stlpsi_net_MiB_pq",
            "meas_stlpsi_net_MiB_pq", "pred_over_meas_comm"]
    rows.sort(key=lambda r: (r["D"], r["label"], r["threads"]))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", newline="") as fh:
        ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        fh.write(f"# STLPSI analytical projection | generated {ts} | {len(rows)} CSTPSI cells\n"
                 f"# per-query model (Q-normalized): time pred_pq=(1+K)*gc_pq+(1+K)/(T_c+K)*query_pq; "
                 f"comm pred_pq=(1+K)*r2s_pq+(1+K)/(T_c+K)*s2r_pq  (K=label_chunks, T_c=cstpsi_T)\n"
                 f"# pred_over_meas_{{time,comm}} validate the model vs directly-measured STLPSI (Q-independent)\n")
        w = csv.DictWriter(fh, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow(r)
    # stderr model-quality summary over cells with a measured STLPSI match.
    tr = [float(r["pred_over_meas_time"]) for r in rows if r.get("pred_over_meas_time")]
    cr = [float(r["pred_over_meas_comm"]) for r in rows if r.get("pred_over_meas_comm")]
    print(f"[analytical] wrote {out} ({len(rows)} CSTPSI cells)", file=sys.stderr)
    if tr:
        mape = sum(abs(x - 1.0) for x in tr) / len(tr) * 100
        print(f"[analytical] TIME model vs measured: {len(tr)} cells, mean pred/meas="
              f"{sum(tr)/len(tr):.3f}, mean abs err={mape:.1f}% (range {min(tr):.2f}-{max(tr):.2f})",
              file=sys.stderr)
    if cr:
        mape = sum(abs(x - 1.0) for x in cr) / len(cr) * 100
        print(f"[analytical] COMM model vs measured: {len(cr)} cells, mean pred/meas="
              f"{sum(cr)/len(cr):.3f}, mean abs err={mape:.1f}% (range {min(cr):.2f}-{max(cr):.2f})",
              file=sys.stderr)
    if not tr and not cr:
        print("[analytical] no measured STLPSI cells matched yet "
              "(run a low-query STLPSI validation cell, then re-run).", file=sys.stderr)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--raw-dir", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--recursive", action="store_true")
    args = p.parse_args()
    cells = load_and_average(args.raw_dir, args.recursive)
    write_csv(project(cells), args.out)


if __name__ == "__main__":
    main()
