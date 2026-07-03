# integration

End-to-end ctest cases exercising library components against real data paths.

- `test_csv.cc` -- `CSVDataLoader` round-trips: enrollment loading (with header
  skip, missing columns, large item values, empty files), query loading with the
  `is_tp` column, and result CSV writing. Defines mock protocol globals so the
  loader links standalone.
