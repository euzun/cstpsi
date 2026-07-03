# CSTPSI Architecture

CSTPSI is **Composable Set-Threshold PSI**: a threshold-labeled fuzzy
PSI protocol that absorbs FAR control into the primitive itself via a
multi-token-round parameter `T`. This document describes the reference
C++ implementation in this repository.

For the formal specification, see the paper sources under
`docs/paper/sections/`. The protocol figure is
`fig:cstpsi-protocol` in `docs/paper/sections/05_building_blocks.tex`.

## Roles

- **Sender (S)** — holds a database `Db = {(x_e, ℓ_{x_e})}_{e ∈ [D]}` where
  each `x_e ∈ D^N` is an `N`-tuple of items and `ℓ_{x_e} ∈ L` is an opaque
  label. Runs as `cstpsi_sender` (network server) or as the
  sender side of a single-process `cstpsi_cli bench` / `csv_run`.
- **Receiver (R)** — issues queries `Y = (y_1, ..., y_N)`. Learns
  `{ℓ_{x_e} : |{j : y_j = x_{e,j}}| ≥ k}` and nothing else. Runs as
  `cstpsi_receiver` or as the receiver side of `cstpsi_cli`.

Threshold `k` is hardcoded to `2`. See the
[footnote in §VI.A of the paper](../paper/sections/05_building_blocks.tex)
for the design rationale (larger `k` blows up the per-pair `binom(N,k)`
work while the FAR amplification is recovered more cheaply by raising `T`).

## Repository layout

```
.
├── app/                          # Binary entry points
│   ├── cli.cc                    # cstpsi_cli (bench + csv_run, loopback)
│   ├── sender.cc                 # cstpsi_sender (network server)
│   └── receiver.cc               # cstpsi_receiver (network client)
├── src/
│   ├── protocol/                 # Core protocol logic
│   │   ├── params.cc/h           # Global parameters (configurable + derived)
│   │   ├── helper.h              # Vector utilities + field_mod_inverse
│   │   ├── helper.cc             # (small) shared helpers
│   │   ├── modpoly.c/h           # FLINT-based Shamir share construction
│   │   ├── plain.cc/h            # Plaintext baseline (correctness reference)
│   │   ├── sender.cc/h           # Sender offline + per-round homomorphic eval
│   │   ├── receiver.cc/h         # Receiver online + repK2 reconstruction
│   │   └── core.cc               # submitSingleQuery + measure_time helpers
│   ├── gc/                       # Garbled-circuit OPRF (EMP-toolkit)
│   │   ├── aes_key.h             # AES key generation
│   │   ├── aes_gc.cc/h           # gcSenderRole / gcReceiverRole / gcSimulateLocal
│   ├── network/                  # ZeroMQ wire layer
│   │   ├── network.cc/h          # REQ/REP socket wrappers; bytes_sent / bytes_received counters
│   │   └── serialization.cc/h    # SEAL ciphertext / plaintext (de)serialization
│   └── io/
│       ├── param_loader.cc/h     # JSON config parser (nlohmann/json)
│       ├── csv.cc/h              # CSV enrollment / query / output I/O
│       ├── instrumentation.cc/h  # Fine-grained JSONL output for §VIII data
│       └── io.h                  # Common I/O includes
├── tests/                        # GoogleTest unit + integration tests
├── experiments/                  # Benchmark configs, scripts, analysis
├── demo/                         # Live demo scripts (sender/receiver across terminals)
├── docs/                         # This documentation tree
│   ├── ARCHITECTURE.md           # ← this file
│   ├── INSTALL.md
│   ├── PARAMETERS.md
│   ├── INSTRUMENTATION.md
│   ├── NETWORK_PROTOCOL.md
│   └── paper/                    # Paper sources (LaTeX)
└── CMakeLists.txt
```

CMake produces three binaries (`cstpsi_cli`, `cstpsi_sender`,
`cstpsi_receiver`) and the following static libraries:

`cstpsi_params`, `cstpsi_helper`, `cstpsi_modpoly`, `cstpsi_plain`,
`cstpsi_gc`, `cstpsi_network`, `cstpsi_io`, `cstpsi_receiver_lib`,
`cstpsi_sender_lib`, `cstpsi_core`.

EMP-toolkit (`emp-tool` 0.2.5, `emp-ot` 0.2.4, `emp-sh2pc` 0.2.2) is
auto-fetched and installed under `build/emp_install/`.

## Component map → protocol steps

Each numbered step below corresponds to a labelled step in
`fig:cstpsi-protocol` of the paper.

| Step | What it does | Implementation |
|------|--------------|----------------|
| 1 [Init] | `S` samples `κ ← {0,1}^128` | `src/gc/aes_key.h:generateAesKey()` |
| 2 [partition] | `S` partitions `Db` into `n_part = ⌈D/s_part⌉` partitions | `src/protocol/sender.cc:parallelPartitionDB()` |
| 3 [blind] | `S` computes `b_{e,i} = AES_κ(x_{e,i}) mod F` | `src/gc/aes_gc.cc:blindDatabaseOffline()` (with `blindItemOffline()` per item) |
| 4 [share] | Per row `e`, round `t`: draw Shamir polynomial `P_{t,e}(x)` over `F_F` | `src/protocol/modpoly.c:createSecretShares()` via `core.cc:setShares()` |
| 5 [interp & pack] | Per `(t, i, p)`: interpolate `f_{t,i,p}` through `{(b_{e,i}, P_{t,e}(i))}` and SIMD-pack `m/N` partitions per BFV plaintext | `sender.cc:computeCoefficients()` → `simdPartitions()` → `encodeCoeffs()` |
| 6 [1-GC] | `R` and `S` evaluate AES-128 in semi-honest 2PC garbled circuit; `R` learns `b_j`, `S` learns `⊥` | `src/gc/aes_gc.cc:gcSenderRole()`, `gcReceiverRole()`, `gcSimulateLocal()` (loopback) |
| 7 [Query powers] | `R` encrypts `{Enc(b_j^ℓ)}_{ℓ ∈ [0,d)}` (Option A, full-receiver-side; see paper §VI footnote re. BSGS variant in App A); `S` caches | `receiver.cc:computeQueryPowers()` → `simdQueryPowers()` → `encryptQueryPowers()` |
| 8 [Per-round HE eval] | Per round `t`, per partition `p`: `S` computes `Enc(f_{t,i,p}(b_j))` via plaintext-ciphertext inner product; returns to `R`; `R` decrypts | `sender.cc:homomorphicPolyEval()` → `evalPartition()`; receiver decrypt: `receiver.cc:decryptQueryResult()` |
| 9 [Token check] | `R` computes the two-point Lagrange combination `KR(i,j,t)` over every pair `(i,j) ∈ binom([N], k)`; pair survives iff `KR = 0` in every token round | `receiver.cc:repK2()` invoked from `checkPartitionPairs()` and `findVerifiedPairs()` |
| 10 [Label recovery] | For each surviving pair: compute `KR` for each label round to recover the `K` chunks of `ℓ_{x_e}` | `receiver.cc:reconstructLabels()` |

`KR` in the paper is the abstract `k`-out-of-`N` Shamir reconstructor of
§VI.A; the figure box (and the implementation symbol) uses `repK2` for the
`k=2` instantiation that all current configs run at.

## Protocol orchestration

### Loopback (`cstpsi_cli`)

In single-process mode, `app/cli.cc:runStlpsiBench()` (despite the legacy
function name) walks the steps sequentially in-memory:

1. Parse config + apply to globals (`src/io/param_loader.cc`)
2. Generate synthetic enrollment DB
3. Step 4: `setShares(pid)` per enrolled id (Shamir share construction)
4. Steps 1–3 (init, partition, blind)
5. Step 5 (interpolate + pack)
6. Step 6: `gcSimulateLocal()` — runs sender + receiver halves of the GC in
   a tight loop on `127.0.0.1`, single AES garbled circuit per session
7. Per-query: steps 7 (query powers) → 8 (hom eval + decrypt) → 9 (token
   check) → 10 (label recovery)
8. Compute FRR / FAR over all queries

Single-process mode is what powers the **fine-grained instrumentation suite**
(see [`INSTRUMENTATION.md`](INSTRUMENTATION.md)). When `--output-jsonl` is
set, queries run sequentially (no outer OpenMP-for over queries) to avoid
per-thread timer collisions on the shared `Instrumentation` object; inner
OpenMP regions inside each query (e.g. `homomorphicPolyEval`'s
`#pragma omp parallel for collapse(2)`) still scale to the configured
thread count.

### Network mode (`cstpsi_sender` + `cstpsi_receiver`)

In two-process mode, the same protocol steps map onto a request/reply
pattern over ZeroMQ (`src/network/network.cc`):

```
┌────────────────────┐         ┌────────────────────┐
│ cstpsi_receiver    │         │ cstpsi_sender      │
│  (REQ socket)      │ ◀─────▶ │  (REP socket)      │
│                    │         │                    │
│ - load query CSV   │         │ - load enroll CSV  │
│ - 1-GC client side │  step 6 │ - 1-GC server side │
│ - encrypt powers   │  step 7 │ - cache powers     │
│ - send query       │ ────▶   │                    │
│                    │         │ - hom eval (step 8)│
│ - recv ciphertexts │ ◀────── │                    │
│ - decrypt          │  step 8 │                    │
│ - repK2 + label    │ steps   │                    │
│   recovery         │ 9, 10   │                    │
│ - write CSV        │         │                    │
└────────────────────┘         └────────────────────┘
```

Message types are defined in `src/network/network.h` (`QUERY_REQUEST`,
`QUERY_RESPONSE`, `SESSION_SETUP`, `ERROR_RESPONSE`). Byte-on-wire counters
in the cppzmq wrapper feed instrumentation `bytes_r_to_s` / `bytes_s_to_r`.

## Cryptographic primitives + parameters

### BFV (Microsoft SEAL 4.1.2)

- `poly_modulus_degree`: 4096 (configurable via `seal_params.poly_modulus_degree`)
- `coeff_modulus`: `CoeffModulus::BFVDefault(poly_modulus_degree)` (~128-bit security at 4096)
- `plain_modulus`: 8519681 (a 23-bit prime, configurable via `seal_params.plain_modulus`). Also sets the global `FIELD_MODULUS` used by Shamir / repK2 arithmetic.

Per-query online evaluation is a degree-`(partition_size - 1)` univariate
polynomial per query position, evaluated in batched form across
`floor(m / N)` partitions per ciphertext.

### Garbled-Circuit OPRF (EMP-toolkit)

`src/gc/aes_gc.cc` realizes the OPRF as a single AES-128 garbled circuit in
the EMP-toolkit's semi-honest 2PC framework. The sender holds the key, the
receiver inputs query items, the receiver alone learns
`b_j = AES_κ(y_j) mod F` (with the convention `0 ↦ 1` to avoid leaking a
secret-dependent zero). The garbled circuit is executed **exactly once per
online session** — the **1-GC optimization** — and the resulting blinded
queries are reused across all `T + K` subsequent rounds.

### Shamir secret sharing (FLINT)

`src/protocol/modpoly.c` uses FLINT's `nmod_poly` arithmetic over `F_F`
(`F = FIELD_MODULUS = 8519681` by default) to construct degree-`(k-1) = 1`
Shamir polynomials per row per round. The secret for token rounds
`t ∈ [0, T)` is `0`; for label rounds `t ∈ [T, T + K)` it is the
`(t - T + 1)`-th chunk of `ℓ_{x_e}`.

## Notable design choices

### 1-GC optimization

Instead of `K + 1` separate garbled-circuit executions (one per round, as in
FLPSI'21), CSTPSI executes the GC exactly once per session. Step 6 produces
blinded `(b_1, ..., b_N)`; step 7 encrypts them; step 8's per-round
ciphertext bundle is **the only thing that varies per round**. The
`--disable-1gc` flag on `cstpsi_sender` / `cstpsi_receiver` restores the
FLPSI'21 multi-GC path for ablation.

### Multi-token-round amplification (`T`)

Setting `T ≥ 2` drives the per-trial FAR from `1/F` to `1/F^T`. Independence
of the `T` per-round reconstructions reduces to independence of Shamir
coefficient samples per round (see `--nrofTokenRounds T` flag,
[`PARAMETERS.md`](PARAMETERS.md) for the bound table). This is the central
contribution of CSTPSI vs. FLPSI'21's single-token configuration.

### Option A vs BSGS for step 7

Paper §VI + Appendix A describe the **BSGS sparse-window** variant of step
7 as canonical. The reference artifact in this repo implements **Option A**
(full-receiver-side encryption of the complete query-power set,
cached on sender). The trade-off (network bandwidth vs. computation +
ciphertext-level mults) is documented in the §VI.B footnote of the paper.

### Instrumentation framework

`src/io/instrumentation.{cc,h}` emits a per-session JSONL record capturing
per-step (and select sub-step) wall-clock + RSS + bytes-on-wire. Smoke and
benchmark scripts route this via the `--output-jsonl <path>` flag on
`cstpsi_cli`. Analysis scripts under `scripts/analysis/` (planned for AE
submission; see [`INSTRUMENTATION.md`](INSTRUMENTATION.md)) re-derive paper
tables and figures from the raw JSONL so reviewer asks about
"what about RSS at step X?" don't require re-running experiments.

## Threading model

- **Outer parallelism (query-level)**: only in `cstpsi_cli` uninstrumented
  mode; `#pragma omp parallel for` across queries. Disabled for instrumented
  runs (sequential, to avoid timer collisions).
- **Inner parallelism (per-query)**: enabled everywhere via `OpenMP`:
  - `parallelPartitionDB` (sender.cc:87)
  - `computeCoefficients` (sender.cc:200)
  - `encodeCoeffs` (sender.cc:249)
  - `homomorphicPolyEval` (sender.cc:267) — `collapse(2)` over `(i, p)` pairs
  - `computePolyModSwitch` (sender.cc:267)
- Thread count: `performance_params.nrof_online_threads` in JSON config; overridable
  via `--threads <int>` on each binary.

## Test surface

`tests/` contains GoogleTest-based unit + integration tests. Most relevant:

- `tests/unit/test_shamir.cc` — Shamir share construction + reconstruction
  round-trip, including the `repK2` two-point Lagrange identity
- `tests/unit/test_aes_reduction.cc` — `AES_κ(·) mod F` blinding correctness
- `tests/unit/test_gc_roundtrip.cc` — GC client/server roundtrip (CI-skips
  when EMP is unavailable, falling back to local-sim)
- `tests/unit/test_param_loader.cc` — JSON config schema validation
- `tests/integration/test_csv.cc` — CSV enrollment + query I/O + plaintext
  baseline match
- `tests/unit/test_serialization.cc` — SEAL ciphertext (de)serialization
  for network mode

Run with `cmake --build build --target <test_name> && ./build/<test_name>`.

## Comparison to FLPSI'21

This repo originated as a clean-room reimplementation of the FLPSI USENIX'21
threshold labeled fuzzy PSI (`uzun2021fuzzy`). The current code path keeps
that lineage as an explicit baseline (the `--disable-1gc` flag plus
`--nrofTokenRounds 1` reproduces FLPSI'21's single-token, multi-GC
behavior), but CSTPSI's contribution is structural:

| Aspect | FLPSI'21 | CSTPSI (this repo) |
|--------|----------|--------------------|
| GC executions per session | `K + 1` (one per round) | **1** (single AES GC reused) |
| Token rounds | 1 (per-trial FAR `≈ 1/F`) | `T ≥ 2` (per-trial FAR `≈ 1/F^T`) |
| FAR composition across DB | caller-amplified via OKVS replication (`p → p^c`) | absorbed into primitive via `T` |
| Threshold semantics | `k`-of-`N` labeled match | `k`-of-`N` labeled match (unchanged) |
| Codebase posture | research prototype | publication-ready artifact with JSONL instrumentation, network mode, ablation flags |

See `docs/paper/sections/03_overview.tex` for the formal framing.

## See also

- [`INSTALL.md`](INSTALL.md) — dependency install + build steps
- [`PARAMETERS.md`](PARAMETERS.md) — JSON config schema + CLI flags
- [`INSTRUMENTATION.md`](INSTRUMENTATION.md) — JSONL schema + smoke-test harness
- [`NETWORK_PROTOCOL.md`](NETWORK_PROTOCOL.md) — wire format details
