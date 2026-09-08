<!-- Describes static event inference and compiler-generated lineage instrumentation. -->

# Event graphs

The independent [RHEG library](../../rheg/README.md) collects the emitted DPI
callbacks and exports snapshots or streaming Perfetto traces.

Use `rhodium/event` to infer possible direct dependencies between event sites
annotated on ready-valid flow topology. Static inference is read-only;
`instrument_events` optionally creates a second verified design containing
synthesizable event references and result-less DPI calls.

## Annotate events

Use `trace_event(label, ~root: #true)` or `trace_valid_event(label, ~root: #true)`
to explicitly start a lineage at a checkpoint, including at an opaque component
output. A root intentionally has no incoming dependency, even if an upstream
annotation exists; downstream inference proceeds normally from its new identity.
The manifest records this decision as `sites[].root`. The default is false:
ordinary checkpoints still reject opaque or uncertified ancestry. Root does not
certify the hidden component, relax clock/reset checks, or correlate its inputs
and outputs. The low-level `describe_interface_event` accepts the same option.

The [flow library facade](../../flow/README.md) exports transparent event checkpoints:

```rhombus
import:
  lib("flow/main.rhdl") open

source
  |> trace_event("accepted")
  |> pipe(2)
  |> trace_event("issued")
  |> sink
```

`trace_event` accepts `Decoupled` and `Irrevocable` payload flows.
`trace_valid_event` is the corresponding nonbackpressured `Valid` checkpoint.
Both preserve the original protocol and payload. Bare checkpoints capture no
payload fields. `~terminal: #true` marks an event
as terminal metadata without changing the transparent hardware path.

Labels must be nonempty and unique within one module definition. A reused
module definition still produces a distinct event-site occurrence for every
concrete instance path.

### Capture fields

Select observations with a typed binder, independently at each checkpoint:

```rhombus
def observed = source |> trace_event("fetch", ~root: #true, ~fields: payload):
  pc: payload.pc
  instruction: payload.instruction
  low_pc(~format: "unsigned"): payload.pc[0..5]
```

Names are unique ASCII identifiers, preserved in exported arguments; `cycle`
and `sequence` are reserved for built-in event arguments. Values must be local
scalar hardware expressions; nested field selections, aliases, slices, and combinational
expressions are allowed. Select aggregate leaves explicitly or cast an aggregate
to Bits for an intentional packed capture. Missing members, duplicate names,
foreign-module values, and incompatible formats fail during elaboration.
The optional formats are `hex`, `unsigned`, `signed` (two's complement), and
`bool` (one bit only). Defaults are `bool` for Bool, `signed` for SInt, and
`hex` for other scalar types. Enum labels are not decoded in this first version.

For an explicit whole-payload dump use `~payload: #true`; this creates the
single `raw` capture. Do not combine it with named captures. Low-level adapters
can pass `~fields: [event_field("pc", pc), ...]` to `describe_interface_event`;
its existing `~payload: value` remains an explicit raw capture.

`sites[].fields` records ordered names, source type descriptions, widths,
least-significant-bit offsets, and encodings. Offsets describe only the selected
capture record: the first field occupies its most-significant bits. The aggregate
`payload_width` is the sum of selected widths, not the functional payload width.
No selection means no payload DPI calls. The unchanged word ABI transports this
compact record; [RHEG](../../rheg/README.md#named-captures) exposes named values.

All fields sample under the same transfer predicate as their occurrence.
Selection does not change event identity, lineage inference, hidden reference
storage, reset, or timing. Captures are local observations, not extra data
propagated along dependency edges.

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
  includes selection, routing, replication, or is uncertified);
- `EventManifest`: original elaboration, sites, dependencies, and `trace_plans`
  keyed by child site ID when inferred with `~dynamic: #true`.

Dynamic plans are typed expressions: `EventTraceSource` names the nearest
annotation (or an unannotated root), `EventTracePipeline` wraps an input plan
with storage stages, and `EventTraceSelection` selects among input plans using
occurrence-qualified grant values. Storage before selection belongs to its
input branch; storage afterward wraps the selected reference. Static manifests
leave `trace_plans` empty. `EventTraceRouting` wraps one input plan with an
occurrence-qualified routing ID, original predicates, and output index. Its
reference is invalid on every unselected branch; storage after routing retains
the selected identity independently of subsequent selector changes.
`EventTraceReplication` wraps one input plan with a concrete atomic-fork ID and
output index. All outputs inherit the same reference; downstream storage wraps
each branch's copy. It creates no additional visible event sites.
`EventTraceBroadcast` additionally retains the concrete buffered-broadcast ID,
input-acceptance control, and selected recipient's pending bit. Its latency is
variable and its outputs use a stored parent, not the live input reference.
`EventTraceJoin` combines the ordered input plans of an atomic rendezvous.
`event_trace_capacity(plan)` reports its statically bounded parent-slot count:
one at an annotation, the sum at a join, the maximum at selection, and unchanged
through storage, routing, or replication. Duplicate identities still occupy
slots; the runtime deduplicates emitted edges by the complete occurrence pair.

`event_manifest_to_json` emits a deterministic version-1 object with format
name `rhodium-event-graph`, the selected top, sites, and dependencies.
The IR-backed stage plan stays in the structured manifest, not in JSON.

## Traceable transforms

An interface transform is traversable only when it carries an
`InterfaceTraceModel`. That model supplies explicit possible input-to-output
routes independently of its display label. The current flow library metadata
covers event checkpoints, map, filter, fixed and elastic pipe, in-order queue, ready-valid arbiters, `demux_flow`,
atomic fork, buffered broadcast, and zip. A downstream annotation whose upstream walk reaches an
unmodeled transform is rejected rather than assigned an approximate parent.

## Deliberate limits

- The manifest describes possible static dependencies, not runtime event
  occurrences.
- Static inference never inserts hardware. Dynamic instrumentation supports
  the storage, selection, and replication subset described below;
  selective/control-only forks, control-only broadcasts, and selective/control-only joins still require
  future dynamic adapters.
- Only flat top-level flow endpoints are traceable; nested interface members
  are rejected.
- Source locations are retained when an annotation supplies one through the
  low-level interface API. The flow helpers currently report
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
ready-valid `arbiter(...)` and `rr_arbiter(...)`, `demux_flow(...)`, `atomic_fork(...)`, `broadcast(...)`, and atomic `zip_flow(...)`.
Every intervening transform needs a typed dynamic trace
contract. Route-only models (including control-only queues and selective/control-only forks), unmodeled joins, disconnected or opaque
upstream boundaries, uncertified multiple-parent paths,
uncertified fanout dependencies, and descendants of terminal events are rejected.
Multiple child sites are supported only when every pair of possible paths from
their shared parent uses different outputs of a common certified routing or
replication occurrence. This supports nested demuxes, atomic forks, broadcasts, and
branch-local storage; an unexplained split remains an error.
Routing checks predicate mutual exclusion at runtime. No selected output means
no input transfer, while older buffered branches can still complete together.
If a selection path has any annotated ancestor, every selectable input path
must have one; partially annotated ancestry is rejected with a diagnostic.
An annotation with no annotated ancestor on any path remains a root event.

At an atomic fork, every branch accepts the same input transaction together.
The compiler forwards the same parent reference onto each branch without new
fork-local state or observation signals. Queues and pipes after the fork retain
their own copies, so downstream events may complete independently and at
different cycles. Each emits one DPI edge to its nearest annotated parent;
there is no synthetic fork node and no change to the collector ABI. An arbiter
may later select individual replicas, producing separate occurrences with the
same ancestor. An atomic `zip_flow` instead combines all input lineages.

An annotation after a join emits one node and a DPI edge for each valid incoming
parent slot, then exposes only its own reference to downstream instrumentation.
Synthesizable records carry a transaction-valid bit and a bounded vector of
references through queues, pipelines, routing, and replication. Arbiters select
the entire lineage and pad shorter inputs with invalid slots. Transaction
validity requires every contributing join input; invalid padding is not a
missing contributor. Partially annotated join ancestry is rejected, as with
selection. An entirely unannotated ancestry establishes a new root event.

Fork/broadcast reconvergence can repeat an identical parent occurrence. Duplicate
DPI calls are allowed; the collector's edge set deduplicates them. Different
sequence numbers at the same site remain distinct parents. The DPI ABI and
manifest JSON format are unchanged. Direct `Join` instances and selective or
control-only joins need their own trace adapters; they are not inferred by name.

At a buffered broadcast, input acceptance captures the parent reference in a
shadow register. Each recipient's actual pending bit gates its copy; recipients
can consume independently over many cycles without losing their common parent.
On simultaneous last-recipient completion and replacement acceptance, outputs
use the old stored reference and only the register's next value changes. Reset
invalidates the stored reference. No shadow pending controller or synthetic
event is created. The original payload and handshake hardware is unchanged.

`EventInstrumentationConfig(clock_port, reset_port)` selects the top-level
`Clock` and synchronous `Reset` inputs (defaults: `"clock"`, `"reset"`). All
functional clock/reset signals in occurrences owning event sites or observed
trace controls must resolve to these inputs through direct wiring or casts.
Generated clocks and independent reset domains in those occurrences are
rejected; untouched opaque subtrees may retain private domains. Assert reset
for at least one sampled rising edge
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

## Runtime handoff

Generate a companion C++ header with `event_manifest_to_cpp(traced.manifest)`
from the **same** `instrument_events` result used to emit RTL. The descriptor
and fixed `rheg_*` DPI calls connect this compiler pass to the independent
[RHEG runtime and exporter](../../rheg/README.md). See that guide for linking,
manifest binding, reset epochs, timing, snapshots, and streaming export.

Instrumented designs are simulation artifacts because of their DPI calls;
keep using the original elaboration for synthesis.

Contributors should read [`DEVELOPING.md`](DEVELOPING.md) before extending
inference, hierarchy expansion, metadata propagation, or manifest generation.
