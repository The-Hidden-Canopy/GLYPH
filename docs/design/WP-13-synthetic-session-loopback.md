# Synthetic session transport loopback

Status: implemented as a deterministic multi-block source/unit integration.

## Delivered

- Sender session manifest and sequential multi-block emission.
- Four-data/three-parity shard construction for each logical block.
- Reordered shard transport through the MP0 frame codec and canonical PNG
  round-trip.
- Strict saved-frame decode, inner tile verification, and outer-FEC recovery.
- Receiver session block acceptance, durable journal checkpoints, exact
  whole-object verification, and atomic final promotion.
- A short final block exercises deterministic FEC padding removal.

## Deliberate boundary

This test is synthetic software evidence. It does not establish camera
acquisition, perspective recovery, display timing, rolling-shutter behavior,
mobile APIs, throughput, or physical conformance. It does establish that the
current logical session and synthetic optical/FEC seams can compose across
multiple blocks without bypassing the receiver's final hash gate.

## Evidence

`glyph.synthetic_session.unit` is source/unit plus deterministic PNG loopback
evidence. It uses no network, account, cloud, or external runtime dependency.
