# protocol

Core of the CSTPSI two-party set-threshold labeled PSI.

- `core.cc` -- share-map setup and the single-query driver; holds the global
  `token_share_map` / `id_share_map`.
- `sender.{h,cc}` -- DB partitioning, Shamir-coefficient computation, SIMD
  packing, BFV encoding, and homomorphic polynomial evaluation.
- `receiver.{h,cc}` -- query powers, encrypt/decrypt, k=2 Lagrange
  reconstruction, and multi-token-round verified-pair / label recovery.
- `plain.{h,cc}` -- plaintext mirror of the poly-eval (used for FAR/FRR study).
- `modpoly.{c,h}` -- Shamir secret sharing and Lagrange interpolation over GF(p).
- `params.{h,cc}` -- global parameters, type aliases, `SealCredentials`, the
  CSTPSI/STLPSI `ProtocolMode` enum; `helper.{h,cc}` -- shared vector/field utils.
