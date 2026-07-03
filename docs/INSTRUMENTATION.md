# Instrumentation & Raw-Data Plan

This document describes how the CSTPSI reference implementation records
its performance numbers, so the paper's evaluation tables and figures can
be reproduced (or re-analyzed under different lenses) from raw data
without re-running experiments.

## Why fine-grained

Every benchmark run records **per-step time, per-step memory, and
per-direction bytes-on-wire** in a structured JSONL log. All paper
tables and figures in §VIII are derived post-hoc from these logs by
analysis scripts under `scripts/analysis/`.  This means:

- Any reviewer question of the form "what's the time/memory/bandwidth
  of step *X* in configuration *Y*?" can be answered from the logs
  without re-running.
- The same raw run feeds the FAR table, the head-to-head comparison,
  the thread-scaling chart, the per-component breakdown, and the
  per-query amortization curve — no duplicated experiments.
- Future analyses (additional reviewer asks, follow-up papers) can
  re-derive from the same logs.

## What is measured

For each of the protocol's 10 steps (numbered as in
Figure~\ref{fig:cstpsi-protocol} of the paper):

| Step | Measured |
|------|----------|
| 1. Init                          | wall time, RSS delta |
| 2. Sender Offline: partition     | wall time, RSS delta |
| 3. Sender Offline: blind         | wall time, RSS delta |
| 4. Sender Offline: share         | wall time **split into** coefficient sampling vs. share evaluation; RSS delta |
| 5. Sender Offline: interp & pack | wall time **split into** Lagrange interpolation vs. SEAL SIMD packing; RSS sampled after each |
| 6. 1-GC garbled-circuit AES      | wall time, bytes R↔S, peak RSS each side |
| 7. Online query powers           | R-side encrypt time, R-side serialize time, S-side receive+cache time, cache size in bytes |
| 8. Per-Round Homomorphic Eval    | S-side wall time **split into** coefficient load, ciphertext inner product, noise flooding, modulus switching, serialization; R-side decrypt time; bytes S→R per round; RSS sampled after rerandomization; round kind (token / label) |
| 9. Token check                   | wall time, pairs tried, tokens found |
| 10. Label recovery               | wall time, labels recovered |

Sub-step totals **must sum to the parent step's wall time** (asserted
in the smoke test).  Boundaries are mutually exclusive and collectively
exhaustive.

## Output format

One JSONL file per `(configuration × D × threads × T)` session.  Schema
sketch (truncated for brevity):

```jsonc
{
  "run_id": "uuid",
  "config":   { "D": 100000, "N": 64, "k": 2, "T": 2, "K": 1,
                "label_bytes": 64, "threads": 8, "config_name": "CSTPSI-best" },
  "seeds":    { "db": 42, "query": 1234 },
  "hardware": { "cpu": "...", "ram_gb": 64, "os": "...", "git_sha": "..." },
  "software": { "seal": "4.1.2", "emp": "...", "gcc": "..." },

  "offline_once_per_session": {
    "step_init":        { "wall_us": ..., "rss_delta_mb": ... },
    "step_partition":   { "wall_us": ..., "rss_delta_mb": ... },
    "step_blind":       { "wall_us": ..., "rss_delta_mb": ... },
    "step_share":       { "sample_coef_us": ..., "eval_shares_us": ...,
                          "wall_us": ..., "rss_delta_mb": ... },
    "step_interp_pack": { "interpolate_us": ..., "simd_pack_us": ...,
                          "wall_us": ..., "rss_after_interp_mb": ...,
                          "rss_after_pack_mb": ... },
    "total_offline_us": ...,
    "s_peak_offline_rss_mb": ...
  },

  "queries": [
    {
      "query_id": 0, "query_type": "TN",
      "step_gc":           { "gc_us": ..., "bytes_r_to_s": ...,
                             "bytes_s_to_r": ..., "r_rss_mb": ...,
                             "s_rss_mb": ... },
      "step_query_powers": { "r_encrypt_us": ..., "r_serialize_us": ...,
                             "s_recv_us": ..., "cache_size_bytes": ...,
                             "bytes_r_to_s": ..., "r_rss_mb": ...,
                             "s_rss_mb": ... },
      "step_hom_rounds": [
        { "t": 0, "round_kind": "token",
          "s": { "coef_load_us": ..., "ctxt_inner_product_us": ...,
                 "noise_flood_us": ..., "mod_switch_us": ...,
                 "serialize_us": ..., "wall_us": ...,
                 "rss_after_rerand_mb": ... },
          "r": { "r_decrypt_us": ... },
          "bytes_s_to_r": ... },
        ...
      ],
      "step_token_check":    { "r_us": ..., "pairs_tried": ...,
                               "tokens_found": ... },
      "step_label_recovery": { "r_us": ..., "labels_recovered": ... },
      "totals":              { "online_wall_us": ...,
                               "r_peak_rss_mb": ..., "s_peak_rss_mb": ...,
                               "bytes_r_to_s_total": ...,
                               "bytes_s_to_r_total": ... },
      "result":              { "matched": true, "labels": [4] }
    }
  ],
  "session_totals": { "session_wall_us": ...,
                      "s_peak_rss_overall_mb": ... }
}
```

## How to re-derive paper numbers

The `scripts/analysis/` directory provides one script per paper table or
figure:

- `make_far_table.py`     — Table 5 (per-D FAR vs. theory)
- `make_far_figure.py`    — Figure 3 (log-log FAR with Wilson CIs)
- `make_h2h_table.py`     — Table 8 (CSTPSI-orig vs. CSTPSI-best)
- `make_breakdown.py`     — per-component time stacked bar
- `make_thread_scaling.py`— Figure 4 (speedup curve)
- `make_amortization.py`  — per-query amortization slope

Each loads the relevant JSONL files, applies the analysis, and emits a
LaTeX-ready table or a PDF figure.  Re-deriving a single table:

```bash
python3 scripts/analysis/make_far_table.py \
    --logs data/m1_far_sweep/*.jsonl \
    --out  out/tab_far.tex
```

## Note on the BSGS / Option A divergence

The paper (§IV + Appendix A) presents the Baby-Step Giant-Step (BSGS)
sparse-window variant of step 7 as canonical.  The reference artifact
implements **Option A** (full-receiver-side power encryption with
sender-side cache), per the documented trade-off in the §VI.B footnote.
The JSONL `step_query_powers` block reflects Option A.  All §VIII
numbers are measured on Option A; the BSGS analytical estimate stays in
the paper as the published variant.

## Reproducibility checklist

- All seeds (DB generator, query generator) are recorded in
  `seeds.db` / `seeds.query`.
- Hardware and software fingerprints (`hardware.cpu`, `software.seal`,
  `software.git_sha`) are recorded per session.
- Sum-invariant assertions run in the smoke-test harness; any
  divergence fails the build.
- Instrumentation overhead is bounded under 1% of wall-clock
  (validated by running the same configuration with and without
  instrumentation enabled).
