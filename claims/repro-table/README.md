# repro-table

Reproduces the paper's head-to-head CSTPSI-vs-STLPSI performance table.

- `claim.txt` -- the claim, metric definitions, and what must match vs. may differ.
- `run.sh` -- one command: generate benchmark databases, run every cell
  (D x label x threads x {STLPSI, CSTPSI}), collect, and render the table
  (text + LaTeX). Grid and query counts are overridable via env vars.
- `render_table.py` -- formats `summarize.py` output into the paper's table.
- `expected/` -- the paper-reported values for cell-by-cell comparison.
