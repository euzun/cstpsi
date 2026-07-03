# expected

Paper-reported reference values for the head-to-head table claim.

- `expected_table.md` -- per-query online time (s) and communication (MiB) for
  STLPSI vs CSTPSI across D in {1K,10K,100K,1M}, the four label sizes, and 1/4/8
  threads, with speedups and comm. savings. `run.sh` measures every cell live;
  compare its output against these numbers (same trends and ballpark, not exact).
