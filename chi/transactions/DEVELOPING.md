<!-- Guides changes to CHI transaction mechanisms. -->

# Developing CHI transaction mechanisms

Read [README.md](README.md) and the parent
[CHI developer guide](../DEVELOPING.md) before changing this area.
The parent's implementation map and focused validation sections are authoritative.

These modules contain both checking and execution mechanisms; they are not all monitors. Preserve each transaction lifetime and keep endpoint-specific policy with its caller.

Keep tests and authoring fixtures in [`../tests/`](../tests/), and behavioral
benches in [`tests/backend/`](../../tests/backend/DEVELOPING.md).
For source moves, update direct consumers, package documentation, and build/CI
paths together. Run `make check-boundaries` and the affected host and behavioral
checks with a fresh isolated compiled root. Directory boundaries do not add RTL
hierarchy or per-directory facade modules.
