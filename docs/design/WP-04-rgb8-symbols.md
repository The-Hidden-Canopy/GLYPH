# WP-04: RGB8 primary-channel symbols

Status: implemented as a CPU/source-unit physical-layer primitive.

## Delivered

- Three independent binary primary-channel bits per RGB8 cell.
- Deterministic MSB-first payload bit packing into 3-bit symbols.
- Strict decode of binary `0`/`255` channel cells with zero-padding checks.
- Calibration-relative channel classification with confidence values.
- Low-confidence or non-finite observations represented as erasures rather than
  forced symbols.
- Payload-size bounds and tests for malformed cells, padding, ambiguity, and
  invalid calibration.

## Deliberate boundary

This is not yet a display lattice, anchor detector, homography solver, camera
backend, or optical impairment simulator. It does not define the unresolved
numeric frame/profile registries, and it does not silently turn an erasure into
data for FEC.

## Evidence

Evidence is source/unit. RGB8 round-trip here proves only the logical symbol
mapping; it does not establish color fidelity, camera robustness, bitrate, or
hardware conformance.

