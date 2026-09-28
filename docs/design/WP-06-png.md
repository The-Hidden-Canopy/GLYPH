# Image boundary: canonical RGBA8 PNG

Status: implemented as a dependency-free offline image artifact boundary.

## Delivered

- Deterministic 8-bit RGBA, non-interlaced PNG encoding with stored DEFLATE
  blocks and verified PNG CRC/adler checksums.
- PNG decoding for canonical RGBA8 images with standard zlib DEFLATE stored,
  fixed-Huffman, and dynamic-Huffman blocks.
- All five PNG scanline filters: none, sub, up, average, and Paeth.
- Explicit limits on dimensions, decoded pixels, compressed input, and all
  intermediate allocations.
- Transactional encode/decode outputs.
- Round-trip, corruption, truncation, malformed-dimension, and input-size
  boundary tests.

## Deliberate boundary

This module only handles in-memory RGBA8 PNG artifacts. It does not read or
write files, invoke image viewers, auto-open received content, or accept
palette, grayscale, interlaced, or compressed image variants outside the
canonical form. It is suitable for saved-frame simulator/decoder plumbing;
successful PNG decoding is not optical recovery or transfer completion.

## Evidence

Evidence is source/unit. The encoder/decoder round-trip and failure tests do
not establish camera behavior, display behavior, image-quality fidelity,
throughput, or hardware conformance.
