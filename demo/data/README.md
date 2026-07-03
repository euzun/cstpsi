# data

Generated input sets for the `demo/` protocol demos, in the CSTPSI integer-set
format. These files are produced on demand by `experiments/datagen.py` (the demo
scripts call it automatically) and are not committed.

- `enr_<size>_lbl<label>.csv` -- enrollment: `id, item_0..item_{N-1}, lbl_0..lbl_{k-1}`.
- `qry_<size>.csv` -- query set: `id, item_0..item_{N-1}, is_tp`.
- `expected_<size>_lbl<label>.csv` -- true-positive ground-truth labels.

Regenerate manually with, e.g.:

```bash
python3 ../../experiments/datagen.py --sizes 1k --labels 23bit --tp 5 --tn 5 --output-dir .
```
