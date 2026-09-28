# WP-04: RGB8 primary-channel symbols

Status: implemented as a CPU/source-unit physical-layer primitive.

## Delivered

- Three independent binary primary-channel bits per RGB8 cell.
- Deterministic MSB-first payload bit packing into 3-bit symbols.
- Strict decode of binary `0`/`255` channel cells with zero-padding checks.
- Calibration-relative channel classification with confidence values.
- CPU fitting of a 3x3 channel-mixing inverse from black and one-hot red,
  green, and blue calibration observations, with singular/non-finite input
  rejection, relative conditioning checks, and an inverse-gain bound.
- Low-confidence or non-finite observations represented as erasures rather than
  forced symbols.
- Classified-decision decoding that reports every logical byte touched by an
  erased cell, plus propagation of those byte erasures into tile recovery.
- A strict logical tile-to-RGB8 composition path that applies tile CRC/ECC and
  interleaving before cell packing, and reverses those steps on decode.
- Payload-size bounds and tests for malformed cells, padding, ambiguity, and
  invalid calibration, plus integrated tile-path failure tests.

## Deliberate boundary

This is not yet a display lattice, anchor detector, homography solver, camera
backend, or optical impairment simulator. The strict-cell path assumes binary
cells; the classified-decision path preserves erased cells by marking every
logical byte they touch and routing those bytes through inner erasure recovery.
The calibration fitter consumes caller-provided observations; it does not
acquire camera patches, define a display/camera color model, or select a
protocol profile. It does not define the unresolved numeric frame/profile
registries.

## Evidence

Evidence is source/unit. RGB8 round-trip, classified-erasure, and integrated
tile-path tests prove only logical symbol mapping and CPU composition; they do
not establish color fidelity, camera robustness, bitrate, or hardware
conformance.
