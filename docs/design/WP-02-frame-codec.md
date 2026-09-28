# WP-02: frame and tile codec

Status: implemented as a CPU/source-unit slice.

## Delivered

- Network-byte-order encoding and decoding for the 64-byte `GLY1` frame control
  header.
- Network-byte-order encoding and decoding for the 12-byte tile miniheader.
- CRC32C calculation and frame-header integrity verification.
- Checked bounds for payload size, cell pitch, tile-grid multiplication,
  unsupported major version, reserved fields, and zero transfer IDs.
- Deterministic rectangular block interleaving/deinterleaving with bounded
  payload length and safe in-place operation.
- A bounded tile-payload reference codec that records logical payload length
  and CRC, zero-pads to the configured inner-code data size, applies inner ECC,
  and then interleaves the fixed-size wire codeword.
- A profile-neutral frame scheduler that expands logical sequence numbers into
  bounded (16M-emission maximum) physical hold emissions, plus a receive-side
  sequence window that
  suppresses duplicates and stale frames without requiring receipt order to
  match transmission order.
- Deterministic frame, tile, and interleaver vectors plus corruption and
  tile-payload composition and malformed-input tests.

## Deliberate boundary

The specification names frame and profile fields but does not yet assign a
public numeric registry for `frame_type` or `profile_id`. The codec therefore
preserves those fields as raw bytes and does not invent semantic registries.
The interleaver is a CPU reference permutation; it is not a camera/display
layout and does not select a public profile ID. The scheduler only plans
logical sequence repetition; surface rendering and profile selection remain
separate boundaries.
The tile-payload codec is a fixed-profile reference composition; its zero
padding and inner-RS parameters must be frozen by a future interoperable
profile. Inner byte ECC and outer shard FEC are delivered in WP-03.

## Evidence

Evidence is source/unit: CRC32C known vector, exact header bytes, round-trip
decode, corruption rejection, bad magic, reserved-field rejection, resource
limits, zero-ID rejection, an exact interleaver vector with in-place and
boundary tests, tile-payload CRC/ECC composition and failure-preservation
tests, and scheduler hold/duplicate/window-boundary tests. No display, camera,
timing, or optical-rate claim follows from this codec.
