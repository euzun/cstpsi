# flpsi

Front-end that turns biometric embeddings into the integer subsample sets the
protocol matches on. Used by `app/flpsi_experiment.cc` and the unit tests.

- `flpsi_wrapper.{h,cc}` -- `lshEncode` (L=256 sign-of-dot-product bits),
  `robustTemplate` (majority vote), `makeSubsampleMasks` / `subsample`
  (N_SUB=64 field elements mod F), and `loadDataset` for the unified FPB1
  EMBED/BITS container plus the legacy LFWE reader.
