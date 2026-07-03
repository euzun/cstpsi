# Parameter Configurations

JSON files passed to `cstpsi_cli` (and the `_sender`/`_receiver` binaries) via `--params`. One file per use case; reuse via `--db`/`--queries` overrides if you want to swap inputs without re-editing the config.

## Files

- `demo_1k.json` -- the 1K-row demo configuration (D=1000, N=64, 23-bit label, CSV integer-set input). Used by every `demo/` entry point: the in-process demo (`demo/demo_small.sh`), the automated network demo (`demo/demo_network.sh`) and the Docker demo, and the manual two-terminal network scripts (`demo/run_sender.sh`, `demo/run_receiver.sh`). All of them generate their CSVs on the fly via `experiments/datagen.py`. It is also the fixture for the `test_param_loader` unit test.

Larger benchmark configurations live in `experiments/configs/` (`bench_1k.json`, `bench_10k.json`, `bench_100k.json`, `smoke_1k_N64_k2.json`).

## Schema

```json
{
  "name": "string",
  "description": "string",
  "protocol_params": {
    "N": 64                   // items per row, 1..256
  },
  "database_params": {
    "nrof_que_ids": 10        // number of queries to run from the input
  },
  "performance_params": {
    "m": 4096,                // SEAL SIMD batch (power of 2; matches poly_modulus_degree)
    "partition_size": 32,     // rows per partition
    "nrof_splits": 1,
    "nrof_collisions": 1,
    "nrof_batch_que": 1,
    "nrof_online_threads": 4
  },
  "seal_params": {
    "poly_modulus_degree": 4096,
    "plain_modulus": 8519681
  },
  "dataset": {
    "format": "csv",
    "enrollment_path": "demo/data/enr_1k_lbl23bit.csv",
    "query_path":      "demo/data/qry_1k.csv",
    "output_path":     "demo/results/demo_output.csv",
    "enr_bits": 10             // log2(enrollment_count) ceiling
  }
}
```

## Hardcoded (not read from JSON)

- `k = 2` -- matching threshold (k-of-N). Hardcoded in `src/io/param_loader.cc`. The paper and current code do not support other values.

## Tuning notes

- Larger `m` and `nrof_online_threads` -- faster, more memory.
- Larger `N` -- stronger threshold semantics, more crypto work per row.
- `partition_size` -- smaller partitions reduce per-batch work but inflate setup time. Defaults are sensible.

## Authoring a new config

Copy `demo_1k.json`, edit the fields above, point `dataset.enrollment_path` / `query_path` at your CSVs, and pass it as `--params`. There is no schema validator yet; `cstpsi_cli` errors on missing or out-of-range fields at load time.
