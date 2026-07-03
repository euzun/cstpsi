# CSTPSI Installation Guide

Build instructions for the CSTPSI reference implementation. Tested on macOS
(Apple Silicon, Sonoma+) and Linux (Ubuntu 22.04 / 24.04). All dependencies
are either system-installable via Homebrew / apt, or auto-fetched by CMake.

## Quick start (macOS, Homebrew)

```bash
brew install cmake seal flint gmp mpfr zeromq cppzmq nlohmann-json libomp openssl
git clone https://github.com/euzun/STLPSI.git cstpsi && cd cstpsi
mkdir build && cd build
cmake ..
cmake --build . --target cstpsi_cli -j8
```

Verify with the smoke test (from repo root):

```bash
DYLD_LIBRARY_PATH=build/emp_install/lib \
  ./build/cstpsi_cli bench \
  --params experiments/configs/smoke_1k_N64_k2.json \
  --output-jsonl /tmp/smoke.jsonl
python3 scripts/validate_jsonl.py /tmp/smoke.jsonl
```

Expected output ends with:

```
FRR = 0.0000% (expected 0%)
FAR = 0.0000%
OK: FRR=0% confirmed for all 2 true-positive queries.
...
Instrumentation output written to: /tmp/smoke.jsonl
```

and `validate_jsonl.py` prints `✓ Validation passed`.

## Dependencies

| Library | Version pin | Source | Purpose |
|---------|-------------|--------|---------|
| Microsoft SEAL | 4.1.2 | system (Homebrew / apt) | BFV homomorphic encryption |
| FLINT | 3.4.x | system | Fast polynomial arithmetic over GF(p) |
| GMP | 6.x | system | Arbitrary precision integers (FLINT dep) |
| MPFR | 4.x | system | Multiple precision floating point (FLINT dep) |
| ZeroMQ | 4.x | system | Network transport for sender/receiver mode |
| cppzmq | 4.x | system | C++ bindings for ZeroMQ |
| nlohmann/json | 3.x | system | Config + JSONL I/O |
| OpenMP | bundled with compiler / libomp | system | Threaded inner loops |
| OpenSSL | 1.1+ / 3.x | system | EMP-toolkit dependency |
| EMP-toolkit (tool/ot/sh2pc) | 0.2.5 / 0.2.4 / 0.2.2 | auto-fetched | Garbled-circuit AES for 1-GC OPRF |

EMP is fetched by CMake's `ExternalProject_Add` at configure time, pinned to
pre-3.0 release tags that match the API CSTPSI's code expects. You do not
install EMP system-wide; it lives in `build/emp_install/`.

## Platform setup

### macOS (Apple Silicon)

```bash
# Xcode CLT (one-time)
xcode-select --install

# All deps via Homebrew
brew install cmake seal flint gmp mpfr zeromq cppzmq nlohmann-json libomp openssl

# Optional: install pkg-config so CMake locates everything cleanly
brew install pkg-config
```

When running the built binaries, set `DYLD_LIBRARY_PATH` so the dynamic
linker finds the EMP shared libraries:

```bash
export DYLD_LIBRARY_PATH=$(pwd)/build/emp_install/lib
```

(or prefix each invocation with `DYLD_LIBRARY_PATH=... ./build/cstpsi_cli ...`)

### Ubuntu 22.04 / 24.04

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake git \
    libssl-dev libgmp-dev libmpfr-dev libflint-dev \
    libzmq3-dev nlohmann-json3-dev \
    libomp-dev
```

SEAL 4.1 is not in the Ubuntu archives at the time of writing; install from
source:

```bash
git clone --branch v4.1.2 https://github.com/microsoft/SEAL.git
cd SEAL && cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release -DSEAL_USE_INTRIN=ON -DSEAL_BUILD_DEPS=OFF
cmake --build build -j$(nproc) && sudo cmake --install build
```

cppzmq is header-only on most distros; if `<zmq.hpp>` is not found, install:

```bash
git clone https://github.com/zeromq/cppzmq.git
cd cppzmq && mkdir build && cd build && cmake .. -DCPPZMQ_BUILD_TESTS=OFF
sudo make install
```

When running binaries on Linux:

```bash
export LD_LIBRARY_PATH=$(pwd)/build/emp_install/lib
```

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

This produces three binaries in `build/`:

- `cstpsi_cli` — single-process synthetic benchmark + CSV-driven runs
- `cstpsi_sender` — network server (long-running)
- `cstpsi_receiver` — network client

Plus the EMP install tree at `build/emp_install/` (created at first configure;
fully rebuilds with `rm -rf build/emp_install build/emp_*_build && cmake ..`).

### Useful CMake options

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug                  # debug build
cmake .. -DCMAKE_PREFIX_PATH=/opt/homebrew         # alt deps location
cmake .. -DSEAL_DIR=/usr/local/lib/cmake/SEAL-4.1  # pin SEAL location
```

### Build target shortcuts

```bash
cmake --build . --target cstpsi_cli       # benchmark + CLI only
cmake --build . --target cstpsi_sender    # server only
cmake --build . --target cstpsi_receiver  # client only
```

## Verification

### Smoke test (CLI mode, single process)

```bash
DYLD_LIBRARY_PATH=build/emp_install/lib \
  ./build/cstpsi_cli bench \
  --params experiments/configs/smoke_1k_N64_k2.json \
  --output-jsonl /tmp/smoke.jsonl
python3 scripts/validate_jsonl.py /tmp/smoke.jsonl
```

Pass criteria: `FRR=0%`, no validator errors, JSONL file contains both an
`offline_once_per_session` block with non-zero step timings and at least one
entry in `queries[]`.

### Network-mode smoke (sender + receiver across two terminals)

Terminal A:

```bash
DYLD_LIBRARY_PATH=build/emp_install/lib \
  ./build/cstpsi_sender \
  --dbFile experiments/results/smoke_test/enroll.csv \
  --paramsFile experiments/configs/smoke_1k_N64_k2.json \
  --port 1212
```

Terminal B:

```bash
DYLD_LIBRARY_PATH=build/emp_install/lib \
  ./build/cstpsi_receiver \
  --queryFile experiments/results/smoke_test/query.csv \
  --paramsFile experiments/configs/smoke_1k_N64_k2.json \
  --senderAddr 127.0.0.1 --port 1212 \
  --outputFile /tmp/cstpsi_network_smoke.csv
```

The receiver writes matched IDs to `/tmp/cstpsi_network_smoke.csv` and the
sender exits cleanly on session close.

### Unit tests

```bash
cmake --build build --target test_shamir test_aes_reduction test_gc_roundtrip
./build/test_shamir
./build/test_aes_reduction
./build/test_gc_roundtrip
```

## Troubleshooting

**`Library not loaded: libemp-tool.dylib`** on macOS — the EMP shared
libraries are installed under `build/emp_install/lib/`, which isn't in the
default search path. Either prefix every invocation with
`DYLD_LIBRARY_PATH=build/emp_install/lib`, or export it in your shell.

**EMP build fails on `bytes_recv` / `bytes_sent` errors** — you're picking up
EMP's `main` branch which has API drift. The repo pins to `0.2.5` /
`0.2.4` / `0.2.2` (tool / ot / sh2pc). If your build was started before this
pin, force a clean re-fetch:

```bash
rm -rf build/emp_tool_build build/emp_ot_build build/emp_sh2pc_build build/emp_install
cmake .. && cmake --build . -j$(nproc)
```

**`Could not find SEAL`** — CMake can't locate the SEAL CMake package.
Inspect the install location and pass it explicitly:

```bash
# Homebrew default on Apple Silicon:
cmake .. -DSEAL_DIR=/opt/homebrew/lib/cmake/SEAL-4.1
# Apt / source install:
cmake .. -DSEAL_DIR=/usr/local/lib/cmake/SEAL-4.1
```

**`CMake Error: Compatibility with CMake < 3.5 has been removed`** during EMP
configure — already handled by `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` in our
EMP `ExternalProject_Add` blocks; if you still see it, your CMake is older
than 3.20. Upgrade CMake (Homebrew: `brew install cmake`; apt: see
`https://apt.kitware.com/`).

**OpenMP not found on macOS** — `brew install libomp` then re-run cmake.

## Next steps

- [`PARAMETERS.md`](PARAMETERS.md) — JSON config schema + CLI flag reference.
- [`ARCHITECTURE.md`](ARCHITECTURE.md) — protocol flow, module map, threading model.
- [`INSTRUMENTATION.md`](INSTRUMENTATION.md) — JSONL schema + how to re-derive paper tables from raw data.
- [`NETWORK_PROTOCOL.md`](NETWORK_PROTOCOL.md) — wire format for sender/receiver mode.
