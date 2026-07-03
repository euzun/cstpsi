# src

The CSTPSI C++ library. Compiled into the binaries under `app/`.

Subdirectories:
- `protocol/` -- core two-party PSI: Shamir sharing, partitioning, BFV
  polynomial evaluation, and threshold reconstruction.
- `gc/` -- AES-based garbled-circuit OPRF (EMP) plus offline AES fallback.
- `io/` -- CSV loaders, JSON parameter loading, and timing/RSS instrumentation.
- `network/` -- ZeroMQ transport and SEAL ciphertext serialization.
- `flpsi/` -- LSH-encoding wrapper that turns embeddings into integer subsamples.
