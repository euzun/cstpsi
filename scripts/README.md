# Scripts

Project-wide helper scripts:

## `install_deps.sh`

Installs the build-time dependencies (SEAL, FLINT + GMP + MPFR, EMP-toolkit, ZeroMQ + cppzmq, nlohmann-json) into the local tree under `build/`. Pinned to versions known to work with the current CMake config; in particular EMP-toolkit is held at `0.2.5 / 0.2.4 / 0.2.2` because the `main` branch drifted on `bytes_recv` / `bytes_sent`.

Usage:

```bash
./scripts/install_deps.sh
```

Re-run is idempotent: the script skips any dependency it finds already built. To force a clean rebuild, `rm -rf build/` first.

## `validate_jsonl.py`

Validates a JSONL file emitted by `cstpsi_cli bench --output-jsonl <file>`. Checks the JSON shape, the per-step timing invariants (sums match wall times within the documented tolerance), and the per-query / per-session record layout described in `docs/INSTRUMENTATION.md`. Exits non-zero on any violation.

Usage:

```bash
python3 scripts/validate_jsonl.py /tmp/run.jsonl
```

No external Python dependencies; uses only the standard library.
