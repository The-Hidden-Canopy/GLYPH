# Canonical saved-frame decoder

Status: implemented as a strict axis-aligned source/unit decoder.

## Delivered

- Exact corner-anchor validation using the shared MP0 pattern.
- Verification of both repeated RGB4 control-header copies.
- Existing frame-header CRC/protocol validation and profile/pitch/sequence
  identity checks.
- Exact black/red/green/blue/white calibration-patch validation.
- Extraction of the canonical payload RGB8 cell field.
- In-memory PNG-to-surface-to-decoder loopback coverage.
- Transactional result behavior on anchor, repetition, and header failures.

## Deliberate boundary

This decoder accepts only the canonical, axis-aligned software surface. It does
not estimate anchors from arbitrary camera pixels, solve a homography, model
rolling shutter, classify noisy observations, or infer payload length beyond
the configured logical field. Ambiguous or malformed control data is rejected;
future tolerant receiver work must preserve erasures rather than guess bytes.

The PNG loopback is saved-frame synthetic evidence. It is not desktop webcam,
mobile, physical, or conformance evidence.

## Evidence

Evidence is source/unit and deterministic PNG loopback. The decoder does not
promote any object, write storage, open content, or declare transfer
completion; final completion remains the manifest length plus SHA-256 gate.
