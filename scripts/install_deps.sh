#!/usr/bin/env bash
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
# install_deps.sh — Install CSTPSI system dependencies
#
# Supported platforms:
#   macOS  (Homebrew)
#   Ubuntu / Debian  (apt-get)
#
# Usage:
#   bash scripts/install_deps.sh

set -euo pipefail

log() { echo "[install_deps] $*"; }

# ──────────────────────────────────────────────────────────────
# Detect platform
# ──────────────────────────────────────────────────────────────
if [[ "$(uname)" == "Darwin" ]]; then
    PLATFORM="macos"
elif [[ -f /etc/debian_version ]]; then
    PLATFORM="debian"
else
    echo "Unsupported platform: $(uname). Install dependencies manually."
    echo "Required: SEAL 4.x, FLINT, GMP, MPFR, ZeroMQ, cppzmq, nlohmann-json, Boost (headers), libuuid, OpenMP, CMake 3.15+"
    exit 1
fi

log "Detected platform: $PLATFORM"

# ──────────────────────────────────────────────────────────────
# macOS (Homebrew)
# ──────────────────────────────────────────────────────────────
if [[ "$PLATFORM" == "macos" ]]; then
    if ! command -v brew &>/dev/null; then
        echo "Homebrew not found. Install it first: https://brew.sh"
        exit 1
    fi

    log "Updating Homebrew..."
    brew update

    log "Installing dependencies..."
    brew install \
        cmake \
        seal \
        flint \
        gmp \
        mpfr \
        zeromq \
        cppzmq \
        nlohmann-json \
        boost \
        libomp \
        openssl@3 \
        googletest
    # (libuuid ships with the macOS SDK -- no formula needed.)

    log "All macOS dependencies installed."
    log ""
    log "Build with:"
    log "  cmake -S . -B build && cmake --build build -j\$(sysctl -n hw.ncpu)"
fi

# ──────────────────────────────────────────────────────────────
# Ubuntu / Debian (apt-get)
# ──────────────────────────────────────────────────────────────
if [[ "$PLATFORM" == "debian" ]]; then
    log "Updating apt package list..."
    sudo apt-get update -y

    log "Installing system packages..."
    # cppzmq is NOT packaged on Ubuntu < 23.04 (vendored below).
    sudo apt-get install -y \
        cmake \
        build-essential \
        git \
        libflint-dev \
        libgmp-dev \
        libmpfr-dev \
        libzmq3-dev \
        nlohmann-json3-dev \
        libboost-dev \
        uuid-dev \
        libssl-dev \
        libgomp1 \
        libgtest-dev

    # cppzmq header (zmq.hpp): header-only, not packaged before Ubuntu 23.04.
    if ! find /usr/include /usr/local/include -name zmq.hpp 2>/dev/null | grep -q .; then
        log "cppzmq header not found; vendoring from upstream..."
        CPPZMQ_SRC="$(mktemp -d)"
        git clone --depth 1 --branch v4.10.0 https://github.com/zeromq/cppzmq.git "$CPPZMQ_SRC"
        sudo cp "$CPPZMQ_SRC/zmq.hpp" "$CPPZMQ_SRC/zmq_addon.hpp" /usr/local/include/
        rm -rf "$CPPZMQ_SRC"
    fi

    # Microsoft SEAL is not in the standard Ubuntu repos;
    # build from source or install via vcpkg.
    if ! pkg-config --exists seal 2>/dev/null && ! find /usr /opt -name "SEALConfig.cmake" 2>/dev/null | grep -q .; then
        log ""
        log "Microsoft SEAL not found. Install from source:"
        log "  git clone https://github.com/microsoft/SEAL.git /tmp/SEAL"
        log "  cd /tmp/SEAL && cmake -S . -B build -DSEAL_BUILD_TESTS=OFF && cmake --build build -j\$(nproc)"
        log "  sudo cmake --install build"
        log ""
    else
        log "Microsoft SEAL found."
    fi

    log "All Ubuntu/Debian dependencies installed."
    log ""
    log "Build with:"
    log "  cmake -S . -B build && cmake --build build -j\$(nproc)"
fi
