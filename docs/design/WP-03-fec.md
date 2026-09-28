# WP-03: bounded systematic erasure recovery

Status: implemented as a CPU/source-unit slice.

## Delivered

- GF(256) arithmetic using the standard `0x11d` Reed–Solomon field.
- A systematic generator matrix derived from a Vandermonde construction, with
  the data rows transformed to identity form.
- Configurable data/parity shard counts and shard byte length.
- M0-shaped `32 data + 8 parity` configuration support.
- Recovery from any declared erasure set with at least `k` verified shards.
- Bounds on total shard count, shard size, and recovery workspace.
- Tests across multiple erasure patterns, insufficient-shard failure, and
  invalid/resource-limited configurations.

## Deliberate boundary

This work package performs erasure recovery only. It does not determine whether
a present shard is corrupted. Tile CRC32C and future inner ECC must reject or
erase corrupt observations before this layer is called. It does not implement
the shortened inner Reed–Solomon byte code, interleaving, fountain coding, or
optical symbol confidence handling.

## Evidence

Evidence is source/unit. The tests do not establish optical loss rates,
throughput, hardware behavior, or whole-object conformance.

