# C ABI: MP0 surface renderer

Status: implemented as the first versioned, caller-buffered native binding
surface.

## Contract

`include/glyph/c_api.h` exposes a C-only boundary for the deterministic MP0
surface renderer:

- opaque `glyph_mp0_renderer_t` handles;
- fixed-width versioned configuration, layout, and output structures;
- encoded 64-byte frame headers and RGB8 payload cells as explicit buffers;
- caller-owned RGBA output memory;
- status-code returns with no C++ exceptions crossing the boundary;
- transactional output: failed render, malformed header, or insufficient
  output capacity does not modify the caller's output buffer or result fields.

The handle stores the validated geometry and the current frame/hold identity.
`glyph_mp0_renderer_set_frame` updates only that local rendering state; it does
not create a transfer, access a camera, open a file, or make a remote call.

## Deliberate boundary

This is not yet the sender/receiver session ABI described by the mobile
document. No object open, camera submission, progress journal, finalization,
Android binding, or local-storage promotion API is exposed until those state
and ownership contracts are implemented. The C boundary is therefore a
surface-rendering milestone, not mobile integration or protocol conformance.

## Evidence

Evidence is source/unit. Tests cover ABI/version/structure validation,
layout export, caller-buffer rendering, integrity rejection, bounded output
capacity, frame-state updates, preservation of output on failure, and a
standalone C compilation of the public header. The ABI has not been exercised
through JNI, Swift, C#, Rust, Python, Android, or physical hardware in this
slice.
