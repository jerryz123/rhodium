<!-- Describes Rhodium's static compiler-inferred event dependency manifests. -->

# Static event graphs

Use `rhodium/event` to infer possible direct dependencies between event sites
annotated on ready-valid flow topology. This first implementation is a
read-only compiler analysis: it does not insert metadata hardware or DPI calls.

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
- No synthesizable lineage records, shadow state, DPI calls, or C++ graph
  runtime are inserted yet.
- Only flat top-level flow endpoints are traceable; nested interface members
  are rejected.
- Source locations are retained when an annotation supplies one through the
  low-level interface API. The standard flow helpers currently report
  `<unknown>` pending call-site location capture.
- Terminal metadata is recorded but does not yet prune downstream analysis.
- Stateful correctness beyond possible in-order routes is deferred to the
  instrumentation phase.

Contributors should read [`DEVELOPING.md`](DEVELOPING.md) before extending the
model, hierarchy expansion, trace metadata, or JSON format.
