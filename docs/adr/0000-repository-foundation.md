# ADR-0000: Establish the GLYPH repository foundation

- Status: accepted
- Date: 2026-09-28
- Scope: repository bootstrap only

## Context

The GLYPH v0.1 engineering specification defines an open, offline optical
transport whose completion condition is exact reconstruction and SHA-256
verification. The repository must be independently buildable and must not
mistake a scaffold, simulator, or demo for optical conformance.

## Decision

Create a standalone C++20 repository with a small CPU-portable core seam,
CTest coverage for that seam, the full supplied specification, and explicit
documentation/vector/benchmark boundaries. Keep hardware, GPU, crypto, FEC,
and media work package-specific until each has normative behavior and
deterministic tests.

## Evidence and non-claims

The initial evidence class is source/unit. No camera, display, GPU, simulator,
network, media, throughput, or physical-hardware result is claimed by this
bootstrap.

## Rejected options

- Copying code from donor repositories would violate the independent-build
  boundary and add unreviewed implementation lineage.
- Creating a generic GUI or media codec before the object/frame contracts would
  obscure the exact-byte completion rule.
- Adding a network service, account flow, telemetry, or hosted activation path
  would violate the open-source contract.

## Follow-up

WP-01 through WP-10 in the specification are the bounded next work packages.
Each package must retain vectors/receipts appropriate to its evidence class.

