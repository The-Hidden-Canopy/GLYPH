# Synthetic optical simulator

Status: implemented as a deterministic source/unit impairment boundary.

## Delivered

- Seeded SplitMix64 random stream with reproducible capture output.
- Per-channel bounded additive noise with clamping.
- Pixel dropout with explicit percentage control.
- Channel quantization from 1 through 8 bits.
- Frame and hold identity preservation.
- Machine-readable capture receipt containing seed and processed/drop/noise
  counts.
- Transactional output and source-shape/configuration validation.

## Deliberate boundary

This is a first synthetic impairment slice, not a camera or ISP model. It does
not implement geometry distortion, resampling, rolling shutter, blur, exposure,
autofocus, JPEG artifacts, lens distortion, or physical display behavior. A
successful simulation run is S1 synthetic evidence only and cannot promote a
claim to desktop loopback, mobile, or conformance evidence.

The simulator does not write files, open images, access a network, or execute
received content. PNG encoding/decoding remains an explicit in-memory artifact
boundary around it.

## Evidence

Evidence is source/unit. Tests prove identity behavior, deterministic replay,
seed variation, complete dropout, quantization/noise receipt accounting, and
invalid-source/configuration rejection.
