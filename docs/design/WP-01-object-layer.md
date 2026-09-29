# WP-01: exact object layer

Status: implemented as a CPU/source-unit slice.

## Delivered

- Portable streaming SHA-256 with known-vector and chunked-update tests.
- Bounded file hashing that refuses configured object-size and I/O-buffer
  violations.
- Exact object verification by byte length plus SHA-256.
- A deterministic CBOR manifest encoder for the core v0.1 fields.
- A bounded decoder for the same fixed core shape, with canonical ordering and
  unsupported secure/extension fields rejected closed.
- Temporary output writes followed by length/hash verification and same-directory
  no-replace promotion through a retained secure file handle.
- Display-name sanitization that prevents path separators, control characters,
  Windows device names, and overwrite of an existing final path.
- An identity-bound append-only resume journal for idempotent shard receipts
  and verified block ranges, with CRC-protected records, bounded replay,
  crash-tail truncation, GLJ2 versioning, and a session-only completion marker.

## Deliberate boundary

The encoder and decoder use the fixed core map shape and canonical text-key
ordering. Their secure `encryption` field is currently null and `extensions`
is currently an empty map. Secure-profile implementation remains separate.
The journal is single-writer and stores only verified receipt metadata;
unverified object bytes remain disposable. FEC is delivered in WP-03.

## Evidence

Evidence is source/unit: SHA-256 vectors, a deterministic manifest byte vector,
resource-limit validation, path-boundary tests, integrity mismatch tests,
successful atomic promotion, and resume-journal identity, replay, corruption,
tail-recovery, idempotency, and bound tests. No optical, simulator, GPU,
camera, or physical hardware claim follows from these tests.
