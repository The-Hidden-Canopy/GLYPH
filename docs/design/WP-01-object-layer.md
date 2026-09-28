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
  rename promotion.
- Display-name sanitization that prevents path separators, control characters,
  Windows device names, and overwrite of an existing final path.

## Deliberate boundary

The encoder uses the fixed core map shape and canonical text-key ordering. Its
secure `encryption` field is currently null and `extensions` is currently an
empty map. There is no manifest decoder, secure-profile implementation, resume
journal, or FEC reconstruction in this work package.

## Evidence

Evidence is source/unit: SHA-256 vectors, a deterministic manifest byte vector,
resource-limit validation, path-boundary tests, integrity mismatch tests, and
successful atomic promotion. No optical, simulator, GPU, camera, or physical
hardware claim follows from these tests.
