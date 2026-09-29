# Logical transfer session orchestrator

Status: implemented as a bounded offline logical-block session boundary.

## Delivered

- Sender states: IDLE, HASHING, MANIFEST_READY, BOOTSTRAP, TRANSMITTING,
  FINAL_REPEAT, DONE, and explicit failure/cancel states.
- Receiver states: IDLE, SEARCHING, SURFACE_FOUND, CALIBRATING, MANIFEST,
  RECEIVING, RECOVERING, VERIFYING, COMPLETE, and explicit failure states.
- Manifest construction and validation from a caller-owned logical object or a
  bounded local source file.
- Streaming file-source block emission with a second digest/length check over
  the bytes actually emitted before `FINAL_REPEAT`; source mutation or
  truncation that changes transmitted bytes fails closed.
- Fixed-size logical block emission with strict sequential block acceptance.
- Durable file checkpoint before each verified-block journal acknowledgement.
- Resume-journal identity binding to transfer ID, manifest digest, object digest,
  and object length.
- Deterministic identity-bound partial-object paths for restartable receivers.
- Existing final and partial symlink entries are rejected before any write or
  restart recovery is attempted.
- Rehydration of a contiguous verified-block prefix after process interruption;
  unverified partial tails are truncated before more bytes are accepted.
- Temporary output plus atomic final promotion only after exact length and
  SHA-256 equality.
- Windows final promotion uses a write-through no-replace move; POSIX-like hosts
  link the retained secure file without replacement, remove the temporary name,
  and synchronize the containing directory entry.
- Durable completion receipt after promotion, with restart recovery that accepts
  only an exact-hash final file backed by a complete verified prefix.
- Rejection of unsupported FEC profiles and out-of-order blocks.

## Deliberate boundary

This orchestrator consumes already-accepted logical blocks. It is not yet an
optical frame/FEC acquisition layer, camera session, or mobile restart-resume
backend. The C sender/receiver ABI is a thin binding of this logical boundary;
it adds no camera or FEC acquisition. Restart recovery is limited to a
contiguous logical block prefix produced by this session; non-contiguous journal claims,
missing partial bytes, final-name collisions, and unsupported profiles fail
closed unless the existing final file is exactly the manifest object and the
journal proves the complete verified prefix. Lower-layer shard receipts are
preserved but are not interpreted as logical object bytes by this orchestrator.

`AtomicObjectWriter::durable_checkpoint()` synchronizes its retained secure
file handle through the host operating-system primitive before a verified-block
journal acknowledgement. The lower-level `checkpoint()` method confirms that
the handle remains open. Directory-entry durability after no-replace promotion is
covered by Windows write-through promotion and POSIX parent-directory
`fsync`; other hosts retain an explicit platform boundary. Final promotion is
followed by a durable completion journal record; a restart can therefore close
the crash window between the atomic move and the caller observing `COMPLETE`.
`ReceiverSession::pause()` and destruction of an active receiver durably flush
and retain the identity-bound partial; explicit `cancel()` discards the partial
and its session journal.

## Evidence

Evidence is source/unit. Tests cover full logical sender-to-receiver delivery,
pause/reopen/resume, exact promoted bytes, truncation of unverified partial
tails, journal identity/receipts, non-contiguous journal rejection,
object-limit rejection, out-of-order block rejection, unsupported-profile
rejection, local-file streaming, source mutation rejection, and final storage
behavior. This does not establish optical, camera, mobile, or physical
conformance evidence.
