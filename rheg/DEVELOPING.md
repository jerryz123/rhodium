<!-- Defines RHEG runtime and Perfetto implementation ownership and focused validation. -->

# Developing RHEG

Read the public [`README.md`](README.md) for integration and trace contracts.
This package owns the C++ event collector, export library, standalone converter,
and their contract tests. It imports neither `flow` nor the Rhodium compiler.
The [compiler event pass](../rhodium/event/DEVELOPING.md) produces the manifest
descriptor and instrumented RTL consumed through the fixed DPI ABI.

## Architecture and ownership

- `runtime/` owns the fixed-width C ABI, order-independent graph storage,
  validation, and deterministic occurrence JSON.
- `perfetto/` owns the optional C++ encoder library and standalone snapshot
  converter. `PerfettoWriter` accepts typed cycle batches; `read_event_trace`
  parses saved snapshots and `write_perfetto` feeds the same encoder. The JSON
  parser is private to this component, never a collector dependency. Native
  wire encoding uses the small documented field subset of the Perfetto v58.2
  schema; verify field changes with the actual native importer, not just a
  matching homegrown decoder. CMake pins the JSON dependency's archive digest.
- `tests/` owns standalone collector, encoder, parser, and native importer
  contracts. Compiler/RTL integration fixtures stay in `tests/backend/`.

Keep the runtime standard-library-only and the JSON parser private to the
optional exporter. Preserve namespace `rheg`, the `rheg_*` DPI ABI, and existing
`rhodium-event-*` JSON format identifiers across packaging changes.

## Focused validation

The runtime binds trusted compiler descriptors before the first callback and
retains that binding across reset. Keep the DPI ABI independent of manifest
loading. `Graph::validate` checks settled completeness and, when bound, site
widths and allowed static edges. Cycle order allows equality. Do not require
all possible static parents: arbitration selects a subset dynamically.
`Snapshot` owns a validated graph copy, exposes only const views, and embeds
the bound manifest in its JSON export. It has no visualization dependencies.
Run timing belongs to the collector/snapshot envelope, not `EventManifest` or
instrumentation configuration. Store an owned optional timing value; copying a
graph into a snapshot also freezes its epoch. Track whether an epoch has run
since the last asserted reset so reset held over several cycles advances only
once. Check epoch exhaustion before clearing data. The standalone collector
test owns binding-order, missing/invalid timing, exact JSON integers, initial
and repeated reset, empty epochs, overflow, and saved-timing coverage.
Streaming adds pending-reference and pending-edge sets only while explicitly
enabled. `finish_cycle` validates the delta against retained parent nodes and
the same manifest checks used by full snapshots; it does not scan historical
payloads each cycle. Clear pending entries only after the batch has been built
and validated. Reject callbacks that could mutate an already emitted child.
Snapshots remain capture-mode objects; bounded-memory collection is separate.

`TRACE_PROCESSOR=/path/to/trace_processor_shell bash rheg/tests/run-event-perfetto.sh`
builds and runs C++ contracts, compares live output byte-for-byte with standalone
replay, and queries every emitted prefix using the native Trace Processor CLI.
No Python package, Python launcher, or local RPC server is used. Supply an
official native executable (verified with v58.2), not its Python download wrapper.
For offline JSON dependency resolution, set `NLOHMANN_JSON_SOURCE_DIR` to an
extracted 3.12.0 source tree. The script reports the tested processor version.
The producer emits delayed fanout and a same-cycle join with reversed site
ordering. Native SQL checks cover timestamps/durations, edges, identities, and
parser diagnostics, explicit track labels, and complete one-cycle slices in
every flushed prefix. Use named non-thread tracks under the top-level process
and disable sibling merging to preserve distinct sites with repeated labels.
Native tests must verify legacy flow attachment on these tracks as well as
the absence of synthetic thread association (which adds numeric UI suffixes).
Use wide arithmetic for the N+1 boundary and validate it before output; test
adjacent slices and fractional clock periods as well as end-only overflow.
C++ tests cover invalid batches, strict JSON parsing,
quantization, overflow, empty traces, and poisoned output streams. The snapshot
parser rejects duplicate keys, excessive nesting, fractional/negative integer
fields, overflow, incomplete nodes, and manifest-invalid edges. Converter errors
are nonzero exits with diagnostics on stderr; stdout may be partial on failure.
Keep generated traces and downloaded binaries out of version control.

`bash rheg/tests/run-event-collector.sh` exercises the collector without
CIRCT/Verilator, including all 120 permutations of a small callback set,
incomplete data, binding misuse, payload errors, cycle order, duplicate edges,
same-site distinct parents, large identities, and snapshot lifetime across reset.

Run Racket commands (including the collector script's JSON check) with a newly
created `PLTCOMPILEDROOTS`, following the repository `AGENTS.md`:

```sh
export PLTCOMPILEDROOTS="$(mktemp -d /tmp/rheg-tests.XXXXXX)"
bash rheg/tests/run-event-collector.sh
TRACE_PROCESSOR=/path/to/trace_processor_shell bash rheg/tests/run-event-perfetto.sh
```

After ABI or generated-descriptor changes, also run the compiler/RTL integration
fixtures described in the [compiler validation guide](../rhodium/event/DEVELOPING.md#focused-validation).
Run `make check-boundaries` and `bash tools/check-ci-changes.sh` after package
moves or dependency changes. Generated traces and build directories stay out
of version control.
