# ADR-0001: GLJ2 resume journal and completion record

- Status: accepted for the current pre-release wire
- Date: 2026-09-28
- Scope: `ResumeJournal` persistence and restart completion evidence

## Context

The resume journal gained a completion record (`type = 3`) so a restart can
distinguish a durably promoted object from a journal that only contains shard
or verified-block receipts. That is a normative wire change. Keeping the old
`GLJ1` magic/version would let an older reader encounter a record it cannot
interpret as part of a complete journal.

## Decision

The journal wire is version 2 and uses the `GLJ2` four-byte magic. The 100-byte
identity header and existing shard (`type = 1`) and verified-block (`type = 2`)
records retain their encoding. The completion record is an empty `type = 3`
record with the normal length and CRC envelope:

```
u32 payload_length = 1
u8  type           = 3
u32 crc32c(type)
```

New readers accept GLJ2 journals containing record types 1, 2, and 3. A GLJ1
journal is rejected as a protocol-version mismatch; there is no silent
in-place migration. A reader accepts a truncated final record tail only after
the GLJ2 header and every preceding complete record have passed identity and
CRC validation, then truncates that tail through the already-open secure file
handle.

The completion record remains an evidence marker, not an authority grant. The
session writes it only after exact object size and SHA-256 verification and
successful no-replace final promotion. The journal is single-writer and its
file path is opened through a regular-file, no-follow/reparse-safe OS handle.

## Compatibility and security impact

This is intentionally forward-incompatible with GLJ1 readers. The version
boundary is explicit and fail-closed. Existing GLJ1 files are not rewritten or
treated as resumable state. Journal and partial-object opens reject symlink or
reparse targets and hard-linked regular files, acquire an exclusive writer
lock, and keep the opened handle for writes, hashing, synchronization, and
promotion. Final promotion does not replace an existing destination.

## Vector impact

No prior vector was changed. The new canonical vector is
`vectors/glj2-completion-record-v1.hex`; it covers a GLJ2 header followed by an
empty completion record for the identity documented alongside the vector in
`vectors/README.md`. The unit test compares the emitted bytes to this stable
vector shape.

## Rejected options

- Keeping `GLJ1` while adding an unknown record type: rejected because old
  readers could not distinguish the new completion semantics.
- Reopening a path after each durability boundary: rejected because it leaves
  a check-then-open race at the storage trust boundary.
- Replacing an existing final object during promotion: rejected because it
  would allow an unrelated destination to be overwritten.
