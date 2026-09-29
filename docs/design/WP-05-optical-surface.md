# WP-05: deterministic MP0 optical surface

Status: implemented as a bounded CPU/source-unit rendering primitive.

## Delivered

- Cell-aligned MP0 geometry with explicit safe area, orientation anchors,
  control band, pilot columns, calibration band, and payload field.
- Landscape, portrait, and square orientation validation without a platform or
  camera dependency.
- A fixed four-symbol RGB4 control encoding for the existing 64-byte frame
  header: four 2-bit cells per header byte, using black/red/green/blue, with
  at least two complete spatial repetitions in the control band.
- Asymmetric high-contrast corner anchors with per-corner orientation colors.
- Repeating pilot cells and persistent black/red/green/blue/white calibration
  patches on every rendered logical frame.
- Placement of already-encoded RGB8 payload cells from the existing tile codec,
  with strict binary-cell validation and bounded surface allocation.
- Transactional output: a failed layout, header, payload, or allocation check
  leaves the caller's existing `OpticalSurface` unchanged.

## Deliberate boundary

The renderer is a deterministic software surface, not a display driver,
camera receiver, homography solver, PNG encoder, Android binding, or optical
conformance implementation. Its pixels are source/unit and synthetic
rendering evidence only. The control geometry is intentionally isolated behind
`render_mp0_surface` so a future stable C ABI or decoder can consume the same
layout contract without making the current UI or core depend on a platform.

The surface budget is capped at 16 MiB of pixels. The four-corner anchors,
control encoding, calibration patches, and payload field are all kept outside
the payload byte stream; no received content is opened or executed.

## Evidence

Evidence is source/unit. Tests cover deterministic repeatability, control-band
capacity, orientation rejection, undersized geometry, malformed RGB8 cells,
header identity mismatch, zero transfer identity, payload capacity, and
transactional failure behavior. These tests do not establish camera recovery,
color fidelity, timing stability, throughput, or published conformance.
