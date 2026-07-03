Infrastructure requirements
===========================

HARDWARE
  - x86-64 or Apple-silicon machine, multi-core (the benchmarks sweep 1/4/8
    threads). A 4-core machine is enough to build and run the demo and the
    default reproducibility run.
  - Memory scales with database size D:
      D <= 100K : ~8-16 GB is comfortable.
      D = 1M    : ~36 GB recommended (the 1M cells are off by default).
  - ~5 GB free disk for the build (including the EMP-toolkit build) and
    generated benchmark data.
  - Network access for the FIRST build only: CMake downloads and builds
    EMP-toolkit. No network is needed afterwards; the protocol runs over the
    loopback interface.

OPERATING SYSTEM
  - macOS 13+ (Apple clang) with Homebrew, or
  - Ubuntu 22.04 LTS / Debian (GCC 11+ or Clang 14+).
  Other Linux distributions work if the dependencies below are installed
  manually.

TOOLCHAIN / DEPENDENCIES (all installed by install.sh)
  - CMake >= 3.15, a C++17 compiler.
  - Microsoft SEAL 4.x         (Homebrew package on macOS; built from source on Ubuntu)
  - FLINT, GMP, MPFR           (polynomial / multiprecision arithmetic)
  - ZeroMQ + cppzmq            (sender/receiver transport)
  - nlohmann/json              (parameter files)
  - OpenMP (libomp / libgomp)  (threading)
  - OpenSSL                    (AES)
  - GoogleTest                 (unit tests; optional)
  - EMP-toolkit                (emp-tool 0.2.5, emp-ot 0.2.4, emp-sh2pc 0.2.2;
                                fetched + built automatically by CMake)

RUNTIME EXPECTATIONS
  - First build: ~10-15 minutes (dominated by EMP-toolkit); subsequent builds
    are seconds.
  - Test suite (ctest): a few seconds.
  - Demo (small synthetic data): under a minute.
  - Reproducibility run (defaults, 2+2 queries, D up to 100K, threads 1/4/8):
    on the order of tens of minutes; the 1M rows (FULL=1) add substantially more
    because of the database setup cost.

RUNNING OUTSIDE THE CURRENT ENVIRONMENT
  The artifact has no dependence on author-specific infrastructure. It builds
  and runs on a stock macOS or Ubuntu machine via `bash install.sh`, and on
  public research infrastructure (e.g., a CloudLab, Chameleon, or FABRIC Ubuntu
  node) with the same one command.
