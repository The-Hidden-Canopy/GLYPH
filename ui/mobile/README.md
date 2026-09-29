# GLYPH mobile UI preview

This is a dependency-free, local-only interaction prototype for the mobile-first
GLYPH surface described by `GLYPH_Mobile_First_Engineering_Document_v0.2.md`.

It currently provides:

- Send and Receive mobile flows;
- local SHA-256 hashing for preview files up to 16 MiB;
- explicit synthetic sender and receiver walkthroughs;
- a camera-permission surface that keeps frames in the browser;
- truthful unavailable and decoder-not-attached states;
- transfer history language for resume and integrity gating;
- MP0/MP1 profile selection and accessibility controls.

The synthetic walkthrough is not a protocol implementation. It deliberately
stops before `COMPLETE` because it has no source/destination hash equality and
cannot promote an object. The browser camera preview is also not decoder or
physical-conformance evidence.

## Run locally

The page can be opened directly for layout inspection. To enable browser APIs
such as camera permission and `crypto.subtle`, serve this directory from a
local development server:

```powershell
python -m http.server 4173 --directory ui/mobile
```

Then open `http://127.0.0.1:4173/`. The page has no fetch, WebSocket, account,
telemetry, or remote-service path.

## Boundary

This prototype is intentionally outside `libglyph`. The native protocol core,
stable C ABI, canonical MP0 surface renderer, and camera decoder remain the
authoritative implementation layers. A future mobile shell should consume
those contracts rather than duplicate hashing, FEC, calibration, journaling,
or final verification in JavaScript.
