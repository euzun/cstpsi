# CSTPSI Parameter Reference

CSTPSI parameters live in two places:

1. **JSON config files** (`experiments/configs/*.json`) — passed via
   `--params <file>` to every binary. Holds protocol, performance, SEAL,
   dataset, and metadata fields.
2. **CLI flags** — runtime overrides on the binaries (`cstpsi_cli`,
   `cstpsi_sender`, `cstpsi_receiver`).

Threshold `k` is hardcoded to `2` and not a config field; see the
[footnote in §VI.A of the paper](../paper/sections/05_building_blocks.tex)
for the design rationale.

## JSON config schema

```jsonc
{
  "name":        "smoke_1k_N64_k2",
  "description": "Lightweight smoke test (1K enrollments, N=64)",

  "protocol_params": {
    "N": 64
  },

  "database_params": {
    "nrof_que_ids": 2
  },

  "performance_params": {
    "m":               4096,
    "partition_size":  32,
    "nrof_splits":     1,
    "nrof_collisions": 1,
    "nrof_online_threads":    4
  },

  "seal_params": {
    "poly_modulus_degree": 4096,
    "plain_modulus":       8519681
  },

  "dataset": {
    "format":          "csv",
    "enrollment_path": "experiments/results/smoke_test/enroll.csv",
    "query_path":      "experiments/results/smoke_test/query.csv",
    "output_path":     "experiments/results/smoke_test/results.csv",
    "enr_bits":        10,
    "enr_total":       1000
  }
}
```

### `protocol_params`

| Field | Type | Required | Typical | Description |
|-------|------|----------|---------|-------------|
| `N` | int | yes | 32, 64, 96, 128 | Item-positions per row. The receiver provides an `N`-tuple `Y = (y_1, ..., y_N)`. |

### `database_params`

| Field | Type | Required | Typical | Description |
|-------|------|----------|---------|-------------|
| `nrof_que_ids` | int | yes | 1–10000 | Number of queries the receiver issues per session. |

### `performance_params`

| Field | Type | Required | Typical | Description |
|-------|------|----------|---------|-------------|
| `m` | int | yes | 1024, 2048, 4096 | BFV SIMD slot count. Must equal `poly_modulus_degree`. |
| `partition_size` | int | yes | 32, 64, 128 | `s_part` in the paper. Max rows per partition; sets the interpolating polynomial's degree to `s_part - 1`. |
| `nrof_splits` | int | yes | 1, 2, 4 | DB splits for parallel preprocessing. |
| `nrof_collisions` | int | yes | 1, 16, 32 | Max within-partition column-value duplicates before forcing a re-pack. `1` for strict (no dupes). |
| `nrof_online_threads` | int | yes | 1–`nproc` | OpenMP thread count for parallel inner loops. |

### `seal_params`

| Field | Type | Required | Typical | Description |
|-------|------|----------|---------|-------------|
| `poly_modulus_degree` | int | yes | 2048, 4096, 8192 | BFV ring dimension. 4096 = ~128-bit security with `BFVDefault` coeff modulus. |
| `plain_modulus` | int | yes | 8519681 | Field modulus `F` for plaintext arithmetic; sets `FIELD_MODULUS` globally. Must be prime and satisfy `F ≡ 1 (mod 2 · m)`. |

### `dataset`

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `format` | string | yes | `"csv"` or `"binary"` |
| `enrollment_path` | string | csv-mode | CSV with rows `id,item0,item1,...,itemN-1[,label0,label1,...]` |
| `query_path` | string | csv-mode | CSV with rows `id,item0,...,itemN-1` |
| `output_path` | string | csv-mode | Where matched IDs get written |
| `path` | string | binary-mode | Path to packed binary database file |
| `enr_bits` | int | yes | Log₂ of the protocol's enrolled-DB size: `nrof_enr_ids = 1 << enr_bits` |
| `enr_total` | int | optional | Actual enrollment count in the file (defaults to `2^enr_bits`) |

## CLI: `cstpsi_cli`

Single-process binary covering the synthetic-bench, CSV-driven runs, and the
loopback path used by the instrumentation suite.

```bash
cstpsi_cli <mode> --params <json> [options]
```

| Mode | Description |
|------|-------------|
| `bench` | Synthetic benchmark: generates random enrollment + true-positive queries from `nrof_enr_ids` (= `1 << enr_bits`); verifies FRR=0%. Useful for smoke / FAR sweeps. |
| `csv_run` | Loads enrollment + query CSVs from `dataset.{enrollment,query,output}_path`; runs the full PSI; writes matched IDs to `output_path`. |
| `help` | Prints usage. |

Options (in addition to `--params`):

| Flag | Default | Description |
|------|---------|-------------|
| `--db <file>` | from config | Override `dataset.enrollment_path` |
| `--queries <file>` | from config | Override `dataset.query_path` |
| `--output <file>` | from config | Override `dataset.output_path` |
| `--output-jsonl <file>` | (off) | Emit per-step fine-grained measurements to this JSONL file. Loopback inner mode runs the queries sequentially so per-thread timer collisions are avoided; inner OpenMP regions inside each query still scale. See [INSTRUMENTATION.md](INSTRUMENTATION.md). |
| `--verbose` | (off) | Verbose console output |

Example:

```bash
DYLD_LIBRARY_PATH=build/emp_install/lib \
  ./build/cstpsi_cli bench \
  --params experiments/configs/smoke_1k_N64_k2.json \
  --output-jsonl /tmp/smoke.jsonl
```

## CLI: `cstpsi_sender` (network server)

```bash
cstpsi_sender --dbFile <csv> --paramsFile <json> [options]
```

| Flag | Default | Description |
|------|---------|-------------|
| `--dbFile <path>` | (required) | Enrollment CSV |
| `--paramsFile <path>` | (required) | JSON parameter config |
| `--port <int>` | 1212 | TCP port to listen on |
| `--bind <addr>` | 0.0.0.0 | Interface to bind to |
| `--labelCol <int>` | -1 | Column index of the label in the CSV (-1 = use the row id as the label) |
| `--nrofLabelChunks K` | 0 | Multi-round (1-GC) mode: `K` separate label rounds packed into one session. Required for label-bytes larger than `log₂(F)`. |
| `--nrofTokenRounds T` | 1 | Multi-token FAR amplification: `T` independent token rounds. `T=1` reproduces the FLPSI'21 baseline; `T=2` drives the per-trial FAR bound from `1/F` to `1/F²`, and so on. |
| `--disable-1gc` | (off) | FLPSI'21-style baseline: run `K + 1` separate GC executions instead of the bundled 1-GC. Requires `--nrofLabelChunks`. |
| `--threads <int>` | from config | Override `performance_params.nrof_online_threads` |
| `--verbose` | (off) | Verbose output |

## CLI: `cstpsi_receiver` (network client)

```bash
cstpsi_receiver --queryFile <csv> --paramsFile <json> --senderAddr <host> [options]
```

| Flag | Default | Description |
|------|---------|-------------|
| `--queryFile <path>` | (required) | Query CSV |
| `--paramsFile <path>` | (required) | JSON parameter config |
| `--senderAddr <host>` | (required) | Sender hostname or IP |
| `--outputFile <path>` | (req in single-round) | Matched IDs CSV (single-round mode) |
| `--outputDir <dir>` | (req in multi-round) | Output directory; per-round results land at `round_N.csv` |
| `--nrofRounds N` | 0 | Multi-round (1-GC) mode: `N` total rounds (`T` token rounds + `K` label-chunk rounds, so `N = T + K`) |
| `--nrofTokenRounds T` | 1 | Multi-token FAR amplification (must match sender). |
| `--disable-1gc` | (off) | FLPSI'21-style baseline path; requires `--nrofRounds` |
| `--port <int>` | 1212 | Sender TCP port |
| `--timeout <ms>` | 30000 | Operation timeout |
| `--threads <int>` | from config | Override thread count |
| `--verbose` | (off) | Verbose output |

## Parameter relationships

### Database size

`nrof_enr_ids = 1 << enr_bits` (set in `dataset.enr_bits`). `enr_total` can
be smaller than `nrof_enr_ids` to model under-populated datasets.

### Partition count

`nrof_part = ceil(nrof_enr_ids / partition_size)`. Smaller `partition_size`
→ more partitions, each evaluating a lower-degree polynomial; trade-off vs.
SIMD packing density.

### SIMD packing

Per ciphertext: `floor(m / N)` partitions packed. `m` = `poly_modulus_degree`
in practice; with `N=64, m=4096` you get `64×` amortization per BFV ciphertext.

### FAR-composition bound

Per Lemma 1 / Theorem 2 of the paper:

```
Pr[breach] ≤ binom(N, k) · nrof_part / F^T  +  T · Adv_PRF_AES
```

With the default `(N=64, k=2, F=8519681, partition_size=32)`,
`binom(N,k) = 2016` and `1/F ≈ 1.17×10⁻⁷`. For `D = 10⁶` →
`nrof_part = 31250`, so:

| `T` | Bound |
|-----|-------|
| 1   | ~0.52 (saturating; this is the FLPSI'21 oversight) |
| 2   | ~10⁻⁸ (engineering-negligible) |
| 3   | ~10⁻¹⁵ (cryptographically negligible at billion scale) |

Hence the default deployment recommendation `T = 2` and the
`--nrofTokenRounds 2` flag.

## Example configs in `experiments/configs/`

| File | Purpose |
|------|---------|
| `smoke_1k_N64_k2.json` | 1K enrollments, fastest sanity check (~3 s wall) |
| `bench_1k.json` / `bench_10k.json` / `bench_100k.json` | Stepwise size sweep |
| `hash_20.json` / `hash_22.json` / `hash_24.json` | Vary `F` / `poly_modulus_degree` |
| `ytf_size_10k.json` / `ytf_size_100k.json` / `ytf_size_1m.json` | YouTube-Faces dataset, three sizes |
| `ytf_ml_12.json` ... `ytf_ml_24.json` | YouTube-Faces with varying alphabet sizes |

Generate fresh configs:

```bash
python3 experiments/configs/generate_configs.py
```

## See also

- [`INSTRUMENTATION.md`](INSTRUMENTATION.md) — what the JSONL output captures
- [`ARCHITECTURE.md`](ARCHITECTURE.md) — module map + protocol flow
- [`NETWORK_PROTOCOL.md`](NETWORK_PROTOCOL.md) — wire format for sender↔receiver
