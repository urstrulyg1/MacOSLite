# benchmarks/

Measurement lives in three CLIs (spec §40, §53), all built by the Makefile:

- `maclite-ui-benchmark` — composite cost per frame class, damage-only cost,
  animation transform cost, and vsync-pacing jitter. Synthetic mode bounds the
  software path; `--live` reports a running session's real frame statistics.
- `maclite-performance` — one-screen CPU/RAM/GPU/compositor/IO/net view.
- `maclite-memory` — per-component RSS+PSS, nothing hidden.

Rules we follow (spec §8): never benchmark the benchmark; measure the whole
shell (panel + dock + desktop + compositor), not a bare canvas; report
QEMU/sandbox results as such and keep real-iMac numbers separate in
docs/PERFORMANCE.md and docs/TESTING.md.
