# WP-03: bounded inner and outer FEC

Status: implemented as a CPU/source-unit slice for the inner byte code and the
outer shard code.

## Delivered

- GF(256) arithmetic using the standard `0x11d` Reed–Solomon field.
- A systematic generator matrix derived from a Vandermonde construction, with
  the data rows transformed to identity form.
- Configurable data/parity shard counts and shard byte length.
- M0-shaped `32 data + 8 parity` configuration support.
- Recovery from any declared erasure set with at least `k` verified shards.
- Bounds on total shard count, shard size, and recovery workspace.
- Shortened systematic Reed-Solomon byte coding over GF(256), bounded to a
  255-byte codeword and configurable data/parity lengths.
- Correction of unknown byte errors within `floor(parity_bytes / 2)` and
  recovery of declared byte erasures up to `parity_bytes`.
- Present-byte consistency checks during byte-erasure recovery, plus a
  deterministic 16+8 encoder vector and adversarial correction tests.
- Tests across multiple shard erasure patterns, insufficient-shard failure,
  invalid/resource-limited configurations, byte errors, byte erasures, and
  integrity failures.

## Deliberate boundary

The outer work-package layer performs erasure recovery only. It does not
determine whether a present shard is corrupted. Tile CRC32C and inner ECC must
reject or erase corrupt observations before outer recovery is called. Inner
decoding is guaranteed only within its configured correction bound; a codeword
with more errors can be undetectably mapped to another valid codeword, so tile
CRC32C and final object SHA-256 remain required. Interleaving, fountain coding,
and optical symbol confidence handling remain separate work.

## Evidence

Evidence is source/unit. The tests do not establish optical loss rates,
throughput, hardware behavior, or whole-object conformance.
