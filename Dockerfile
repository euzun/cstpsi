# syntax=docker/dockerfile:1
#
# CSTPSI -- reproducible build/run environment.
# Multi-stage: a fat builder compiles SEAL + EMP + the project and runs the test
# suite; a slim runtime keeps only the binaries, test executables, EMP's shared
# lib, and the Python experiment harness.
#
#   docker build -t cstpsi .
#   docker run --rm cstpsi verify     # re-run the unit/integration tests (ctest)
#   docker run --rm cstpsi repro      # reproduce the head-to-head performance table
#   docker run --rm cstpsi demo       # single-container two-party network demo
#   docker compose up                 # distributed two-container demo
#
# NOTE ON TIMING: absolute latency under a container/VM is not the claim. The
# reproducible results are the CSTPSI/STLPSI speedup ratio, the communication
# saving, and the zero false-accept (T=2) correctness -- all ratios/flags that
# hold regardless of host. Build native to your CPU arch (no emulation).

############################
# Stage 1: builder
############################
FROM ubuntu:22.04 AS builder
ENV DEBIAN_FRONTEND=noninteractive

# Build toolchain + dependency dev packages. SEAL is built from source below
# (not packaged for Ubuntu); EMP-toolkit is fetched by CMake at configure time.
# nlohmann-json and cppzmq are header-only.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates git cmake build-essential \
        libflint-dev libgmp-dev libmpfr-dev \
        libzmq3-dev nlohmann-json3-dev libboost-dev \
        libssl-dev libgomp1 libgtest-dev uuid-dev \
        python3 \
    && rm -rf /var/lib/apt/lists/*

# cppzmq (C++ binding for ZeroMQ) is header-only and not packaged for Ubuntu
# 22.04, so vendor the headers from upstream into /usr/local/include.
RUN git clone --depth 1 --branch v4.10.0 https://github.com/zeromq/cppzmq.git /tmp/cppzmq \
    && cp /tmp/cppzmq/zmq.hpp /tmp/cppzmq/zmq_addon.hpp /usr/local/include/ \
    && rm -rf /tmp/cppzmq

# Microsoft SEAL 4.1.2 from source (static), installed into /usr/local.
RUN git clone --depth 1 --branch 4.1.2 https://github.com/microsoft/SEAL.git /tmp/SEAL \
    && cmake -S /tmp/SEAL -B /tmp/SEAL/build \
        -DSEAL_BUILD_TESTS=OFF -DSEAL_BUILD_EXAMPLES=OFF -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /tmp/SEAL/build -j"$(nproc)" \
    && cmake --install /tmp/SEAL/build \
    && rm -rf /tmp/SEAL

WORKDIR /opt/cstpsi
COPY . .

# Configure + build (CMake fetches and builds EMP-toolkit during this step).
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)"

# Fail the image build if the test suite does not pass.
RUN cd build && LD_LIBRARY_PATH=/opt/cstpsi/build/emp_install/lib:/usr/local/lib \
        ctest --output-on-failure

# Slim the build tree before it is copied into the runtime stage: drop object
# files, static archives, and EMP's ExternalProject build trees, but keep the
# executables, test binaries, CTest metadata, and emp_install/lib.
RUN find build -type f \( -name '*.o' -o -name '*.a' \) -delete \
    && rm -rf build/emp_tool_build build/emp_ot_build build/emp_sh2pc_build

############################
# Stage 2: runtime
############################
FROM ubuntu:22.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive

# Runtime shared libs + python3 (experiment harness) + cmake (provides ctest).
# SEAL is statically linked into the binaries, so no SEAL runtime lib is needed.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates cmake python3 \
        libflint-dev libgmp10 libmpfr6 libzmq5 libssl3 libgomp1 libuuid1 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /opt/cstpsi /opt/cstpsi

# EMP is a shared lib in a local prefix (no rpath baked in); register it with
# the dynamic loader so every binary and test resolves it without env vars.
RUN echo "/opt/cstpsi/build/emp_install/lib" > /etc/ld.so.conf.d/cstpsi-emp.conf \
    && ldconfig

WORKDIR /opt/cstpsi
ENTRYPOINT ["bash", "docker/entrypoint.sh"]
CMD ["help"]
