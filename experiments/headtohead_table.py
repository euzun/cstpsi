#!/usr/bin/env python3
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
"""Emit the tab:headtohead time cells from the processed CSVs.

Per-cell rule (matches the table footnote):
  CST per-query online (s) = cstpsi_total_ms_pq / 1000   (= total_online_ms/(n_tp+n_tn))
  STL per-query online (s) = pred_stlpsi_ms_pq  / 1000   (analytical STLPSI projection)
  Spd                      = STL / CST

1M 1/4-thread coverage: 23-bit (K=1) and 16-B (K=6) are MEASURED; 32-B (K=12)
and 64-B (K=23) are EXTRAPOLATED per thread config, because online time is
affine in K (gc constant + query affine: T+K rounds at ~equal per-round cost).
The same STLPSI projection (pred = (1+K)*gc + (1+K)/(T_c+K)*query) is then applied
to the extrapolated components, so CST and STL stay consistent. Validated below
against the 8T grid, where 32/64-B ARE measured: speedup error < 1%.

Comm columns are deterministic (thread-independent) and unchanged across runs;
this script emits only the time columns.
"""
import csv, os

HERE = os.path.dirname(os.path.abspath(__file__))
ANA = os.path.join(HERE, "results/processed/stlpsi_analytical.csv")

DORDER = ["1k", "10k", "100k", "1m"]
DSHOW = {"1k": "1K", "10k": "10K", "100k": "100K", "1m": "1M"}
LORDER = ["23bit", "16byte", "32byte", "64byte"]
LSHOW = {"23bit": "23-bit", "16byte": "16-B", "32byte": "32-B", "64byte": "64-B"}
KOF = {"23bit": 1, "16byte": 6, "32byte": 12, "64byte": 23}
T_C = 2  # CSTPSI token rounds

rows = list(csv.DictReader(r for r in open(ANA) if not r.startswith("#")))

def get(D, lbl, thr):
    for r in rows:
        if r["db_label"] == D and r["label"] == lbl and int(r["threads"]) == thr:
            return r
    return None

def comp(D, lbl, thr):
    """measured (gc_pq, query_pq, total_pq, pred_stl_pq) in ms, or None."""
    r = get(D, lbl, thr)
    if not r:
        return None
    return (float(r["cstpsi_gc_ms_pq"]), float(r["cstpsi_query_ms_pq"]),
            float(r["cstpsi_total_ms_pq"]), float(r["pred_stlpsi_ms_pq"]))

def extrap(D, lbl, thr):
    """Extrapolate (total_pq, pred_stl_pq) for a missing cell from the D's
    23-bit (K=1) and 16-B (K=6) measured cells at this thread: gc = mean of the
    two (label-independent), query affine in K, then the STLPSI projection."""
    a, b = comp(D, "23bit", thr), comp(D, "16byte", thr)
    if not a or not b:
        return None
    K = KOF[lbl]
    gc = (a[0] + b[0]) / 2.0
    # query affine in K through (1, a_query) and (6, b_query)
    slope = (b[1] - a[1]) / (KOF["16byte"] - KOF["23bit"])
    q = a[1] + slope * (K - KOF["23bit"])
    total = gc + q
    pred_stl = (1 + K) * gc + (1 + K) / (T_C + K) * q
    return (total, pred_stl)

def cell(D, lbl, thr):
    """(STL_s, CST_s, spd, is_extrap)."""
    m = comp(D, lbl, thr)
    if m:
        return (m[3] / 1000, m[2] / 1000, m[3] / m[2], False)
    e = extrap(D, lbl, thr)
    if e:
        return (e[1] / 1000, e[0] / 1000, e[1] / e[0], True)
    return None

# ---- validation: extrapolate 32/64-B at 8T and compare to measured ----
print("=== extrapolation check at 1M 8T (32/64-B measured vs predicted) ===")
for lbl in ("32byte", "64byte"):
    m = comp("1m", lbl, 8)
    e = extrap("1m", lbl, 8)
    mt, ms_pred = m[2], m[3]; et, es = e
    print(f"  {LSHOW[lbl]:5s}: CST meas {mt/1000:6.2f}s pred {et/1000:6.2f}s "
          f"({100*(et-mt)/mt:+.1f}%) | STL meas {ms_pred/1000:6.2f}s pred {es/1000:6.2f}s "
          f"({100*(es-ms_pred)/ms_pred:+.1f}%) | spd meas {ms_pred/mt:.2f} pred {es/et:.2f}")

print("\n=== tab:headtohead time cells (E = extrapolated) ===")
print(f"{'D':5s}{'Label':8s}| {'1T STL':>7s}{'CST':>6s}{'Spd':>5s}  | "
      f"{'4T STL':>7s}{'CST':>6s}{'Spd':>5s}  | {'8T STL':>7s}{'CST':>6s}{'Spd':>5s}")
for D in DORDER:
    for lbl in LORDER:
        parts = []
        for thr in (1, 4, 8):
            c = cell(D, lbl, thr)
            stl, cst, spd, ex = c
            tag = "E" if ex else " "
            parts.append(f"{stl:7.1f}{cst:6.1f}{spd:4.1f}x{tag}")
        print(f"{DSHOW[D]:5s}{LSHOW[lbl]:8s}| " + " | ".join(parts))
