# C ABI: MP0 surface and logical session

Status: implemented as a versioned, caller-buffered native binding surface.

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
- opaque logical sender and receiver handles;
- bounded local-file sender opening with emitted-byte re-verification before
  final repeat;
- caller-owned manifest/block buffers and explicit state/progress accessors;
- UTF-8 path inputs for receiver storage, explicit pause/reopen/resume, and
  final-path export only after verified promotion.

Receiver block acknowledgement uses the native durable file-checkpoint path
where the host provides one; a plain stream flush is not presented as crash
durability.

The handle stores the validated geometry and the current frame/hold identity.
`glyph_mp0_renderer_set_frame` updates only that local rendering state; it does
not create a transfer, access a camera, open a file, or make a remote call.

## Deliberate boundary

The session ABI consumes caller-provided logical object bytes or a bounded local
source file and already-accepted logical blocks. File sources are hashed before
manifest emission, and the bytes actually emitted are hashed again before
`FINAL_REPEAT`; a changed or truncated source cannot silently alter the
transmitted object. The ABI does
not acquire camera frames, render Android surfaces, perform JNI/Swift binding,
expose shard recovery, or claim mobile/physical conformance. The sender block
call requires a caller buffer at least as large as its configured block size.
Receiver paths are local storage paths; no network, account, or remote service
is involved.

## Evidence

Evidence is source/unit. Tests cover ABI/version/structure validation,
layout export, caller-buffer rendering, integrity rejection, bounded output
capacity, frame-state updates, preservation of output on failure, a complete
C sender/receiver logical transfer, pause/reopen/resume, out-of-order block
rejection, final-path export, and a standalone C compilation of the public
header. The ABI has not been exercised through JNI, Swift, C#, Rust, Python,
Android, camera acquisition, or physical hardware in this slice.
