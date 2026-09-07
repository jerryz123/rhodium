<!-- Describes static event manifests and opt-in linear hardware/DPI instrumentation. -->

# Event graphs

Use `rhodium/event` to infer possible direct dependencies between event sites
annotated on ready-valid flow topology. Static inference is read-only;
`instrument_events` optionally creates a second verified design containing
synthesizable event references and result-less DPI calls.

## Annotate events

The standard flow facade exports transparent event checkpoints:

```rhombus
import:
  lib("rhodium/std/flow.rhdl") open

source
  |> trace_event("accepted")
  |> pipe(2)
  |> trace_event("issued")
  |> sink
```

`trace_event` accepts `Decoupled` and `Irrevocable` payload flows.
`trace_valid_event` is the corresponding nonbackpressured `Valid` checkpoint.
Both preserve the original protocol and payload and record the complete flow
payload as the initial event payload schema. `~terminal: #true` marks an event
as terminal metadata without changing the transparent hardware path.

Labels must be nonempty and unique within one module definition. A reused
module definition still produces a distinct event-site occurrence for every
concrete instance path.

## Infer a manifest

```rhombus
import:
  lib("rhodium/event/main.rhm") open

def manifest = infer_event_manifest(logical_design)
def json = event_manifest_to_json(manifest)
```

`infer_event_manifest` verifies and extracts the supplied
`DesignElaboration`, expands its reachable hierarchy by instance occurrence,
and walks backward from every annotated site. Traversal stops at the first
annotated site on each path, so dependencies describe nearest possible parents
instead of transitive closure.

The structured result contains:

- `EventSite`: stable occurrence ID, label, defining module, instance path,
  module-local site ordinal, protocol, packed payload type and width, terminal
  flag, and source location;
- `EventDependency`: parent ID, child ID, and ordered intervening transform
  path;
- `EventManifest`: original elaboration, sites, and dependencies.

`event_manifest_to_json` emits a deterministic version-1 object with format
name `rhodium-event-graph`, the selected top, sites, and dependencies.

## Traceable transforms

An interface transform is traversable only when it carries an
`InterfaceTraceModel`. That model supplies explicit possible input-to-output
routes independently of its display label. The current standard flow metadata
covers event checkpoints, map, filter, fixed and elastic pipe, in-order queue,
atomic fork, and zip. A downstream annotation whose upstream walk reaches an
unmodeled transform is rejected rather than assigned an approximate parent.

## Deliberate limits

- The manifest describes possible static dependencies, not runtime event
  occurrences.
- Static inference never inserts hardware. Dynamic instrumentation supports
  only the linear subset described below; buffered and branching paths still
  require future dynamic adapters.
- Only flat top-level flow endpoints are traceable; nested interface members
  are rejected.
- Source locations are retained when an annotation supplies one through the
  low-level interface API. The standard flow helpers currently report
  `<unknown>` pending call-site location capture.
- Terminal metadata is recorded but does not yet prune downstream analysis.
- Buffered dynamic lineage requires future stateful trace adapters.

## Instrument a linear path

```rhombus
def traced = instrument_events(logical_design)
// Emit traced.instrumented.design through the existing CIRCT backend.
def manifest_json = event_manifest_to_json(traced.manifest)
```

The returned `EventInstrumentedElaboration` retains `original`, `instrumented`,
and `manifest`. No original IR objects or metadata are modified. The derived
top keeps the original functional ports. Only event-bearing occurrences and
their ancestors are specialized; hidden record ports carry references across
parent, child, and sibling boundaries. Unchanged module definitions are
imported once into the derived design and shared by its instances, without
added trace ports, counters, or DPI calls. This preserves sharing within the new
design, not object identity across designs. Forwarding-only ancestors need no
event counters. Original extension metadata stays with the original design
and manifest, not with the rebuilt modules.

The supported dynamic path consists of annotations, interface connections,
hierarchy boundaries, `map_flow`, `map_valid`, `filter_flow`, `filter_valid`,
and `gate_flow`. A typed combinational trace contract is mandatory for every
intervening transform. Route-only models (including pipes, queues, forks and
joins), disconnected or opaque upstream boundaries, multiple parent paths,
branching dependencies, and descendants of terminal events are rejected.

`EventInstrumentationConfig(clock_port, reset_port)` selects the top-level
`Clock` and synchronous `Reset` inputs (defaults: `"clock"`, `"reset"`). All
functional clock/reset signals in the reachable hierarchy must resolve to
these inputs through direct wiring or casts. Generated clocks and independent
reset domains are rejected. Assert reset for at least one sampled rising edge
before tracing. Reset clears all trace counters and the runtime graph;
identities are unique within that reset epoch, not across retained epochs.

Ready-valid checkpoints fire only on `valid & ready`; Valid checkpoints fire
on `valid`. A site increments its own 64-bit sequence counter and emits its
current reference on that same edge. Combinational consumers therefore see
the intended same-cycle parent, including across hierarchy. Parent-presence
and sequence-exhaustion assertions fail instead of silently inventing lineage
or allowing identity wraparound. Cycle counts start at zero after reset.

## DPI runtime and visualization handoff

Link [`runtime/rhodium_event.cc`](runtime/rhodium_event.cc) into the simulator.
Its [header](runtime/rhodium_event.h) defines the fixed ABI and `graph()` API:

- `rhodium_event_node` records a site, sequence, cycle, and payload width.
- `rhodium_event_payload` supplies zero-padded 32-bit words, least-significant
  word first, so arbitrary fixed-width packed payloads use the same ABI.
- `rhodium_event_edge` records an exact parent/child reference pair.
- `rhodium_event_reset` clears the graph on an asserted sampled reset.

The site number is the zero-based index into **the accompanying manifest's
`sites` array**, not a hash. Together with the per-site sequence it identifies
one concrete occurrence, even for repeated module definitions. Each hidden
reference is an ordinary packed record containing `valid`, `site`, and
`sequence`. Manifest IDs retain the exact human-readable hierarchy path.

The collector tolerates node, payload, and edge callbacks in any order and
deduplicates edges. After the simulator has settled the sampled edge, call
`rhodium_event::graph().json()` for deterministic output; incomplete callbacks
and duplicate identities are errors. The export uses decimal strings for
64-bit sequence/cycle values to avoid rounding in JavaScript. Join numeric
sites with the separately emitted manifest to recover labels, hierarchy,
source locations, and payload schemas. No visualizer or manifest parser is
bundled yet.

The current collector supports one instrumented top per process on the
simulator thread. It retains the whole current epoch in memory. Instrumented
designs are simulation artifacts because of their DPI calls; keep using the
original elaboration for synthesis.

Contributors should read [`DEVELOPING.md`](DEVELOPING.md) before extending the
model, hierarchy expansion, trace metadata, or JSON format.
