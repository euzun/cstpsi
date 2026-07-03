#!/usr/bin/env bash
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
# install.sh -- one-click setup for the CSTPSI artifact.
#
# Installs every system dependency and builds all binaries + tests.
# Supported out of the box:
#   - macOS  (Homebrew)
#   - Ubuntu / Debian  (apt-get; Microsoft SEAL built from source)
#
# Usage:
#   bash install.sh
#
# After it finishes you will have build/cstpsi_{cli,sender,receiver} and the
# test suite. Run `cd build && ctest --output-on-failure` to verify.
#
# EMP-toolkit (emp-tool/emp-ot/emp-sh2pc) is fetched and built automatically by
# CMake on first configure, so an internet connection is required for the first
# build. Everything else is a system package.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
log() { echo "[install] $*"; }
die() { echo "[install] ERROR: $*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 1. System dependencies
# ---------------------------------------------------------------------------
if [[ "$(uname)" == "Darwin" ]]; then
    command -v brew >/dev/null 2>&1 || die "Homebrew not found. Install it from https://brew.sh and re-run."
    log "macOS detected; installing dependencies via Homebrew ..."
    brew update
    # cmake build tool; SEAL/FLINT/GMP/MPFR crypto+math; zeromq+cppzmq transport;
    # nlohmann-json config; boost string algorithms; libomp threading; googletest
    # unit tests; openssl AES. (libuuid ships with the macOS SDK -- no formula.)
    brew install cmake seal flint gmp mpfr zeromq cppzmq nlohmann-json boost libomp googletest openssl@3 || true
    JOBS="$(sysctl -n hw.ncpu)"
elif [[ -f /etc/debian_version ]]; then
    log "Debian/Ubuntu detected; installing dependencies via apt-get ..."
    sudo apt-get update -y
    # cppzmq is NOT packaged on Ubuntu < 23.04 (vendored below); boost (headers)
    # and uuid-dev are required by the CLI and string handling.
    sudo apt-get install -y \
        cmake build-essential git \
        libflint-dev libgmp-dev libmpfr-dev \
        libzmq3-dev nlohmann-json3-dev libboost-dev uuid-dev \
        libssl-dev libgomp1 libgtest-dev
    JOBS="$(nproc)"

    # cppzmq header (zmq.hpp): header-only, not packaged before Ubuntu 23.04.
    # Vendor it from upstream into /usr/local/include if not already present.
    if ! find /usr/include /usr/local/include -name zmq.hpp 2>/dev/null | grep -q .; then
        log "cppzmq header not found; vendoring from upstream ..."
        CPPZMQ_SRC="$(mktemp -d)"
        git clone --depth 1 --branch v4.10.0 https://github.com/zeromq/cppzmq.git "$CPPZMQ_SRC"
        sudo cp "$CPPZMQ_SRC/zmq.hpp" "$CPPZMQ_SRC/zmq_addon.hpp" /usr/local/include/
        rm -rf "$CPPZMQ_SRC"
    fi
    # Microsoft SEAL is not packaged for Ubuntu; build + install it from source
    # (idempotent: skip if a SEAL CMake config is already discoverable).
    if ! find /usr/local /usr /opt -name "SEALConfig.cmake" 2>/dev/null | grep -q .; then
        log "Microsoft SEAL not found; building from source ..."
        SEAL_SRC="$(mktemp -d)"
        git clone --depth 1 --branch 4.1.2 https://github.com/microsoft/SEAL.git "$SEAL_SRC"
        cmake -S "$SEAL_SRC" -B "$SEAL_SRC/build" -DSEAL_BUILD_TESTS=OFF -DSEAL_BUILD_EXAMPLES=OFF -DCMAKE_BUILD_TYPE=Release
        cmake --build "$SEAL_SRC/build" -j"$JOBS"
        sudo cmake --install "$SEAL_SRC/build"
        rm -rf "$SEAL_SRC"
    else
        log "Microsoft SEAL already installed."
    fi
else
    die "Unsupported platform: $(uname). Install manually: CMake>=3.15, SEAL 4.x, FLINT, GMP, MPFR, ZeroMQ+cppzmq, nlohmann-json, Boost (headers), libuuid, OpenMP, OpenSSL, GoogleTest."
fi

# ---------------------------------------------------------------------------
# 2. Configure + build (EMP-toolkit fetched automatically here)
# ---------------------------------------------------------------------------
log "Configuring (first run downloads + builds EMP-toolkit; ~10-15 min) ..."
cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release
log "Building with $JOBS jobs ..."
cmake --build "$ROOT/build" -j"$JOBS"

# ---------------------------------------------------------------------------
# 3. Done
# ---------------------------------------------------------------------------
log "Build complete. Binaries in $ROOT/build : cstpsi_cli, cstpsi_sender, cstpsi_receiver"
log "Verify the build:    cd build && ctest --output-on-failure"
log "Run the demo:        see README.md (two-terminal sender/receiver)"
log "Reproduce the table: bash claims/repro-table/run.sh"
