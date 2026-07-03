# Installation Guide

## Supported Platforms

| Platform | Status | Notes |
|----------|--------|-------|
| Ubuntu 22.04 LTS | Recommended | Tested with GCC 11 |
| Ubuntu 20.04 LTS | Supported | GCC 9 minimum; install GCC 11 via ppa |
| macOS 13 (Ventura) or later | Supported | Apple Clang 15; Homebrew required |
| Other Linux | Likely works | Install dependencies manually |

## System Requirements

- C++17-capable compiler (GCC 11+, Clang 14+, or Apple Clang 15+)
- CMake 3.15 or newer
- Git (for cloning and for CMake ExternalProject to fetch EMP-toolkit)
- Internet access during first build (EMP-toolkit fetched from GitHub)
- Python 3.8+ (for experiment scripts; no non-standard libraries required)
- RAM: 8 GB minimum; 16 GB recommended; 36 GB for D=1M experiments

---

## Quick Install (macOS or Ubuntu/Debian)

A single script handles all system-level dependencies:

```bash
bash install.sh
```

This installs CMake, SEAL, FLINT, GMP, MPFR, ZeroMQ, cppzmq, nlohmann-json, Boost
(headers), libuuid, OpenMP, OpenSSL, and GoogleTest via Homebrew (macOS) or apt-get
(Ubuntu/Debian), then configures and builds the project. On Ubuntu it also builds
Microsoft SEAL from source and vendors the cppzmq header (neither is packaged).

EMP-toolkit (emp-tool 0.2.5, emp-ot 0.2.4, emp-sh2pc 0.2.2) is NOT installed by
this script -- it is fetched and built automatically by CMake the first time you run
`cmake --build`.

---

## Manual Dependency Installation

### Ubuntu 22.04

```bash
sudo apt-get update -y
sudo apt-get install -y \
    cmake build-essential git \
    libflint-dev libgmp-dev libmpfr-dev \
    libzmq3-dev nlohmann-json3-dev libboost-dev uuid-dev \
    libssl-dev libgomp1 libgtest-dev
```

cppzmq (the C++ header binding for ZeroMQ) is header-only and not packaged on
Ubuntu before 23.04. Vendor the headers from upstream:

```bash
git clone --depth 1 --branch v4.10.0 https://github.com/zeromq/cppzmq.git /tmp/cppzmq
sudo cp /tmp/cppzmq/zmq.hpp /tmp/cppzmq/zmq_addon.hpp /usr/local/include/
```

(On Ubuntu 23.04+ install the `cppzmq-dev` package instead.)

Microsoft SEAL is not in the standard Ubuntu repositories. Build from source:

```bash
git clone https://github.com/microsoft/SEAL.git /tmp/SEAL
cd /tmp/SEAL
git checkout 4.1.2   # or latest stable 4.x tag
cmake -S . -B build -DSEAL_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
cd -
```

### macOS (Homebrew)

```bash
brew install cmake seal flint gmp mpfr zeromq cppzmq nlohmann-json boost libomp googletest openssl@3
```

(libuuid ships with the macOS SDK, so there is no Homebrew formula for it.)

---

## Build

```bash
# Configure -- EMP-toolkit will be cloned+built on first build (adds ~10 min)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Compile everything
cmake --build build -j$(nproc)            # Linux
cmake --build build -j$(sysctl -n hw.ncpu)  # macOS
```

This produces three executables in `build/`:
- `cstpsi_cli` -- interactive in-process demo
- `cstpsi_sender` -- network server (sender party)
- `cstpsi_receiver` -- network client (receiver party)

### macOS: EMP shared-library path

The EMP shared library (`libemp-tool.dylib`) is installed under `build/emp_install/lib`.
Because no rpath is embedded in the binaries, you must export the path before running them:

```bash
export DYLD_LIBRARY_PATH=build/emp_install/lib
```

The experiment harness (`experiments/run.sh` and `experiments/lib/run_cell.sh`) sets this
automatically. Only needed if you invoke the binaries directly.

On Linux this is not required (the .so has an appropriate RPATH or is found via ldconfig).

---

## Verify the Build

```bash
# Run all unit and integration tests
cd build
ctest --output-on-failure

# Expected: 13 tests PASS (12 if GoogleTest is absent)
# Note: test_serialization is only built if GoogleTest is installed
cd ..
```

---

## Minimal Smoke Run (verifies the full stack)

```bash
export DYLD_LIBRARY_PATH=build/emp_install/lib   # macOS only

./build/cstpsi_cli bench \
    --params experiments/configs/smoke_1k_N64_k2.json \
    --output-jsonl /tmp/smoke.jsonl

python3 scripts/validate_jsonl.py /tmp/smoke.jsonl
```

Expected runtime: ~30-60 seconds. Produces a JSONL file with one record per query.

---

## Dependency Version Reference

| Package | Version used in development | Notes |
|---------|----------------------------|-------|
| SEAL | 4.1.x (any 4.x works) | BFV scheme; find_package looks for SEAL 3.5+ |
| EMP-toolkit emp-tool | 0.2.5 | Pinned in CMakeLists.txt via GIT_TAG |
| EMP-toolkit emp-ot | 0.2.4 | Pinned |
| EMP-toolkit emp-sh2pc | 0.2.2 | Pinned |
| FLINT | 2.9+ | Polynomial arithmetic for Shamir SS |
| GMP | 6.x | Required by FLINT |
| MPFR | 4.x | Required by FLINT |
| ZeroMQ | 4.3+ | Network transport |
| cppzmq | 4.9+ | C++ header wrapper for ZeroMQ |
| nlohmann/json | 3.2.0+ | JSON parameter files |
| Boost | 1.7x (headers) | String algorithms used by the CLI |
| libuuid | system | Session ids; macOS SDK provides it, Ubuntu uses uuid-dev/libuuid1 |
| OpenMP | 4.5+ | Parallel loops; optional (single-thread fallback) |
| OpenSSL | 1.1.x or 3.x | AES in GC; system-provided |

---

## Troubleshooting

**EMP build fails during CMake build:**
Network connectivity is required. CMake clones emp-tool, emp-ot, and emp-sh2pc sequentially
from `github.com/emp-toolkit/`. If your machine is behind a proxy, set `https_proxy` before
running `cmake --build`.

**SEAL not found:**
CMake looks in standard system paths and `/opt/homebrew`. If SEAL is installed to a custom
prefix, add `-DCMAKE_PREFIX_PATH=/path/to/seal/prefix` to the cmake configure command.

**OpenMP not found (macOS):**
Apple Clang does not expose OpenMP via `find_package`. CMake falls back to Homebrew libomp
if installed at `/opt/homebrew/opt/libomp`. Install with `brew install libomp`. Without
OpenMP the protocol runs single-threaded (valid, slower).

**test_param_loader fails ("file not found"):**
This test reads `parameters/demo_1k.json` relative to the repo root. Run `ctest` from
within `build/` (which sets `WORKING_DIRECTORY` to the source tree root via CMakeLists.txt).

**`libemp-tool.dylib: image not found` (macOS):**
Set `export DYLD_LIBRARY_PATH=build/emp_install/lib` before invoking any binary directly.
