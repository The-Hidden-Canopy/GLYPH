# Synthetic optical transport loopback

Status: implemented as a deterministic source/unit composition test.

## Delivered

The loopback composes the currently implemented boundaries in one path:

```text
logical shards
  -> outer FEC parity
  -> MP0 tile/frame codec
  -> canonical RGBA8 surface
  -> PNG encode/decode
  -> strict MP0 decoder
  -> tile inner ECC/CRC
  -> outer-FEC group assembler
```

The test receives a reordered subset of data and parity shards, reconstructs
the configured final block length, and compares the result byte-for-byte with
the source fixture.

## Deliberate boundary

This is synthetic software evidence. It does not model perspective,
camera-sensor noise, rolling shutter, exposure/white-balance drift, display
timing, Android/iOS APIs, throughput, or physical handheld operation. It also
does not promote a file through the session object writer; that final hash and
atomic-storage gate remains separately tested by WP-09.

## Evidence

`glyph.synthetic_loopback.unit` is a deterministic 21st CTest target and uses
no network, account, cloud, or external runtime dependency.
