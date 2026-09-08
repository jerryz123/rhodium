<!-- Guides changes to CHI transaction adapters. -->

# Developing CHI transaction adapters

Read [README.md](README.md) and the parent
[CHI developer guide](../DEVELOPING.md) before changing this area.
The parent's implementation map and focused validation sections are authoritative.

Preserve transaction identity and unaffected packet metadata. These adapters do not define an alternative memory protocol or own generic flow machinery.

Keep tests and authoring fixtures in [`../tests/`](../tests/), and behavioral
benches in [`tests/backend/`](../../tests/backend/DEVELOPING.md).
For source moves, update direct consumers, package documentation, and build/CI
paths together. Run `make check-boundaries` and the affected host and behavioral
checks with a fresh isolated compiled root. Directory boundaries do not add RTL
hierarchy or per-directory facade modules.
