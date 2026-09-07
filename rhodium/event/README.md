<!-- Describes static event manifests and opt-in storage/selection lineage with DPI emission. -->

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
- `EventDependency`: parent ID, child ID, ordered intervening transform path,
  `latency_cycles` (a nonnegative fixed delay, or `false` for variable/unknown
  latency), and ordered `trace_stages` for a linear path (`false` when the path
  includes selection or is uncertified);
- `EventManifest`: original elaboration, sites, dependencies, and `trace_plans`
  keyed by child site ID when inferred with `~dynamic: #true`.

Dynamic plans are typed expressions: `EventTraceSource` names the nearest
annotation (or an unannotated root), `EventTracePipeline` wraps an input plan
with storage stages, and `EventTraceSelection` selects among input plans using
occurrence-qualified grant values. Storage before selection belongs to its
input branch; storage afterward wraps the selected reference. Static manifests
leave `trace_plans` empty.

`event_manifest_to_json` emits a deterministic version-1 object with format
name `rhodium-event-graph`, the selected top, sites, and dependencies.
The IR-backed stage plan stays in the structured manifest, not in JSON.

## Traceable transforms

An interface transform is traversable only when it carries an
`InterfaceTraceModel`. That model supplies explicit possible input-to-output
routes independently of its display label. The current standard flow metadata
covers event checkpoints, map, filter, fixed and elastic pipe, in-order queue, ready-valid arbiters,
atomic fork, and zip. A downstream annotation whose upstream walk reaches an
unmodeled transform is rejected rather than assigned an approximate parent.

## Deliberate limits

- The manifest describes possible static dependencies, not runtime event
  occurrences.
- Static inference never inserts hardware. Dynamic instrumentation supports
  the storage and selection subset described below; forks and joins still
  require future dynamic adapters.
- Only flat top-level flow endpoints are traceable; nested interface members
  are rejected.
- Source locations are retained when an annotation supplies one through the
  low-level interface API. The standard flow helpers currently report
  `<unknown>` pending call-site location capture.
- Terminal metadata is recorded but does not yet prune downstream analysis.
- Control-only queues and opaque storage require dedicated trace adapters.

## Instrument storage and selection paths

```rhombus
def traced = instrument_events(logical_design)
// Emit traced.instrumented.design through the existing CIRCT backend.
def manifest_json = event_manifest_to_json(traced.manifest)
```

The returned `EventInstrumentedElaboration` retains `original`, `instrumented`,
and `manifest`. No original IR objects or metadata are modified. The derived
top keeps the original functional ports. Event-bearing occurrences, elastic
control-source occurrences, and their ancestors are specialized; hidden record
ports carry references across
parent, child, and sibling boundaries. Unchanged module definitions are
imported once into the derived design and shared by its instances, without
added trace ports, counters, or DPI calls. This preserves sharing within the new
design, not object identity across designs. Forwarding-only ancestors need no
event counters. Original extension metadata stays with the original design
and manifest, not with the rebuilt modules.

The supported dynamic path consists of annotations, interface connections,
hierarchy boundaries, `map_flow`, `map_valid`, `filter_flow`, `filter_valid`,
`gate_flow`, fixed-latency `valid_pipe(stages)`, and elastic ready-valid
`pipe(stages)`, in-order `queue(depth, ~pipe: ..., ~flow: ...)`,
and ready-valid `arbiter(...)` and `rr_arbiter(...)`.
Every intervening transform needs a typed dynamic trace
contract. Route-only models (including control-only queues, forks and joins), disconnected or opaque
upstream boundaries, uncertified multiple-parent paths,
fanout dependencies, and descendants of terminal events are rejected.
If a selection path has any annotated ancestor, every selectable input path
must have one; partially annotated ancestry is rejected with a diagnostic.
An annotation with no annotated ancestor on any path remains a root event.

`EventInstrumentationConfig(clock_port, reset_port)` selects the top-level
`Clock` and synchronous `Reset` inputs (defaults: `"clock"`, `"reset"`). All
functional clock/reset signals in the reachable hierarchy must resolve to
these inputs through direct wiring or casts. Generated clocks and independent
reset domains are rejected. Assert reset for at least one sampled rising edge
before tracing. Reset clears all trace counters and the runtime graph;
identities are unique within that reset epoch, not across retained epochs.

For `valid_pipe`, the compiler sums certified delays along each dependency and
inserts an unconditional reference shift register at its downstream annotation.
The functional pipeline modules remain unchanged and shareable. This also
supports composed delays across hierarchy and intervening maps or filters;
filtered transactions do not produce downstream nodes. Reset flushes pending
references as well as the functional pipeline validity. No queue, elastic
pipeline, or variable-latency behavior is inferred from a stage count or name.

Ready-valid checkpoints fire only on `valid & ready`; Valid checkpoints fire
on `valid`. For an elastic pipe, each shadow stage loads its upstream reference
only when the corresponding functional stage advances, loads an invalid
reference for a bubble, and holds while stalled. Hidden observation ports carry
the pipe's actual advance and input-valid signals to the shadow registers;
they never drive functional ready, valid, or payload signals. Ordered stage
plans preserve composition across multiple pipes and hierarchy boundaries.

For queues, the compiler adds a same-capacity shadow reference memory, driven
by the functional queue's storage-write enable and read/write addresses.
It uses the actual resident-head validity and bypass selector: an empty
flow-through transfer uses the live upstream reference without a storage write,
while full simultaneous dequeue/enqueue consumes the old reference and stores
the replacement. Reset suppresses shadow writes and functional occupancy hides
stale entries until overwritten; the shadow memory needs no reset sweep.
No payload, pointer, or occupancy policy is duplicated. Queue stages compose
in order with pipes, maps, and filters, including across hierarchy. Only user
annotations emit nodes; queue operations add no extra DPI callbacks.

For arbitration, static dependencies list every possible nearest parent, but
each runtime occurrence has only the parent selected by the actual grant on
that transfer. Grants are observed, not reconstructed from arbitration policy.
Unselected input storage continues to track its own transactions. Output
storage carries the selected reference, so later grant changes cannot change
an already-buffered transaction's parent. Nested arbiters use the same rule.
One-hot assertions reject overlapping grants; the DPI ABI and collector remain
unchanged. Valid-only, control-only, and packet arbitration remain outside this
dynamic subset.

A site increments its own 64-bit sequence counter and emits its
current reference on that same edge. Combinational consumers therefore see
the intended same-cycle parent, including across hierarchy; pipeline paths
consume the corresponding stored reference instead. Parent-presence
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
