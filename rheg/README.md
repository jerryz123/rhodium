<!-- Documents RHEG collector, snapshot, timing, and C++ Perfetto export contracts. -->

# RHEG: Rhodium Hardware Event Graph

RHEG represents microarchitectural activity as a graph of event occurrences:
nodes record annotated transfers, and edges identify their nearest contributing
parents. The compiler derives possible dependencies from typed flow semantics
and instruments actual transfers; see the
[graph-model overview and diagram](../rhodium/event/README.md#event-graphs).

This package is the independent C++ collector and export library for that graph.
Its namespace is `rheg`, and DPI symbols use the `rheg_` prefix.
It does not depend on `flow` or compiler implementation modules; the
[compiler event pass](../rhodium/event/README.md) generates its descriptor and DPI calls.

For a ready-to-run pipeline trace, start with the
[SimpleSoC simulator](../sims/README.md#export-simplesoc-events-to-perfetto).
For a custom simulator, bind the [manifest](#validated-trace-snapshots) and
[timing](#optional-trace-timing), then choose [streaming or replay](#streaming-to-perfetto).
The [Perfetto display contract](#perfetto-display-and-queries) explains tracks,
slice names, timing, and queries.

## DPI runtime and visualization handoff

The collector requires C++17 and the standard library. Link
[`runtime/rheg.cc`](runtime/rheg.cc) into the simulator.
Its [header](runtime/rheg.h) defines the fixed ABI and `graph()` API:

- `rheg_node` records a site, sequence, cycle, and payload width.
- `rheg_payload` supplies zero-padded 32-bit words, least-significant
  word first, so arbitrary fixed-width packed payloads use the same ABI.
- `rheg_edge` records an exact parent/child reference pair.
- `rheg_reset` clears the graph on an asserted sampled reset.

The site number is the zero-based index into **the accompanying manifest's
`sites` array**, not a hash. Together with the per-site sequence it identifies
one concrete occurrence, even for repeated module definitions. Manifest IDs
retain the exact human-readable hierarchy path.

The collector tolerates node, payload, and edge callbacks in any order and
deduplicates edges. After the simulator has settled the sampled edge, call
`rheg::graph().json()` for deterministic output; incomplete callbacks
and duplicate identities are errors. The export uses decimal strings for
64-bit sequence/cycle values to avoid rounding in JavaScript. Join numeric
sites with the compiler manifest to recover labels, hierarchy,
source locations, and payload schemas. The collector itself has no visualizer
or runtime manifest parser; the optional Perfetto library provides snapshot parsing.

## Validated trace snapshots

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

The occurrence-only `graph().json()` is also available. Snapshots copy the
current epoch and require additional memory proportional to the retained graph.

## Named captures

Compiler manifests include an ordered `fields` schema for every site and
matching C++ `Manifest::fields` tables. Each field has a unique ASCII identifier
name, positive width, LSB offset, and encoding. Fields exactly cover the compact
capture in declaration order, most-significant first; omitted observations do
not occupy bits or generate callbacks. A zero-width site has an empty field list.

At a settled boundary, use `graph.field(ref, "pc")` or
`snapshot.field(ref, "pc")`. `FieldValue` provides width, encoding, LSW-first
words, lossless `hex()` and `decimal()`, and checked `unsigned_value()` /
`signed_value()` access for values up to 64 bits. `decimal()` interprets the sign
only for the `signed` encoding. `capture_field(node, field)` also works on a
cycle batch without requiring a retained graph.

Perfetto preserves capture names such as `pc` and `instruction`
(SQL keys `debug.pc`, etc.). The built-in names `cycle` and `sequence` are
reserved and rejected as capture names. Bitvectors default to fixed-width hex
strings. Booleans and integers use native scalar arguments; values wider than
64 bits, and unsigned values above INT64_MAX, use exact decimal strings.
Raw word arrays and aggregate widths are no longer normal display arguments.
An explicit raw capture appears as `raw`.

The schema is additive within version 1. Legacy snapshots without it retain
raw-word display, but cannot provide named lookup.
Mixed schema presence, duplicate names, overlaps, gaps, out-of-range fields,
invalid encodings, and JSON/C++ table mismatches are errors. The collector
validates typed descriptors without acquiring a JSON dependency; the shared
export library interprets the same schema for streaming and replay.

## Enum labels

Fields with `encoding: "enum"` carry a nonempty `symbols` array of
`{"value":"2","name":"ReadClean"}` entries. Values are exact unsigned decimal
strings; names and values must be unique and values must fit the field's width
(1–64 bits). The compiler supplies these tables from hardware enum declarations.
`Field::symbols` carries the matching numeric/string pairs in the C++ descriptor.

An explicit `label: true` selects one enum field per site as the Perfetto slice
name. Known values use the member name; unknown values use fixed-width hex.
Track names and numeric field arguments are unchanged. Enum labels take precedence
over instruction mnemonics; without selection, existing naming behavior remains.
Symbol tables live in static track descriptions, not repeated event arguments.
RHEG contains no CHI-specific opcode table and does not correlate transactions.

## Instruction disassembly

Fields with `encoding: "riscv"` include `isa` (an explicit RV32I/RV64I ISA
string) and `pc` (a same-site hex/unsigned capture alias of XLEN width).
Instructions may be 16 or 32 bits; a 32-bit capture can hold a zero-extended
compressed instruction. Unsupported widths, missing PC references, and malformed
ISA configurations are errors. This format is opt-in: a field merely named
`instruction` is still ordinary hex unless tagged.

The shared C++ exporter uses Spike's pinned disassembler to show assembly under
the original field name. It resolves relative branch/jump targets using the
associated PC, wrapping at XLEN; it does not resolve ELF symbols. Unknown or
unsupported encodings retain fixed-width hex. Disassembly is presentation, not
an architectural legality check. The graph and snapshot preserve the original
bits, available through the normal field accessors. Live and standalone exports
use the same formatter.

Without an explicit enum label, a site with exactly one `riscv` field names each
Perfetto slice with its disassembled mnemonic (including aliases such as `li`
and `j`), or raw hex for
an unknown encoding. The full assembly remains in the field argument. Track
names retain the site labels, such as `core.s1.fetch`; queries selecting stages
should join `slice.track_id` to `track.id`. Sites with no instruction field or
multiple instruction fields retain their site label as the slice name.

## Optional trace timing

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

An initial asserted reset preserves the supplied epoch ID. After a deasserted
reset callback or any occurrence callback, the next asserted reset increments
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

## Streaming to Perfetto

The optional [`rheg_perfetto`](perfetto/rheg_perfetto.h) C++
library writes native `.pftrace` packets as settled batches arrive. The same
encoder powers the standalone `rheg-perfetto` snapshot converter.
Build with CMake 3.20+ and a C++17 Clang/GCC compiler on macOS or Linux:

```sh
cmake -S rheg/perfetto -B /tmp/rhodium-perfetto-build
cmake --build /tmp/rhodium-perfetto-build -j 4
```

The exporter privately uses nlohmann JSON 3.12.0 (found locally or fetched) and
Spike's disassembler (fetched at a pinned revision). Downloads are checksum-pinned.
The Spike simulator and FESVR are not built. No Python, LLVM, external disassembler
process, Perfetto SDK, or protobuf runtime is required.

For offline builds, point CMake at extracted source trees:

| Dependency | CMake option | Test-script / simulator make variable |
|---|---|---|
| nlohmann JSON | `FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON` | `NLOHMANN_JSON_SOURCE_DIR` |
| Spike | `FETCHCONTENT_SOURCE_DIR_RHEG_SPIKE` | `RHEG_SPIKE_SOURCE_DIR` |

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

The encoder interns repeated names, categories, and captured strings, and omits
flow starts for sites with no possible outgoing dependency. These are lossless
encoding optimizations: displayed events, arguments, timing, and dependency
arrows are unchanged. Intern tables are bounded, with inline fallback for new
strings when full; cycle and sequence values remain exact inline decimal strings.

### Stream lifecycle and failures

The caller owns the output stream and must keep it alive for the writer's
lifetime. Choose a fresh output path: ordinary file opening and shell redirection
can overwrite an existing file. Each writer represents one reset epoch.
Each successfully written batch is flushed and leaves an importable prefix.
I/O failure can leave a partial final packet: treat the output as incomplete,
do not resume the same writer. Invalid batches do not advance writer state;
retain the batch returned by `finish_cycle` if retry is needed. A converter
error can leave partial stdout; discard that output. No recording service is
needed. This is incremental file generation, not a live-refresh connection to
the Perfetto UI.

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

## Perfetto display and queries

Each occurrence becomes a one-cycle slice spanning `[N, N+1)` on a track named
with its annotated event label, without a synthetic thread-ID suffix. Each site
retains a separate track even when labels repeat. These are non-thread tracks
grouped under a custom track named for the top-level design. This group requests
lexicographic child ordering, independent of site IDs or callback order; viewers
may override this display hint. Occurrence arguments contain only exact
cycle, sequence, and captured values (legacy snapshots retain their raw words).
Slice names normally match their site labels; a single tagged instruction uses
its [disassembled mnemonic](#instruction-disassembly) instead. An explicitly
selected [enum label](#enum-labels) takes precedence over both.
Frequency and epoch are emitted once before occurrences as trace metadata,
available in SQL's `metadata` table as `cr-rheg.clock_frequency_hz` and
`cr-rheg.epoch_id`, with exact decimal `str_value` values. Even an empty trace
or the first flushed prefix contains these values.

Each track's description is JSON containing `site_id`, `source_location`,
`payload_width`, and, for named captures, the ordered `fields` layout. The UI's
track description exposes this context without repeating it on every event.
SQL can read it through `EXTRACT_ARG(track.source_arg_set_id, 'description')`
and `json_extract`. Numeric site identity is
`EXTRACT_ARG(track.source_arg_set_id, 'trace_id') - 1`; sequence then identifies
the occurrence within that site and epoch.
Both boundaries use `floor(cycle * 1000000000 / frequency)` with integer
arithmetic and reject signed-64-bit nanosecond overflow before writing a batch.
Fractional nanoseconds are quantized independently without cumulative drift;
sub-nanosecond cycles can quantize to zero duration. This one-cycle display width
does not infer occupancy or time stalled between checkpoints.

Dependency arrows connect exact parent and child occurrences, including delayed
fanout and joins. Events within a batch are ordered by cycle and dependency;
same-cycle dependency cycles are rejected. The display width does not alter
graph lineage.

Query stage identity through the track, independently of each slice's mnemonic:

```sql
SELECT t.name AS stage, s.name AS mnemonic,
       EXTRACT_ARG(s.arg_set_id, 'debug.pc') AS pc,
       EXTRACT_ARG(s.arg_set_id, 'debug.instruction') AS instruction
FROM slice s JOIN track t ON t.id = s.track_id
WHERE t.name GLOB 'core.*';
```

## Scope and compatibility

The fixed `rheg_*` ABI and version-1 `rhodium-event-*` JSON identifiers remain
stable. Named-field schemas and optional timing extend version 1 additively.

The current collector supports one instrumented top per process on the
simulator thread. It retains the whole current epoch in memory. Instrumented
designs are simulation artifacts because of their DPI calls; keep using the
original elaboration for synthesis.

Contributors should read [`DEVELOPING.md`](DEVELOPING.md) before extending the
collector, trace formats, or Perfetto exporter.
