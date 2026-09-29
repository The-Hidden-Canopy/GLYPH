# MP0 optical frame loopback

Status: implemented as a strict software integration seam.

## Delivered

- One-tile MP0 frame composition from a logical payload.
- Explicit RGB8 encoding of the 12-byte tile miniheader followed by the
  existing inner Reed-Solomon/interleaver wire cells.
- Frame-header identity, block, symbol-group, payload-length, profile, and
  cell-pitch binding.
- Canonical MP0 surface rendering and saved-PNG round-trip decoding.
- Tile-header parsing, inner ECC, interleaving reversal, and tile CRC
  verification before returning logical bytes.
- Transactional decode outputs and strict rejection of wrong geometry,
  erased payload cells, malformed control, and tile-length disagreement.

## Deliberate boundary

The tile miniheader placement in this work package is a documented software
loopback seam for the current MP0 renderer; it is not a claim that the final
multi-tile optical wire profile is frozen. The codec supports one logical tile
per saved surface and does not implement perspective recovery, camera
classification, rolling-shutter handling, outer FEC reconstruction, timing
negotiation, or a physical display/camera path.

The loopback is source/unit and deterministic PNG evidence. It does not promote
an object, open received content, or establish mobile or conformance evidence.

## Evidence

The unit test covers logical payload recovery, frame/tile identity, PNG
round-trip recovery, control corruption, and geometry mismatch while preserving
prior outputs on failure.
