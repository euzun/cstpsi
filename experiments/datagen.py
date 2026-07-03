#!/usr/bin/env python3
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
"""
datagen.py -- CSTPSI benchmark dataset generator (locked-plan edition).

Rewritten from scratch for the ACSAC'26 M1-M6 experiment plan.  Produces, for
each (db_size, label_size) cell:

    enr_<size>_lbl<label>.csv   enrollment: id, item_0..item_{N-1}, lbl_0..lbl_{k-1}
    qry_<size>.csv              queries:    id, item_0..item_{N-1}, is_tp
    expected_<size>_lbl<label>.csv  TP ground-truth labels: query_id, lbl_0..lbl_{k-1}

The query file mixes --tp true-positive and --tn true-negative rows; `is_tp`
flags each.  verify.py consumes `is_tp` to split FRR (TP) from FAR (TN).

Key differences from the original gen_benchmark_datasets.py:
  * configurable --tn / --tp counts (plan: 1000 TN, 200 TP) instead of 10/10
  * configurable --sizes incl. 5k/50k/500k/1m (plan M1/M5 D-grid)
  * O(N * occ) TN non-overlap check via an inverted index, so D=1M is feasible
    (the original O(TN * D * N) scan is ~10^11 ops at D=1M).
  * separate enrollment seed (default 42, matches the C++ synthetic bench) and
    query seed (default 1234, "fresh seed != enrollment" per the FAR methodology)

Usage:
  python3 experiments/datagen.py --sizes 1k --labels 23bit --tn 1000 --tp 200
  python3 experiments/datagen.py --sizes 1k,5k,10k,50k,100k --labels 23bit
  python3 experiments/datagen.py --sizes 500k,1m --labels 23bit   # heavy; ~RAM note below
"""

import argparse
import csv
import os
import random
import sys
from collections import defaultdict

# --- Protocol constants (must match the C++ defaults) -----------------------
FIELD_MODULUS = 8519681   # plain_modulus, ~23-bit field
N = 64                    # set size per row
K_THRESHOLD = 2           # threshold k: a "match" needs >= k shared items

# label_size name -> number of field-element chunks per label
# (~22.3 usable bits/chunk: 64 bytes = 512 bits / 23 chunks; 16B->6, 32B->12).
# The chunk count is what matters for correctness -- datagen and the binary's
# --nrofLabelChunks must agree (run.sh label_chunks() mirrors this map).
LABEL_CONFIGS = {
    "23bit":   1,
    "16byte":  6,
    "32byte":  12,
    "64byte":  23,
    "512byte": 179,
}

SIZE_LABELS = {
    1000: "1k", 5000: "5k", 10000: "10k", 50000: "50k",
    100000: "100k", 500000: "500k", 1000000: "1m",
}
LABEL_TO_SIZE = {v: k for k, v in SIZE_LABELS.items()}

DEFAULT_ENR_SEED = 42
DEFAULT_QRY_SEED = 1234


# --- low-level helpers ------------------------------------------------------

def rand_item(rng):
    """Random integer in [1, FIELD_MODULUS)."""
    return rng.randint(1, FIELD_MODULUS - 1)


def rand_unique_row(rng, n=N):
    """List of n distinct random field elements."""
    items = set()
    while len(items) < n:
        items.add(rand_item(rng))
    return list(items)


def rand_label_tuple(rng, num_chunks):
    return tuple(rand_item(rng) for _ in range(num_chunks))


# --- enrollment -------------------------------------------------------------

def generate_enrollment_rows(rng, db_size):
    return [rand_unique_row(rng) for _ in range(db_size)]


def generate_labels(rng, db_size, num_chunks):
    used = set()
    labels = []
    for _ in range(db_size):
        while True:
            lbl = rand_label_tuple(rng, num_chunks)
            if lbl not in used:
                used.add(lbl)
                labels.append(lbl)
                break
    return labels


def write_enrollment_csv(path, item_rows, labels, num_chunks):
    header = ["id"] + [f"item_{i}" for i in range(N)] + [f"lbl_{j}" for j in range(num_chunks)]
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        for rid, (items, lbl) in enumerate(zip(item_rows, labels)):
            w.writerow([rid] + items + list(lbl))


# --- inverted index for fast TN non-overlap testing -------------------------

def build_inverted_index(item_rows):
    """value -> list of row-ids containing it.

    Lets a candidate TN row be tested in O(N * occ) instead of O(D * N):
    we tally, across the candidate's N items, how many times each enrolled
    row is hit; a hit-count >= K means >= K shared items with that row.
    """
    idx = defaultdict(list)
    for rid, items in enumerate(item_rows):
        for v in items:
            idx[v].append(rid)
    return idx


def shares_k_with_any(candidate, idx, k=K_THRESHOLD):
    """True iff `candidate` shares >= k items with at least one enrolled row."""
    hit = defaultdict(int)
    for v in candidate:
        for rid in idx.get(v, ()):  # rows containing this exact value
            hit[rid] += 1
            if hit[rid] >= k:
                return True
    return False


# --- query generation -------------------------------------------------------

def generate_tp_query(rng, enrolled_items):
    """One TP query: keep m in-place items from an enrolled row (k <= m <= N/2),
    replace the rest with fresh values absent from the row.  Positional
    alignment is required -- the FHE eval matches values at the same index."""
    m = rng.randint(K_THRESHOLD, N // 2)
    keep = set(rng.sample(range(N), m))
    q = list(enrolled_items)
    used = set(enrolled_items)
    for pos in range(N):
        if pos not in keep:
            while True:
                v = rand_item(rng)
                if v not in used:
                    q[pos] = v
                    used.add(v)
                    break
    return q


def generate_tn_query(rng, idx):
    """One TN query sharing < k items with every enrolled row."""
    while True:
        cand = rand_unique_row(rng)
        if not shares_k_with_any(cand, idx):
            return cand


def generate_queries(rng, item_rows, idx, num_tp, num_tn):
    """Returns (query_rows, tp_source_ids).
    query_rows: list of (query_id, items, is_tp); ids 0..tp-1 are TP, then TN.
    tp_source_ids[i] is the enrolled row index TP query i was derived from."""
    db_size = len(item_rows)
    query_rows = []
    tp_source_ids = []
    for qid in range(num_tp):
        src = rng.randint(0, db_size - 1)
        query_rows.append((qid, generate_tp_query(rng, item_rows[src]), 1))
        tp_source_ids.append(src)
    for qid in range(num_tp, num_tp + num_tn):
        query_rows.append((qid, generate_tn_query(rng, idx), 0))
    return query_rows, tp_source_ids


def write_query_csv(path, query_rows):
    header = ["id"] + [f"item_{i}" for i in range(N)] + ["is_tp"]
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        for qid, items, is_tp in query_rows:
            w.writerow([qid] + items + [is_tp])


def write_expected_csv(path, tp_source_ids, labels, num_chunks):
    header = ["query_id"] + [f"lbl_{j}" for j in range(num_chunks)]
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        for qid, src in enumerate(tp_source_ids):
            w.writerow([qid] + list(labels[src]))


# --- driver -----------------------------------------------------------------

def data_rows(path):
    """Number of data rows in a CSV (excluding header), or -1 if absent.
    Used to detect stale files whose TP/TN counts no longer match the request,
    so a pre-existing dataset can't be silently reused at the wrong size."""
    if not os.path.exists(path):
        return -1
    with open(path) as f:
        return sum(1 for _ in f) - 1


def enr_first_row_items(path, n=N):
    """First enrolled row's N item values (as strings), or None if absent/empty.

    Cheap provenance probe: the enrollment items are a deterministic function of
    --enr-seed, so the first row alone identifies the generator stream. Reusing
    an on-disk enr whose row 0 differs from the freshly-built item_rows would
    desync it from the regenerated qry/expected (the TP queries index in-memory
    rows the C++ binary never sees -> every TP misses, network FRR -> 1.0)."""
    if not os.path.exists(path):
        return None
    with open(path) as f:
        f.readline()                       # header
        line = f.readline()
        if not line:
            return None
        return line.rstrip("\n").split(",")[1:1 + n]   # skip id; item_0..item_{n-1}


def enr_reproduces(path, item_rows):
    """True iff the on-disk enr's row 0 matches the in-memory item_rows[0]."""
    return enr_first_row_items(path) == [str(v) for v in item_rows[0]]


def parse_sizes(s):
    """Accept preset labels (1k..1m) or a bare integer (e.g. '10' for the M6
    tiny ground-truth instance, labelled 'd10')."""
    out = []
    for tok in s.split(","):
        tok = tok.strip()
        if tok in LABEL_TO_SIZE:
            out.append(LABEL_TO_SIZE[tok])
        elif tok.isdigit():
            n = int(tok)
            SIZE_LABELS.setdefault(n, f"d{n}")
            out.append(n)
        else:
            sys.exit(f"Error: unknown size '{tok}'. Valid: {', '.join(SIZE_LABELS.values())} or a bare integer")
    return out


def parse_labels(s):
    out = []
    for tok in s.split(","):
        tok = tok.strip()
        if tok not in LABEL_CONFIGS:
            sys.exit(f"Error: unknown label '{tok}'. Valid: {', '.join(LABEL_CONFIGS)}")
        out.append(tok)
    return out


def main():
    p = argparse.ArgumentParser(description="CSTPSI benchmark dataset generator")
    here = os.path.dirname(os.path.abspath(__file__))
    p.add_argument("--output-dir", default=os.path.join(here, "benchmark_datasets"))
    p.add_argument("--sizes", default="1k,5k,10k,50k,100k,500k,1m")
    p.add_argument("--labels", default="23bit",
                   help="comma list of label sizes (default 23bit; FAR is label-independent)")
    p.add_argument("--tn", type=int, default=100, help="true-negative queries per cell (FAR)")
    p.add_argument("--tp", type=int, default=100, help="true-positive queries per cell (FRR)")
    p.add_argument("--enr-seed", type=int, default=DEFAULT_ENR_SEED)
    p.add_argument("--qry-seed", type=int, default=DEFAULT_QRY_SEED)
    p.add_argument("--force", action="store_true",
                   help="overwrite existing files (default: skip if present)")
    args = p.parse_args()

    sizes = parse_sizes(args.sizes)
    labels = parse_labels(args.labels)
    os.makedirs(args.output_dir, exist_ok=True)

    print(f"enr-seed={args.enr_seed}  qry-seed={args.qry_seed}  "
          f"tn={args.tn}  tp={args.tp}  labels={labels}")

    n_written = n_skipped = 0

    for db_size in sizes:
        sl = SIZE_LABELS[db_size]
        print(f"\n=== D={sl} ({db_size:,} rows) ===")

        qry_path = os.path.join(args.output_dir, f"qry_{sl}.csv")
        enr_paths = {l: os.path.join(args.output_dir, f"enr_{sl}_lbl{l}.csv") for l in labels}
        exp_paths = {l: os.path.join(args.output_dir, f"expected_{sl}_lbl{l}.csv") for l in labels}

        # Whole-size fast skip: if every output for this size already exists,
        # has the requested TP/TN counts, and we're not forcing, do NONE of the
        # expensive in-memory generation (enrollment rows + inverted index + TN
        # search) -- just report it. The count check guards against silently
        # reusing a stale dataset built with a different --tn/--tp (the qry file
        # is shared across labels, so a leftover small qry would otherwise be
        # paired with freshly-regenerated, larger expected files -> desync).
        all_present = (os.path.exists(qry_path)
                       and all(os.path.exists(p) for p in enr_paths.values())
                       and all(os.path.exists(p) for p in exp_paths.values()))
        counts_ok = (data_rows(qry_path) == args.tp + args.tn
                     and all(data_rows(p) == args.tp for p in exp_paths.values()))
        # Provenance check: a present enr whose row 0 doesn't match the
        # --enr-seed stream is stale (built by a different seed/generator).
        # Reusing it would desync the C++ enrollment from the regenerated
        # qry/expected (every TP indexes an in-memory row absent on disk ->
        # network FRR -> 1.0). Probe with a cheap one-row regeneration.
        enr_stale = False
        if all_present:
            row0 = [str(v) for v in rand_unique_row(random.Random(args.enr_seed))]
            enr_stale = any(enr_first_row_items(p) != row0 for p in enr_paths.values())
        if all_present and counts_ok and not enr_stale and not args.force:
            print(f"  SKIP D={sl}: all files present with tp={args.tp} tn={args.tn} "
                  f"(--force to regenerate). No data generated.")
            n_skipped += 1 + 2 * len(labels)
            continue
        if all_present and not args.force and (not counts_ok or enr_stale):
            reason = (f"TP/TN counts differ from requested tp={args.tp} tn={args.tn}"
                      if not counts_ok else
                      f"on-disk enr does not match --enr-seed {args.enr_seed} stream "
                      f"(stale enrollment)")
            print(f"  REGEN D={sl}: {reason} -- regenerating to keep "
                  f"enr/qry/expected mutually consistent.")

        if db_size >= 500000:
            print(f"  [note] D={sl}: inverted-index TN check uses ~{db_size*N*8//10**9 + 1} GB RAM; "
                  f"enrollment CSV is large.")

        # Enrollment items + queries share one RNG stream seeded distinctly.
        enr_rng = random.Random(args.enr_seed)
        qry_rng = random.Random(args.qry_seed)

        print(f"  generating {db_size:,} enrollment rows ...")
        item_rows = generate_enrollment_rows(enr_rng, db_size)

        print(f"  indexing + generating {args.tp} TP + {args.tn} TN queries ...")
        idx = build_inverted_index(item_rows)
        query_rows, tp_source_ids = generate_queries(qry_rng, item_rows, idx, args.tp, args.tn)

        # qry + expected are a coupled unit -- expected indexes the exact TP
        # queries just generated -- so whenever we regenerate for this size we
        # rewrite qry and every expected together. They are small and cheap.
        # enr is independent (its rows depend only on the enrollment + label
        # streams, not on --tn/--tp) so it can be reused when already present at
        # the right row count; this avoids rewriting the large enrollment CSV.
        write_query_csv(qry_path, query_rows)
        print(f"    WROTE {qry_path} ({len(query_rows)} rows)")
        n_written += 1

        for lbl_name in labels:
            num_chunks = LABEL_CONFIGS[lbl_name]
            enr_path, exp_path = enr_paths[lbl_name], exp_paths[lbl_name]
            lbl_rng = random.Random(args.enr_seed + 7 + num_chunks)  # label stream
            labels_list = generate_labels(lbl_rng, db_size, num_chunks)

            # Reuse the on-disk enr only when it is present at the right row
            # count AND its items reproduce the in-memory item_rows. The row-0
            # provenance probe catches a stale enr from a different seed or an
            # older generator, which would otherwise silently desync from the
            # freshly-written qry/expected.
            present = data_rows(enr_path) == db_size
            reproduces = present and enr_reproduces(enr_path, item_rows)
            if not args.force and present and reproduces:
                print(f"    skip  {enr_path} (present, {db_size:,} rows, items verified)")
                n_skipped += 1
            else:
                if not args.force and present and not reproduces:
                    print(f"    WARN  {enr_path}: items differ from --enr-seed "
                          f"{args.enr_seed} stream -- rewriting (was stale).")
                write_enrollment_csv(enr_path, item_rows, labels_list, num_chunks)
                print(f"    WROTE {enr_path} ({db_size:,} rows, {num_chunks} chunk(s))")
                n_written += 1

            # Always rewrite expected to stay consistent with the qry just written.
            write_expected_csv(exp_path, tp_source_ids, labels_list, num_chunks)
            print(f"    WROTE {exp_path} ({len(tp_source_ids)} TP rows)")
            n_written += 1

    print(f"\nDone. {n_written} file(s) written, {n_skipped} skipped (already present; "
          f"--force to regenerate).")


if __name__ == "__main__":
    main()
