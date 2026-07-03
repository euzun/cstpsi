# Tests

GoogleTest unit and integration tests for the CSTPSI implementation. Tests are wired into CMake; build them as part of the main `cmake --build build` step (no extra target needed) and run via `ctest` or by executing the individual test binaries under `build/tests/`.

## Layout

```
tests/
├── unit/           per-module tests
└── integration/    end-to-end tests against the CLI
```

## Unit tests

| File | Covers |
|------|--------|
| `unit/test_serialization.cc` | Ciphertext / public-material round-trips through the SEAL serializer (built only when GoogleTest is present) |
| `unit/test_network_framing.cc` | SEAL ciphertext serialization round-trips via the public vector / CVector2D APIs (uint32 LE length prefixes) |
| `unit/test_aes_reduction.cc` | The AES-based PRF reductions used in the receiver pipeline |
| `unit/test_shamir.cc` | Shamir secret-sharing share generation and reconstruction over the 23-bit field |
| `unit/test_gc_roundtrip.cc` | The 1-GC garbled-circuit step end-to-end (sender side <-> receiver side, on synthetic inputs) |
| `unit/test_multi_token.cc` | Multi-token-round (T > 1) amplification logic |
| `unit/test_multi_token_arithmetic.cc` | Multi-token FAR amplification and label recovery (findVerifiedPairs / reconstructLabels across T rounds) |
| `unit/test_protocol_arithmetic.cc` | Core match/reconstruct arithmetic with real Shamir shares (no FHE/network): TP/TN/edge cases |
| `unit/test_partition_completeness.cc` | Regression test for the partition-completeness invariant (every enrolled id placed exactly once) |
| `unit/test_param_loader.cc` | Loading and validating JSON parameter configs |
| `unit/test_disable_1gc.cc` | The `--disable-1gc` code path (FLPSI-orig style ablation row) |
| `unit/test_flpsi_wrapper.cc` | The FLPSI LSH encoding and subsampling wrapper (`src/flpsi/flpsi_wrapper`) |

## Integration tests

| File | Covers |
|------|--------|
| `integration/test_csv.cc` | Full CSV ingest -> protocol -> result CSV round-trip via `cstpsi_cli` |

## Running

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

To run a single test binary directly:

```bash
DYLD_LIBRARY_PATH=build/emp_install/lib \
  ./build/tests/unit/test_shamir
```

The `DYLD_LIBRARY_PATH` is only needed on macOS; Linux picks up the EMP libraries from CMake rpath.
