# Outer-FEC group assembler

Status: implemented as a bounded logical-shard assembly boundary.

## Delivered

- Transfer ID, block ID, FEC-group, shard-size, and shard-count binding.
- Reordered shard acceptance through the existing systematic Reed-Solomon
  implementation.
- Exact duplicate suppression and conflicting-duplicate integrity rejection.
- Reconstruction after the configured data-shard threshold, with a precise
  final block length that discards deterministic padding.
- Transactional state/result behavior when identity, size, or reconstruction
  validation fails.

## Deliberate boundary

The assembler consumes logical shards whose tile CRC and inner-code checks have
already succeeded. It does not decode camera observations, perform tile CRC or
inner ECC itself, persist a resume journal, or feed an object writer. A group
is single-use and must be created with the expected final block length; outer
FEC does not authorize guessed or unverified bytes.

## Evidence

The unit test covers reordered data/parity shards, reconstruction, exact and
conflicting duplicates, late arrival of a previously reconstructed shard,
identity mismatch, and bounded configuration rejection. This is source/unit
evidence only; it is not optical or physical conformance evidence.
