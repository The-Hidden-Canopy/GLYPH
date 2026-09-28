# Contributing to GLYPH

GLYPH changes should remain small, reviewable, independently buildable, and
evidence-labeled.

Protocol behavior changes require tests and, where applicable, deterministic
vectors. Boundary tests should cover malformed input, bounded resource
failure, duplicate/reordered frames, erasures, integrity mismatch, and safe
temporary-file promotion. Do not auto-execute received content.

Claims about bitrate, distance, error rate, camera/display compatibility, or
media conformance must include reproducible configuration and hashes. A demo
or successful playback check is not sufficient.

Contributors should sign commits off under the Developer Certificate of Origin
(DCO) when commits are eventually created. No commit is created by the initial
repository bootstrap.

