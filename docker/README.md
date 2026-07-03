# docker

Docker support for the artifact image.

- `entrypoint.sh` -- command dispatcher for `docker run cstpsi <cmd>`:
  `verify` (ctest), `repro` (head-to-head performance table), `smoke` (fast
  structural run), `demo` (single-container two-party network demo), plus
  `demo-sender` / `demo-receiver` for the two-container `docker compose` demo
  and an interactive `shell`. The Dockerfile/compose files live at the repo root.
