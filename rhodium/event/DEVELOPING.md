<!-- Explains event inference, immutable instrumentation, DPI ownership, and validation. -->

# Developing event graphs

Read the public [`README.md`](README.md) first. This package owns the
compiler-side static event model, occurrence-aware dependency inference, and
manifest serialization, immutable instrumentation, and the C++ collector. It
does not own ready-valid hardware behavior, core IR semantics, or CIRCT lowering.

## Architecture and ownership

```mermaid
flowchart LR
  Author["trace_event flow checkpoint"] --> Interface["interface metadata"]
  Flow["standard flow transforms"] --> Trace["typed trace routes"]
  Interface --> Diagram["logical flow extraction"]
  Trace --> Diagram
  Diagram --> Analyze["event/analyze.rhm"]
  Analyze --> Model["event manifest"]
  Model --> JSON["deterministic JSON"]
```

- `rhodium/std/flow/event.rhdl` owns the ready-valid convenience annotation and
  transparent wiring.
- `rhodium/frontend/layers/interface.rhm` owns the generic event and typed
  trace-route metadata because it owns interface topology.
- `rhodium/diagram/` resolves metadata against verified IR connectivity and
  exposes the logical blocks and channels reused here.
- `model.rhm` owns tool-neutral event sites, dependencies, and manifests.
- `analyze.rhm` expands module definitions per concrete instance occurrence and
  infers nearest annotated predecessors.
- `json.rhm` is a projection of the structured manifest, not a second analysis.
- `main.rhm` is the public re-export surface.
- `copy.rhm` remaps values, places, memories, DPI declarations, and instance
  bindings into another design; it leaves extension metadata on the original.
- `instrument.rhm` validates the dynamic subset, selectively specializes
  module occurrences, memoizes unchanged imports, threads hidden references,
  and constructs ordinary core state and DPI operations.
- `runtime/` owns the fixed-width C ABI, order-independent graph storage,
  validation, and deterministic occurrence JSON.

The event package may consume core elaborations, logical diagrams, and
interface-owned metadata. Core, frontend, standard libraries, diagrams, and
backends must not depend on this optional compiler consumer. Hardware
instrumentation constructs ordinary verified IR and continues to use the
existing backend rather than introducing event cases into CIRCT lowering.

### Selective hierarchy rebuilding

For the supported combinational subset, mark event-bearing occurrences and
their ancestors before rebuilding. Ancestors must retarget changed children
and forward hidden metadata even when they contain no local annotation.
Only occurrences with local sites get event counters and node/edge emission;
the root also owns the reset callback.

Import each unmarked module definition once, memoized by original object
identity, recursively preserving its child-definition sharing. Parent instance
operations and bindings are still recreated: core ownership forbids referencing
an original-design module from the derived design. Reserve all original module
names when allocating specialization names so lazy imports cannot collide.
Original extension metadata remains on the original elaboration, including for
unchanged imported definitions.

Copying therefore scales with distinct unchanged definitions plus modified
occurrences; analysis and clock/reset validation remain occurrence-aware.
Future stateful adapters must extend the modification footprint to include
any additional metadata storage or transport they introduce.

## Extend trace coverage

Attach an `InterfaceTraceModel` only when a transform can state its possible
top-level endpoint routes exactly. Route indices refer to the transform's
flattened endpoint-array inputs and outputs. Do not infer routes from transform
labels or signal names.

Static routes are necessary but not sufficient authority for dynamic
instrumentation. The current optional `InterfaceTraceCombinational` contract
certifies one-to-one, zero-storage, non-inventing transfers. Its optional guard
retains the functional predicate, while event handshakes determine actual
transfer and parent-reference validity. Before traversing state, extend the
model with the transfer, ordering, and storage semantics needed for exact
lineage. Never upgrade route-only passthrough metadata to dynamic passthrough
merely because its endpoint cardinality is one-to-one.

An unmodeled transform is a supported boundary: inference reports the concrete
transform occurrence and stops with an error when a downstream annotation
requires traversal through it.

## Focused validation

Run:

```sh
make event-test
make event-runtime-test
```

The focused test covers linear transforms, hierarchy occurrence expansion,
forks, merges, JSON parsing, and opaque-transform rejection. Run
`make diagram-test` after changing the shared logical model or extraction, and
`make check-boundaries` after changing package placement or dependencies.

The runtime fixture covers same-cycle edges, repeated definitions, parent/child
hidden ports, map/filter/gate behavior, ready-valid stalls, Valid pulses, 65-bit
payloads, reset epochs, callback ordering, and edge deduplication. Host tests
also compare original CIRCT emission before and after instrumentation, check
unchanged nested/diamond definition sharing, and distinguish metadata-only
wrappers from event-bearing specializations. The runtime scoreboard exercises
functional logic in shared imported children as well as traced descendants.

Every direct Racket or Rhombus command must use a fresh `PLTCOMPILEDROOTS` as
required by the repository `AGENTS.md`.
