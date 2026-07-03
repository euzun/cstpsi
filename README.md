# CSTPSI

[![License: PolyForm Noncommercial 1.0.0](https://img.shields.io/badge/License-PolyForm%20Noncommercial%201.0.0-blue.svg)](LICENSE) [![arXiv](https://img.shields.io/badge/arXiv-2606.27803-b31b1b.svg)](https://arxiv.org/abs/2606.27803) [![IACR ePrint](https://img.shields.io/badge/IACR%20ePrint-2026%2F1322-6f42c1.svg)](https://eprint.iacr.org/2026/1322)

**Composable Set-Threshold Labeled Private Set Intersection.** Research artifact for the paper *Reliable Homomorphic Matching for Fuzzy Labeled PSI at Scale*. Two parties run a protocol in which the receiver learns, for each of its queries, the labels of database rows whose item set matches the query set on at least *k* of *N* items, while the sender learns nothing about the query and the receiver learns nothing about non-matching rows.

## Summary

An FLPSI match is decided under encryption by a set-threshold kernel. Efficiency there costs a per-trial false-accept probability, and each query runs one trial per record, so the error compounds with database size into the kernel's *realization soundness error* (RSE): the rate at which it accepts a query the plaintext matcher would reject. At a million records the baseline kernel's RSE reaches 100% (Figure 2; 0.999 on real Deep1B data, Table IV), while CSTPSI runs independent token rounds and holds RSE at 0 in every measured configuration, at no recall cost. The security is nearly free: CSTPSI decouples cost from label size and, for large labels at small-to-moderate scale, runs more than 20x faster with up to 93% less communication (Table III, Figures 3-4), converging to the baseline only at million scale.

## Quick start

Build dependencies and the CLI binary:

```bash
./scripts/install_deps.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Run the smoke test (1K-row synthetic DB, ~1 minute):

```bash
DYLD_LIBRARY_PATH=build/emp_install/lib \
  ./build/cstpsi_cli bench \
  --params experiments/configs/smoke_1k_N64_k2.json \
  --output-jsonl /tmp/run.jsonl
python3 scripts/validate_jsonl.py /tmp/run.jsonl
```

See `INSTALL.md` for full dependency notes (SEAL, FLINT, EMP-toolkit, ZeroMQ, OpenMP, CMake 3.15+).

## Claims and Reproducing Experiments

| ID | Claim | Section | Command | Time (est.) |
|----|-------|---------|---------|-------------|
| C1 | STLPSI (T=1) realization soundness error (RSE) grows with D: ~56% at D=100K (100% at D=1M) | Eval: RSE (Figure 2) | `bash experiments/reproduce.sh --target far` | hours |
| C2 | CSTPSI (T=2) holds RSE at 0 in every measured cell through D=1M | Eval: RSE (Figure 2, Table IV) | `bash experiments/reproduce.sh --target far` | included above |
| C3 | T=3 sweep confirms RSE=0 across all D (sufficient at billion scale) | Security (Corollary 1) | `bash experiments/run.sh a1` | hours |
| C4 | CSTPSI >20x online speedup vs STLPSI (peak: 64-byte label, D=1K, 8 threads = 21.2x) | Eval: performance (Table III, Figure 3) | `bash claims/repro-table/run.sh` | hours |
| C5 | Up to ~93% communication saving (send-once caching; peak: 64-byte label, D=1K) | Eval: performance (Table III, Figure 4) | `bash claims/repro-table/run.sh` | included in C4 |
| C6 | Advantage grows with label size (~2x at 23-bit, ~6.7x at 16B, ~12x at 32B, ~21x at 64B at small D); converges to baseline at D=1M | Eval: performance (Table III) | `bash claims/repro-table/run.sh` | included in C4 |
| C7 | FRR=0 and label_mismatch=0 (correctness) | Eval: correctness | `bash experiments/run.sh m6` | ~2 min |
| C8 | Unit tests pass | Artifact | `ctest --test-dir build` | ~2 min |

### One-command reproduction of the performance table

```bash
bash claims/repro-table/run.sh        # defaults: 2 TP + 2 TN per cell, D up to 100K
```

Generates the benchmark databases, runs every cell (D x label x threads x {STLPSI, CSTPSI}), collects the measurements, and renders the head-to-head table (text + LaTeX). Override fidelity/scope with env vars `TP TN SIZES LABELS THREADS` (`FULL=1` adds the 1M rows). See `claims/repro-table/claim.txt` for the claim and metric definitions.

### Full paper reproduction (every table and figure)

`experiments/reproduce.sh` is the single-command harness that regenerates **every** table and figure in the paper (head-to-head, the RSE-vs-D curve, the ablation, and the Deep1B/LFW real-data soundness results):

```bash
bash experiments/reproduce.sh            # all targets   (--list shows them, --help all flags)
bash experiments/reproduce.sh --target headtohead --sizes 100k --labels 64byte   # one cell
```

The Deep1B and LFW datasets download automatically from the release assets on first run. See `experiments/README.md` for the target/flag reference.

## Run with Docker

A multi-stage `Dockerfile` builds a hermetic, pinned environment (Ubuntu 22.04 + SEAL/EMP/FLINT) so the artifact runs the same on any host with Docker, with no system packages installed on the host. Build native to your CPU architecture (avoid x86 emulation).

```bash
docker build -t cstpsi .                  # first build ~10-15 min (compiles deps)

docker run --rm        cstpsi verify      # re-run the test suite (ctest)
docker run --rm        cstpsi repro       # reproduce the head-to-head table
docker run --rm        cstpsi demo        # two-party network demo (single container)
docker run --rm -it    cstpsi shell       # interactive shell

# higher fidelity / wider grid (see claims/repro-table/claim.txt):
docker run --rm -e TP=20 -e TN=20 cstpsi repro
```

Distributed demo (sender and receiver in **separate** containers on a private network):

```bash
docker compose up        # sender serves; receiver queries and verifies recovery
docker compose down -v   # tear down + remove the shared data volume
```

Notes: the test suite runs at image-build time (the build fails if it does not pass). Absolute latency under a container/VM is *not* the claim. The reproducible results are the CSTPSI/STLPSI speedup ratio, the communication saving, and the zero false-accept (T=2) correctness. The 1M-row cells need a large `--memory` allowance and are off by default. (For a real two-host deployment, pass an **IP** to the receiver's `--senderAddr`: the garbled-circuit channel connects by address and does not resolve hostnames.)

## Repo layout

| Path | Purpose |
|------|---------|
| `app/` | CLI binaries: in-process `cli`, network-split `sender` and `receiver` |
| `src/` | Protocol implementation: cryptography, IO, instrumentation |
| `tests/` | Unit and integration tests (GoogleTest) |
| `demo/` | End-to-end demo scripts (in-process and network mode) |
| `parameters/` | Reusable protocol-parameter JSON files |
| `experiments/` | Benchmark configs, synthetic datasets, and paper-evaluation scripts |
| `scripts/` | Dependency installer and JSONL output validator |
| `docs/` | Installation, architecture, parameters, and instrumentation |

## Documentation

- `docs/INSTALL.md` -- build and dependency setup
- `docs/ARCHITECTURE.md` -- protocol design and module layout
- `docs/PARAMETERS.md` -- parameter reference
- `docs/INSTRUMENTATION.md` -- JSONL telemetry schema for reviewers
- `docs/NETWORK_PROTOCOL.md` -- sender/receiver wire protocol

## License

PolyForm Noncommercial License 1.0.0. The code is publicly available for
noncommercial use (academic research, teaching, evaluation); commercial use
requires a separate license. See `LICENSE` for the full text and `NOTICE` for
the third-party dependency licenses.

## Citation

This repository is the artifact for the CSTPSI paper. If you use it, please cite:

```bibtex
@misc{cryptoeprint:2026/1322,
      author = {Erkam Uzun},
      title = {Reliable Homomorphic Matching for Fuzzy Labeled {PSI} at Scale},
      howpublished = {Cryptology {ePrint} Archive, Paper 2026/1322},
      year = {2026},
      url = {https://eprint.iacr.org/2026/1322}
}
```

The work builds on the FLPSI predecessor (USENIX Security 2021):

```bibtex
@inproceedings{uzun2021fuzzy,
  title     = {Fuzzy Labeled Private Set Intersection with Applications to Private Real-Time Biometric Search},
  author    = {Uzun, Erkam and Chung, Simon P. and Kolesnikov, Vladimir and Boldyreva, Alexandra and Lee, Wenke},
  booktitle = {30th USENIX Security Symposium (USENIX Security 21)},
  year      = {2021},
  pages     = {911--928}
}
```
