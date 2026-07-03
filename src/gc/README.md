# gc

Garbled-circuit OPRF that blinds set items with a shared AES key.

- `aes_gc.{h,cc}` -- sender/receiver EMP garbled-circuit roles, the offline
  `blindItemOffline` / `blindDatabaseOffline` AES path, and `gcSimulateLocal`
  (a local fallback used when EMP is not wired in).
- `aes_key.h` -- 16-byte `AesKey` type with generate/save/load helpers and
  `reduceAesOutput` (folds the 128-bit AES block into a nonzero field element).
