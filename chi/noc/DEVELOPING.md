<!-- Guides changes to CHI network integration. -->

# Developing CHI network integration

Read [README.md](README.md) and the parent
[CHI developer guide](../DEVELOPING.md) before changing this area.
The parent's implementation map and focused validation sections are authoritative.

Keep noc-authoring.rhm independent of Rhodium and CIRCT. Generic topology, routing analysis, and router hardware remain in the root noc/ package.

Keep tests and authoring fixtures in [`../tests/`](../tests/), and behavioral
benches in [`tests/backend/`](../../tests/backend/DEVELOPING.md).
For source moves, update direct consumers, package documentation, and build/CI
paths together. Run `make check-boundaries` and the affected host and behavioral
checks with a fresh isolated compiled root. Directory boundaries do not add RTL
hierarchy or per-directory facade modules.
