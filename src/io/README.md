# io

Data loading, configuration, and measurement utilities.

- `csv.{h,cc}` -- `CSVDataLoader`: reads enrollment/query integer-set CSVs,
  builds Shamir token/id share maps, and writes result CSVs.
- `param_loader.{h,cc}` -- `ParamConfig`: loads a JSON parameter file, applies
  it to the global protocol parameters, and initializes the SEAL context.
- `instrumentation.{h,cc}` -- timers, RSS sampling, byte counters, and JSONL
  output for fine-grained per-step profiling.
- `io.h` -- declares the global `token_share_map` / `id_share_map` externs.
