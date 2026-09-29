# GLYPH

Open Optical Data Transport Protocol — reference implementation foundation.

GLYPH transports arbitrary binary objects over visible display pixels and
accepts a transfer as complete only when the reconstructed bytes match the
manifest length and SHA-256 digest. The protocol is designed for offline,
account-free, locally verifiable operation.

## Current status

This repository is an early, buildable implementation slice of the v0.1
engineering specification. It is not yet a conforming optical implementation.
The current CPU-portable core provides:

- streaming SHA-256 and independently checked digest vectors;
- bounded source-file hashing and exact object verification;
- a deterministic core-manifest CBOR encoder with fixed v0.1 field ordering;
- temporary-file output with hash/length verification before atomic promotion;
- identity-bound append-only resume journaling for shard receipts and verified
  block ranges;
- the 64-byte frame control header and 12-byte tile miniheader codecs;
- CRC32C framing checks with corruption and resource-boundary tests;
- deterministic bounded payload interleaving and deinterleaving;
- bounded tile-payload composition across CRC, inner ECC, and interleaving;
- profile-neutral frame hold scheduling and bounded duplicate suppression;
- logical tile-to-RGB8 cell composition with strict binary-cell decoding;
- CPU 3x3 RGB channel-mixing calibration fitting with singular-input rejection;
- bounded systematic GF(256) erasure recovery for the M0 shard model;
- shortened GF(256) inner byte ECC with bounded correction and erasure recovery;
- RGB8 primary-channel symbol packing with confidence-to-erasure classification.
- deterministic MP0 software surfaces with safe geometry, control/header cells,
  anchors, pilots, calibration patches, and bounded payload placement;
- a versioned C ABI slice with opaque surface-renderer handles and
  caller-owned output buffers plus logical sender/receiver session handles;
- a dependency-free canonical RGBA8 PNG encoder/decoder for saved-frame
  processing;
- a seeded synthetic capture impairment slice with replay receipts;
- a logical sender/receiver session state machine with atomic final
  verification and identity-bound journaling;
- a bounded local-file sender path that hashes the source before manifest
  emission, re-hashes emitted bytes before final repeat, and rejects changed
  bytes;
- a native durable checkpoint before resumable receiver receipts are persisted;
- a strict one-tile MP0 frame loopback joining tile ECC, RGB8 cells,
  canonical surfaces, and saved-PNG recovery;
- a bounded outer-FEC group assembler with reorder, duplicate, and
  conflicting-shard handling;
- a deterministic synthetic loopback composing FEC, MP0, PNG, tile recovery,
  and exact block reconstruction;
- a multi-block synthetic session loopback that drives recovered blocks through
  durable journaling and atomic whole-object promotion;

The manifest encoder/decoder currently uses `encryption = null` and an empty
`extensions` map. Secure-profile fields, optical layout, calibration
acquisition/validation, camera/display backends, GPU paths, and conformance
hardware fixtures remain explicit work packages.

The authoritative draft is [the open engineering specification](docs/GLYPH_Open_Engineering_Specification_v0.1.md).

## Build and test

The foundation has no network or third-party runtime dependency:

```powershell
cmake -S . -B build -DGLYPH_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

For memory and undefined-behavior checks, configure a separate build with
`-DGLYPH_ENABLE_SANITIZERS=ON` and run the same CTest command.

The tested behavior is source/unit evidence only. It does not establish
optical, GPU, camera, media, throughput, or 100 MiB conformance claims.

## Repository shape

- `include/glyph/` — public C++ API surface; future stable C ABI belongs under
  the same public boundary.
- `src/` — implementation, kept independent of Hidden Canopy donor projects.
- `docs/` — specification, protocol notes, and append-only decisions.
- `tests/` — unit/property/fuzz/conformance layers as they are implemented.
- `vectors/` — deterministic manifests, frames, FEC, and crypto vectors.
- `benchmarks/` — reproducible receipts, not unqualified performance claims.
- `ui/mobile/` — dependency-free mobile-first interaction preview; synthetic
  walkthroughs are explicitly not optical or conformance evidence.

The first UI slice can be inspected locally with:

```powershell
python -m http.server 4173 --directory ui/mobile
```

Its contract checks run independently of the CMake build:

```powershell
node --test ui/mobile/contract.test.mjs
```

The current work-package decisions are documented in
[WP-01 object layer](docs/design/WP-01-object-layer.md) and
[WP-02 frame codec](docs/design/WP-02-frame-codec.md), plus
[WP-03 inner/outer bounded FEC](docs/design/WP-03-fec.md), and
[WP-04 RGB8 symbols](docs/design/WP-04-rgb8-symbols.md),
[WP-05 deterministic optical surface](docs/design/WP-05-optical-surface.md),
and the [C ABI surface and session boundary](docs/design/C-ABI-surface.md).
[The canonical PNG boundary](docs/design/WP-06-png.md) documents the
saved-frame image contract.
[The synthetic simulator](docs/design/WP-07-simulator.md) documents the
current S1 evidence boundary.
[The logical session](docs/design/WP-09-session.md) documents the current
offline block-orchestration boundary.
[The synthetic session loopback](docs/design/WP-13-synthetic-session-loopback.md)
documents the multi-block composition boundary.

## Non-negotiable completion rule

`COMPLETE` means exact object length plus SHA-256 equality. A visually correct
surface, successfully parsed media file, finite loss-recovery run, or lower
level test cannot substitute for whole-object verification.

## License

The repository is intended to use Apache-2.0 with SPDX-tracked dependencies.
See [LICENSE](LICENSE).
The project follows the
[Open Canopy Contract](OPEN_CANOPY_CONTRACT.md).
