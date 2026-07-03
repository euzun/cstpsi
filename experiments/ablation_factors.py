#!/usr/bin/env python3
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
"""Compute Table tab:ablation cumulative speedup factors for 64-byte label, 8 threads.

Base = CSTPSI at T_c=2 with NO caching (= STLPSI-style no-caching protocol run at T=2):
  GC re-run every round, query powers re-uploaded every round, per-pair inverse
  recomputed every round.

Components (per query, 8 threads): gc_pq (one GC pass), query_pq (FHE eval+match+
reconstruct, accumulated over T_c+K rounds), r2s_pq (query upload, one round),
s2r_pq (response, accumulated over T_c+K rounds).

64-byte => K=23. T_c=2 => rounds = T_c+K = 25.
Network SIMULATED: 10 Gbps = 1.25e9 bytes/s, 0.02 ms (=0.02e-3 s) one-way latency.
"""
import json, glob, statistics, math, os

# Live results dir, relative to this script -- works for any clone, no hardcoded path.
# Cells are matched recursively by name, so the bmain/blabel/recon subdir layout does
# not matter (reproduce.sh and the older suite both land under results/raw/).
RAW = os.path.join(os.path.dirname(os.path.abspath(__file__)), "results", "raw")
BW = 1.25e9          # bytes/s (10 Gbps)
LAT = 0.02e-3        # s one-way latency per round
T_c = 2
K64 = 23
ROUNDS = T_c + K64   # 25

def med(files, col):
    vals = [json.load(open(f)).get(col) for f in files]
    return statistics.median([v for v in vals if v is not None])

def qcount(files):
    return statistics.median([(json.load(open(f)).get("n_tp") or 0) +
                              (json.load(open(f)).get("n_tn") or 0) for f in files])

def comp(pat):
    fs = sorted(glob.glob(f"{RAW}/**/{pat}", recursive=True))
    if not fs:
        return None
    Q = qcount(fs); K = json.load(open(fs[0]))["label_chunks"]
    return dict(K=K, Q=int(Q),
                gc=med(fs, "gc_online_ms")/Q, qy=med(fs, "query_online_ms")/Q,
                r2s=med(fs, "net_r2s_bytes")/Q, s2r=med(fs, "net_s2r_bytes")/Q,
                tot=med(fs, "total_online_ms")/Q)

P = {"16byte": "cstpsi_D%s_lbl16byte_T2_thr8_p*.json",
     "23bit":  "cstpsi_D%s_lbl23bit_T2_thr8_p*.json",
     "64byte": "cstpsi_D%s_lbl64byte_T2_thr8_p*.json",
     "32byte": "cstpsi_D%s_lbl32byte_T2_thr8_p*.json"}

# ---- gather available cells ----
avail = {}
for D in ["1k", "10k", "100k"]:
    for sz in ["23bit", "16byte", "32byte", "64byte"]:
        c = comp(P[sz] % D)
        if c: avail[(D, sz)] = c
c1m = comp("cstpsi_D1m_lbl23bit_T2_thr8*.json")
avail[("1m", "23bit")] = c1m

# 1M/16-byte (K=6) cell from the LIVE raw dir, if the author has run it. K=6 gives 8
# rounds, so (K=1,K=6) gives a 2-point affine-in-K fit for the 1M column (same method
# as 1K/10K), replacing the single-point slope extrapolation.
_f1m16 = sorted(glob.glob(f"{RAW}/**/cstpsi_D1m_lbl16byte_T2_thr8_p*.json", recursive=True))
if _f1m16:
    _Q16 = qcount(_f1m16); _K16 = json.load(open(_f1m16[0]))["label_chunks"]
    avail[("1m", "16byte")] = dict(K=_K16, Q=int(_Q16),
        gc=med(_f1m16, "gc_online_ms")/_Q16, qy=med(_f1m16, "query_online_ms")/_Q16,
        r2s=med(_f1m16, "net_r2s_bytes")/_Q16, s2r=med(_f1m16, "net_s2r_bytes")/_Q16,
        tot=med(_f1m16, "total_online_ms")/_Q16)

# ---- derive 64-byte (K=23) components per D ----
# gc_pq: one GC pass, D- and K-independent. Use the per-D 23bit cell (most reps).
# r2s_pq: query upload, one round, K- and D-independent (FHE query ct). Use 23bit cell.
# query_pq(K=23): affine fit qy = a + b*K per D (validated at 100K: pred 4149.8 vs meas 4153.4).
# s2r_pq(K=23): (T_c+K)*per_round_response(D); per_round_response is K-independent (validated).

def affine_qy(D, Kt):
    pts = []
    for sz in ["23bit", "16byte", "32byte", "64byte"]:
        if (D, sz) in avail:
            c = avail[(D, sz)]; pts.append((c["K"], c["qy"]))
    n = len(pts); sx = sum(k for k, _ in pts); sy = sum(y for _, y in pts)
    sxx = sum(k*k for k, _ in pts); sxy = sum(k*y for k, y in pts)
    b = (n*sxy - sx*sy) / (n*sxx - sx*sx); a = (sy - b*sx) / n
    return a + b*Kt, a, b, pts

def per_round_resp(D):
    vals = []
    for sz in ["23bit", "16byte", "32byte", "64byte"]:
        if (D, sz) in avail:
            c = avail[(D, sz)]; vals.append(c["s2r"]/(T_c+c["K"]))
    return statistics.mean(vals)

comps = {}
for D in ["1k", "10k", "100k", "1m"]:
    if (D, "64byte") in avail:           # 100K: measured directly
        c = avail[(D, "64byte")]
        comps[D] = dict(gc=c["gc"], qy=c["qy"], r2s=c["r2s"], s2r=c["s2r"],
                        src="measured-64byte")
        continue
    base = avail[(D, "23bit")]
    gc = base["gc"]; r2s = base["r2s"]
    prr = per_round_resp(D)
    s2r = ROUNDS * prr
    if D in ("1k", "10k") or (D == "1m" and ("1m", "16byte") in avail):
        # 2-point affine-in-K fit (1K/10K: K=1+K=6; 1M: K=1+K=6 once the 16-byte cell lands).
        qy, a, b, pts = affine_qy(D, K64)
        src = f"derived: qy affine a={a:.1f} b={b:.2f} pts={pts}; s2r=25*prr({prr:.0f})"
    else:  # 1m: only K=1 available -> bound the query_pq slope (provisional)
        # asymptotic per-chunk slope from larger-D cells: b/n_part at 10k,100k ~0.064,0.052
        # 1M n_part = 31250. Use 100K's b/n_part=0.0516 (most converged) as central est.
        npart_100k = math.ceil(100000/32); b_100k = affine_qy("100k", 0)[2]
        bpn = b_100k / npart_100k
        npart_1m = math.ceil(1000000/32)
        b_1m = bpn * npart_1m
        # intercept a(1m): K=1 cell gives qy = a + b*1 -> a = qy(K1) - b_1m
        a_1m = base["qy"] - b_1m
        qy = a_1m + b_1m * K64
        src = (f"derived(1M, K=1 only): per-round-resp exact; qy via b/n_part={bpn:.5f} "
               f"from 100K -> b_1m={b_1m:.1f}, a_1m={a_1m:.1f}; s2r=25*prr({prr:.0f})")
    comps[D] = dict(gc=gc, qy=qy, r2s=r2s, s2r=s2r, src=src)

# ---- recon-cache: DIRECTLY MEASURED via a cache on/off pair ----
# The "cached reconstruction" step replaces a per-pair field inversion with a cached
# lookup (smallDiffInverse). receiver.cc has CSTPSI_DISABLE_INV_CACHE to force the
# uncached path, so a paired on/off run at the SAME cell measures the saving directly
# (no model): recon_save = (recon_fvp+recon_other)_OFF - _ON. Measured at 100K/23-bit
# (token check dominates the inverse calls and is label-independent, so 23-bit matches
# 64-byte: fvp_ON 18.18 ms == the 64-byte cell's 18.21 ms). The saving is a per-partition
# rate (n_part=ceil(D/32), fully thread-saturated since n_part>>8 for all D>=1K), so it
# projects linearly in D.
def n_part(D): return math.ceil(D / 32)
RECON_ON  = json.load(open(f"{RAW}/_recon_validate/cstpsi_D100k_lbl23bit_T2_thr8_invON_p1.json"))
RECON_OFF = json.load(open(f"{RAW}/_recon_validate/cstpsi_D100k_lbl23bit_T2_thr8_invOFF_p1.json"))
def _recon_pq(d): return (d["recon_fvp_us"] + d["recon_other_us"]) / (d["n_tp"] + d["n_tn"]) / 1000.0
recon_save_100k = _recon_pq(RECON_OFF) - _recon_pq(RECON_ON)      # ms/query at 100K
recon_save_per_part = recon_save_100k / n_part(100000)           # ms/partition (thread-saturated)

D_OF = {"1k": 1000, "10k": 10000, "100k": 100000, "1m": 1000000}

print(f"{'D':5} {'gc_pq':>8} {'query_pq':>9} {'r2s_pq':>12} {'s2r_pq':>15} {'src'}")
for D in ["1k", "10k", "100k", "1m"]:
    c = comps[D]
    print(f"{D:5} {c['gc']:8.1f} {c['qy']:9.1f} {c['r2s']:12.0f} {c['s2r']:15.0f}  {c['src']}")

print()
print(f"{'D':5} | {'t_base':>9} {'+1GC':>9} {'+powcache':>9} {'+recon':>9} | "
      f"{'f1GC':>6} {'f_pow':>7} {'f_recon':>8} | gc_share")
results = {}
for D in ["1k", "10k", "100k", "1m"]:
    c = comps[D]; gc, qy, r2s, s2r = c["gc"], c["qy"], c["r2s"], c["s2r"]
    # transport terms (seconds -> ms)
    def transport(rounds_for_r2s):
        # r2s uploaded `rounds_for_r2s` times; s2r already summed over ROUNDS rounds.
        vol = (rounds_for_r2s * r2s + s2r) / BW          # s
        lat = rounds_for_r2s * LAT                        # s  (one latency per round)
        return (vol + lat) * 1000.0                       # ms

    # BASE: GC every round, powers re-uploaded every round, recon every round.
    comp_base = ROUNDS * gc + qy
    tr_base = transport(ROUNDS)
    t_base = comp_base + tr_base

    # +1GC: GC once. saves (ROUNDS-1)*gc.
    comp_1 = gc + qy
    t_1 = comp_1 + transport(ROUNDS)          # powers still per-round

    # +sender power-window cache: powers uploaded ONCE. r2s sent 1x; saves (ROUNDS-1) latencies too.
    t_2 = comp_1 + transport(1)

    # +receiver reconstruction cache: per-call field inversion -> cached lookup.
    # MEASURED on/off saving, projected linearly by partition count.
    recon_save = recon_save_per_part * n_part(D_OF[D])
    comp_3 = (gc + (qy - recon_save))
    t_3 = comp_3 + transport(1)

    f1 = t_base / t_1
    f2 = t_base / t_2
    f3 = t_base / t_3
    gc_share = ROUNDS*gc / (ROUNDS*gc + qy)
    results[D] = dict(t_base=t_base, t1=t_1, t2=t_2, t3=t_3, f1=f1, f2=f2, f3=f3,
                      recon_save=recon_save, gc_share=gc_share,
                      tr_base=tr_base)
    print(f"{D:5} | {t_base:9.0f} {t_1:9.0f} {t_2:9.0f} {t_3:9.0f} | "
          f"{f1:6.2f} {f2:7.2f} {f3:8.2f} | {gc_share:.3f}")

print()
print("Cumulative factors (rows = +1GC, +sender power cache, +receiver recon cache):")
for step, key in [("+1GC", "f1"), ("+sender power cache", "f2"), ("+receiver recon cache", "f3")]:
    print(f"  {step:24}", "  ".join(f"{results[D][key]:.2f}x" for D in ["1k","10k","100k","1m"]))

print()
print(f"recon-cache on/off saving @100K = {recon_save_100k:.3f} ms/query "
      f"({recon_save_per_part*1000:.3f} us/partition, n_part(100K)={n_part(100000)})")
print("recon_save (ms) per D and as %% of query_pq:")
for D in ["1k","10k","100k","1m"]:
    print(f"  {D:5} recon_save={results[D]['recon_save']:.3f} ms ({100*results[D]['recon_save']/comps[D]['qy']:.2f}% of query_pq), "
          f"transport_base={results[D]['tr_base']:.1f} ms")
