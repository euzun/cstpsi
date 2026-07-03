# app

Runnable binaries built on top of the `src/` library.

- `sender.cc` / `receiver.cc` -- the two-party network server and client (BFV
  homomorphic poly-eval over ZeroMQ, with the EMP garbled-circuit OPRF channel).
- `cli.cc` -- single-process driver with `bench` (synthetic FRR/FAR check) and
  `csv_run` (full PSI run from CSV integer-set files) modes.
- `flpsi_experiment.cc` -- RSE (soundness) and FRR harness over real LFW
  embeddings, comparing the k-of-N, STLPSI, and CSTPSI matching kernels across
  5 rungs.
