# unit

Per-component ctest unit tests.

- `test_shamir.cc`, `test_protocol_arithmetic.cc`, `test_multi_token.cc`,
  `test_multi_token_arithmetic.cc` -- secret sharing, k=2 reconstruction, and
  multi-token-round FAR mitigation (verified pairs / label recovery).
- `test_partition_completeness.cc` -- DB partition coverage across splits.
- `test_aes_reduction.cc`, `test_gc_roundtrip.cc` -- AES field reduction and the
  GC-vs-offline blinding invariant.
- `test_flpsi_wrapper.cc` -- LSH encode / subsample / `loadDataset`.
- `test_serialization.cc`, `test_network_framing.cc` -- SEAL serialization and
  message framing. `test_param_loader.cc`, `test_disable_1gc.cc` -- JSON config
  loading and the `ProtocolMode` enum.
