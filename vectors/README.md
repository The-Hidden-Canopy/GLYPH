# Conformance vectors

Vectors will cover canonical manifests, frame and tile headers, optical cell
matrices, FEC reconstruction, secure transcripts, and whole-object hashes.

Released vectors are immutable. Corrections receive new vector identifiers;
they are not silently replaced. No vector in this initial foundation claims
optical or hardware conformance.

Current vector:

- `glj2-completion-record-v1.hex`: transfer ID `01..10`, object digest
  `00..1f`, manifest digest `ff..e0`, object size `123456`, followed by an
  empty GLJ2 completion record.
