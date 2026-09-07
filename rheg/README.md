<!-- Documents RHEG collector, snapshot, timing, and C++ Perfetto export contracts. -->

# RHEG: Rhodium Hardware Event Graph

RHEG is the independent C++ runtime and export library for hardware event
graphs. Its namespace is `rheg`, and DPI symbols use the `rheg_` prefix.
Existing `rhodium-event-*` JSON format identifiers remain unchanged.
It does not depend on `flow` or compiler implementation modules; the
[compiler event pass](../rhodium/event/README.md) generates its descriptor and DPI calls.

## DPI runtime and visualization handoff

Link [`runtime/rheg.cc`](runtime/rheg.cc) into the simulator.
Its [header](runtime/rheg.h) defines the fixed ABI and `graph()` API:

- `rheg_node` records a site, sequence, cycle, and payload width.
- `rheg_payload` supplies zero-padded 32-bit words, least-significant
  word first, so arbitrary fixed-width packed payloads use the same ABI.
- `rheg_edge` records an exact parent/child reference pair.
- `rheg_reset` clears the graph on an asserted sampled reset.

The site number is the zero-based index into **the accompanying manifest's
`sites` array**, not a hash. Together with the per-site sequence it identifies
one concrete occurrence, even for repeated module definitions. Each hidden
reference is an ordinary packed record containing `valid`, `site`, and
`sequence`. Manifest IDs retain the exact human-readable hierarchy path.

The collector tolerates node, payload, and edge callbacks in any order and
deduplicates edges. After the simulator has settled the sampled edge, call
`rheg::graph().json()` for deterministic output; incomplete callbacks
and duplicate identities are errors. The export uses decimal strings for
64-bit sequence/cycle values to avoid rounding in JavaScript. Join numeric
sites with the compiler manifest to recover labels, hierarchy,
source locations, and payload schemas. The collector itself has no visualizer
or runtime manifest parser; the optional Perfetto library provides snapshot parsing.

### Validated trace snapshots

Generate a companion C++ header with
`event_manifest_to_cpp(traced.manifest)` from the **same** `instrument_events`
result used to emit RTL. Put `rheg/runtime/` on the C++ include path, include the
generated header, and bind its descriptor before evaluating the simulator:

```cpp
#include "my_trace_manifest.h"

rheg::graph().bind_manifest(rheg_generated::manifest());
// Evaluate the simulator, including its initial sampled reset.
// After all callbacks for an edge have settled:
const auto snapshot = rheg::graph().snapshot();
const auto trace_json = snapshot.json();
```

The generated header contains both the complete version-1 manifest JSON and
matching numeric site-width/dependency tables. It defines one
`rheg_generated::manifest()` function per instrumented top; do not
combine headers for different tops in one translation unit. The descriptor is
trusted compiler output, not an API for parsing arbitrary JSON. Binding copies
it, is permitted only once before any callback (including reset), and survives
reset. A consumer cannot attach or swap manifests after collecting a run.
Build systems must keep the generated header and RTL together: the unchanged
DPI ABI does not authenticate a wrong but structurally compatible descriptor.

With a bound manifest, validation also rejects unknown sites, mismatched payload
widths, edges outside the possible static dependency set, and parents whose
cycle is later than their child. Same-cycle edges and distinct occurrences of
one parent site are valid. Missing nodes and incomplete payload callbacks remain
permitted during collection, but must resolve before validation or snapshot.
These checks validate observed edges, not whether every required join input
emitted an edge; completeness remains enforced by the instrumented logic.

`snapshot()` requires a bound manifest and returns an owning, read-only copy
with `nodes()`, `edges()`, and `manifest()` accessors. Later callbacks and resets
do not change an existing snapshot. The call must run on the simulator thread
at a settled boundary; it is not a concurrent snapshot API. Its JSON is a
version-1 `rhodium-event-trace` object containing `manifest` and `occurrences`,
each retaining its own existing format and version. It preserves decimal-string
64-bit identities and deterministic ordering. Reset clears live occurrences,
not saved snapshots; never merge reset epochs by site/sequence alone.

The existing occurrence-only `graph().json()` and fixed DPI entry points remain
available. Snapshots copy the current epoch and therefore require additional
memory proportional to the retained graph.

### Optional trace timing

Before any simulator evaluation or callback, bind run timing separately from
the compiler manifest (the two bindings may occur in either order):

```cpp
rheg::graph().bind_timing(rheg::TraceTiming{100000000, 0});
```

`TraceTiming` holds a positive 64-bit `clock_frequency_hz` and a 64-bit starting
`epoch_id` (default zero). Binding copies the values and is allowed only once,
before any callback including reset. There is no assumed frequency default.
SoC integration should supply `SoCClockConfig.clock_frequency_hz`, not
`timebase_frequency_hz`; standalone integrations supply their own frequency.
The [SimpleSoC simulator](../sims/README.md#export-simplesoc-events-to-perfetto)
supplies this timing in its opt-in trace build; other integrations bind it explicitly.

`Snapshot::timing()` returns a const optional timing value. Untimed snapshots
remain supported and retain their existing JSON shape. Timed snapshots add an
optional `timing` object to the version-1 trace envelope, outside `manifest`:

```text
"timing": {"clock_frequency_hz": "100000000", "epoch_id": "0", "origin": "cycle-zero"}
```

Both integers use decimal strings to preserve all 64 bits. The fixed origin
means cycle zero maps to timestamp zero in each epoch; no wall-clock alignment
is implied. At 100 MHz, each cycle represents 10 ns. The collector keeps exact
cycles and does not perform timestamp conversion.

The runtime requires C++17. An initial asserted reset preserves the supplied
epoch ID. After a deasserted reset callback or any occurrence callback, the next asserted reset increments
the epoch once and clears live occurrences. Holding reset asserted does not
increment it repeatedly. Frequency survives reset, and old snapshots retain
their original epoch and data. Epoch exhaustion is an error before clearing
data, never wraparound. `clear()` only discards occurrences, not timing or epoch
state; it is not a substitute for the reset callback. Capture at a settled
boundary before asserting reset if the previous epoch must be retained. Epoch
IDs distinguish epochs within a run, not independently started simulations.
The existing RTL emits reset callbacks only while reset is asserted. Occurrence
callbacks distinguish nonempty epochs without harness changes. To count empty
epochs too, the harness must call `graph().reset(false)` after reset deassertion;
without that notification, empty intervals between asserted resets are
indistinguishable from continuously held reset and share an epoch ID.

### Streaming to Perfetto

The optional [`rheg_perfetto`](perfetto/rheg_perfetto.h) C++
library writes native `.pftrace` packets as settled batches arrive. The same
encoder powers the standalone `rheg-perfetto` snapshot converter.
Neither export nor its tests use Python. Build with CMake 3.20+ and a C++17
Clang/GCC compiler on macOS or Linux:

```sh
cmake -S rheg/perfetto -B /tmp/rhodium-perfetto-build
cmake --build /tmp/rhodium-perfetto-build -j 4
```

The build uses nlohmann JSON 3.12.0, found locally or fetched from a hash-pinned
archive. Offline builds may set `FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON` to its
extracted source directory. This private dependency parses the manifest once
and saved snapshots; the collector itself remains standard-library-only. Native
protobuf encoding requires neither the Perfetto SDK nor a protobuf runtime.

For integration, `add_subdirectory(rheg/perfetto)` and link the
`rheg_perfetto` CMake target. It links `rheg_runtime` transitively;
do not also compile another copy of the collector. After binding a manifest
and timing, begin a stream on an empty graph:

```cpp
#include "rheg_perfetto.h"
#include <fstream>

auto& events = rheg::graph();
const auto header = events.begin_stream();
std::ofstream output("trace.pftrace", std::ios::binary);
rheg::PerfettoWriter writer(output, header.manifest(), *header.timing());
// Evaluate the simulator and let all callbacks settle for event cycle N.
writer.write(events.finish_cycle(N)); // Repeat at settled boundaries.
events.end_stream(); // Only after the final batch has been delivered.
```

```sh
/tmp/rhodium-perfetto-build/rheg-perfetto snapshot.json > replay.pftrace
```

The binary reads a complete timed `rhodium-event-trace` snapshot emitted by
`snapshot().json()`, validates it, and writes binary Perfetto to stdout with
diagnostics on stderr. A library caller can use `read_event_trace(input)` and
`write_perfetto(output, snapshot)` for the same operation. It is a snapshot
postprocessor, not a parser for the optional cycle-batch JSON log. Streaming
passes typed batches directly to the writer, without JSON serialization or a
helper process. Streaming and replay use identical ordering and encoding.

The caller owns the output stream and must keep it alive for the writer's
lifetime. Choose a fresh output path: ordinary file opening and shell redirection
can overwrite an existing file. Each writer represents one reset epoch.
Each successfully written batch is flushed and leaves an importable prefix.
I/O failure can leave a partial final packet: treat the output as incomplete,
do not resume the same writer. Invalid batches do not advance writer state;
retain the batch returned by `finish_cycle` if retry is needed. A converter
error can leave partial stdout; discard that output. No recording service is
needed. This is incremental
file generation, not a live-refresh connection to the Perfetto UI.

`finish_cycle(N)` is a watermark: no later callback may supply a node at or
before N, a payload for a flushed node, or an edge to a flushed child. Watermarks
must strictly increase; the first may be zero and empty batches are allowed.
An old parent may acquire new children in later batches. Validation failures
leave the pending batch available for completion and retry. Batches own their
new nodes and edges. Capture mode still retains the complete in-memory graph
for snapshots; use callback APIs, not direct mutation of public graph containers,
while streaming. The exporter retains a compact identity/cycle index for the
epoch, so neither component claims bounded total memory.

End the stream before reset (except an initial held reset before activity),
then use a new header and output file for the next epoch. The simulator must
supply the instrumentation's event-cycle count, not an unrelated harness tick.
No DPI ABI or RTL changes are required for this explicit host boundary.

Each occurrence becomes a one-cycle slice spanning `[N, N+1)` on a track named
with its annotated event label, without a synthetic thread-ID suffix. Each site
retains a separate track even when labels repeat; the full site path remains in
the track description and event arguments. These are non-thread tracks grouped
under the top-level design. Payload words, exact cycle,
sequence, epoch, frequency, and source location are retained as arguments.
Both boundaries use `floor(cycle * 1000000000 / frequency)` with integer
arithmetic and reject signed-64-bit nanosecond overflow before writing a batch.
Fractional nanoseconds are quantized independently without cumulative drift;
sub-nanosecond cycles can quantize to zero duration. This one-cycle display width
does not infer occupancy or time stalled between checkpoints.

The native protobuf contains legacy `s`/`f` flow records enclosed by each
one-cycle occurrence slice. A source identity gets one flow start; each
child adds a non-closing flow end for each parent. Unlike modern flow steps,
this preserves the original source through delayed fanout and supports joins
without inventing sibling dependencies or predicting future edge IDs. Events
within a batch are ordered by cycle and dependency; same-cycle cycles in the
graph are rejected. This compatibility mapping is covered by Trace Processor
tests; it is deliberately isolated from the graph schema and collector.

The current collector supports one instrumented top per process on the
simulator thread. It retains the whole current epoch in memory. Instrumented
designs are simulation artifacts because of their DPI calls; keep using the
original elaboration for synthesis.

Contributors should read [`DEVELOPING.md`](DEVELOPING.md) before extending the
collector, trace formats, or Perfetto exporter.
