# GLYPH remaining-work integration - 2026-11-17

Current public documentation explicitly keeps optional integrations independent and describes capability precedents without disclosing private implementation paths. Reconciled old duplicate-foundation history only after proving all production/test blobs identical. Two reviewed documentation commits were carried, with the final specification cleanup incorporated during conflict resolution.

## Files changed

- `README.md`
- `docs/GLYPH_Open_Engineering_Specification_v0.1.md`
- `WORKTREE_INTEGRATION_20261117.md` (this ledger).

Exact initial refs, source-equivalence proof, commands, complete positive/negative logs, document-link check and promotion receipt are local under the ignored `build/remaining-integration-20261117/` directory.

## Verification and limits

Fresh native Debug configure/build and 22/22 CTest cases passed; 13 local documentation links passed. No production/test source changed. The initial Release run passed 15 and failed 7 tests: setup/encode calls inside assert expressions disappear under NDEBUG, leading to invalid fixture state and crashes. The Debug result does not qualify Release test execution; retained both logs and recorded this existing harness limitation instead of changing production code.

Not run: Assertion-dependent tests are unsuitable for NDEBUG builds until separately repaired. No hardware optical transfer, browser/mobile visual pass, throughput benchmark, protocol conformance or complete arbitrary-object hardware transfer was tested.

## Invariants and cross-product impact

Production/test source and all existing domain-transition, permission, scope, approval and audit/event owners are unchanged. No authority comes from a public source checkout. Company authorship/license notices are retained. No RegOS, IDA training, hidden_canopy_bot or Ask_IDA service API/schema was edited; no dataset, evaluation artifact, trained model, live trust state or promotion was changed. Their tests were not run. Optional adapters remain explicit boundaries; no private runtime dependency is added.

This repairs current documentation, not historical Git redaction. No public push, deployment, history rewrite, credential access or dependency install was performed. Dataset progress: none; training progress: none; capability progress: current-boundary documentation and recorded local source/unit verification; promotion progress: local main only.

## Risk, rejected options and rollback

Assertion-dependent tests are unsuitable for NDEBUG builds until separately repaired. No hardware optical transfer, browser/mobile visual pass, throughput benchmark, protocol conformance or complete arbitrary-object hardware transfer was tested. Rejected wholesale replacement by older source, removal of company attribution, treating document changes as runtime authorization, deleting active work and calling skipped/failed checks successful. No new production behavior risk or migration was introduced. Reconstruct original source from `backup/main-before-remaining-integration-20261117` in a new worktree, or revert the reviewed integration on active main. Retired copies require verified source/evidence backups and exact refs before removal.
