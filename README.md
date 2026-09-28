# GLYPH

Open Optical Data Transport Protocol — reference implementation foundation.

GLYPH transports arbitrary binary objects over visible display pixels and
accepts a transfer as complete only when the reconstructed bytes match the
manifest length and SHA-256 digest. The protocol is designed for offline,
account-free, locally verifiable operation.

## Current status

This repository is the initial buildable foundation for the v0.1 engineering
specification. It is not yet a conforming optical implementation. The current
code provides only a small, CPU-portable core status/version seam and a
build/test boundary. Optical framing, manifest canonicalization, FEC,
calibration, camera/display backends, crypto, and conformance vectors remain
explicit work packages.

The authoritative draft is [the open engineering specification](docs/GLYPH_Open_Engineering_Specification_v0.1.md).

## Build and test

The foundation has no network or third-party runtime dependency:

```powershell
cmake -S . -B build -DGLYPH_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

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

## Non-negotiable completion rule

`COMPLETE` means exact object length plus SHA-256 equality. A visually correct
surface, successfully parsed media file, finite loss-recovery run, or lower
level test cannot substitute for whole-object verification.

## License

The repository is intended to use Apache-2.0 with SPDX-tracked dependencies.
See [LICENSE](LICENSE).

