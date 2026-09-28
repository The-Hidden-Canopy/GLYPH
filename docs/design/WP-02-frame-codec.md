# WP-02: frame and tile codec

Status: implemented as a CPU/source-unit slice.

## Delivered

- Network-byte-order encoding and decoding for the 64-byte `GLY1` frame control
  header.
- Network-byte-order encoding and decoding for the 12-byte tile miniheader.
- CRC32C calculation and frame-header integrity verification.
- Checked bounds for payload size, cell pitch, tile-grid multiplication,
  unsupported major version, reserved fields, and zero transfer IDs.
- Deterministic frame and tile vectors plus corruption and malformed-input tests.

## Deliberate boundary

The specification names frame and profile fields but does not yet assign a
public numeric registry for `frame_type` or `profile_id`. The codec therefore
preserves those fields as raw bytes and does not invent semantic registries.
Payload interleaving, optical coding, inner ECC, outer FEC, and frame scheduling
remain separate work packages.

## Evidence

Evidence is source/unit: CRC32C known vector, exact header bytes, round-trip
decode, corruption rejection, bad magic, reserved-field rejection, resource
limits, and zero-ID rejection. No display, camera, timing, or optical-rate
claim follows from this codec.

