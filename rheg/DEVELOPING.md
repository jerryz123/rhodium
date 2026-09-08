<!-- Defines RHEG collector invariants, Perfetto encoding, decoder ownership, and validation. -->

# Developing RHEG

Read [README.md](README.md) for integration, schema, timing, and export contracts.
RHEG imports neither `flow` nor the Rhodium compiler. The
[event compiler](../rhodium/event/DEVELOPING.md) produces the matching descriptor
and ordinary RTL/DPI instrumentation.

## Architecture and ownership

| Component | Responsibility |
|---|---|
| `runtime/` | Fixed C ABI, order-independent collection, validation, snapshots and occurrence JSON |
| `perfetto/` | C++ native encoder, private JSON parser and instruction decoder, standalone converter |
| `tests/` | Collector, exporter, parser, and native importer contracts |
| `tests/backend/` | Compiler/RTL integration fixtures, owned outside RHEG |

Keep the collector standard-library-only. Preserve namespace `rheg`, the fixed
`rheg_*` ABI, and `rhodium-event-*` format identifiers. Presentation changes must
not change graph storage or DPI packing. Build generated headers and RTL from
the same instrumentation result; descriptor binding is not an authenticity check.

## Collector and snapshot invariants

Bind an owned copy of the trusted compiler descriptor before callbacks and retain
it across reset. `Graph::validate` checks settled completeness and, when bound,
site widths and allowed static edges. Parent cycles may equal child cycles.
Validate observed parents, not every possible static parent: selection chooses
a subset dynamically, while instrumented logic enforces join completeness.

Keep capture words compact in nodes; resolve fields through immutable site
schema. Width, offset, overlap, gap, encoding, and named-field extraction checks
belong in the collector. Never infer captures from type-description strings.
The exporter parses and cross-checks JSON against typed tables once, then reuses
the collector extractor. Preserve legacy no-schema snapshots.

`Snapshot` owns a validated graph copy with const views and its bound manifest;
it has no visualization dependency. Optional timing belongs in the trace
envelope, not `EventManifest` or instrumentation configuration. Snapshot copying
freezes timing and epoch too. Track whether activity or a deassertion notification
occurred since reset, so held reset advances the epoch only once. Check epoch
exhaustion before clearing state. Preserve the public distinction between
`clear()` and reset, including notification of otherwise empty epochs.

Streaming adds pending-reference and pending-edge sets only while enabled.
`finish_cycle` validates the delta against retained parents and the same manifest
rules as snapshots; do not rescan historical payloads per cycle. Clear pending
entries only after constructing and validating the batch. Reject callbacks that
could mutate an emitted child. Full-epoch retention and snapshot copying remain
intentional; bounded-memory collection is separate work.

## Perfetto encoding

`PerfettoWriter` accepts typed settled batches. `read_event_trace` parses saved
snapshots; `write_perfetto` feeds the same writer. Keep JSON private to the exporter.
The parser rejects duplicate keys, excessive nesting, invalid integer forms,
overflow, incomplete nodes, and manifest-invalid edges.

Encode the documented field subset of the Perfetto v58.2 native schema. Run
timing and epoch go once into ChromeEventBundle metadata before descriptors or
occurrences. Site identity, source location, and capture layout go in the JSON
TrackDescriptor description; v58.2 has no arbitrary track annotation field.
Occurrence arguments contain only exact cycle, sequence, and captured values.

Intern categories, event names, annotation names, and captured string values in
separate sequence-local IID tables. Define each string in the first packet that
references it; clear incremental state once at the start and mark event packets
as requiring it. Stage dictionary additions per batch and commit only after a
successful output flush. IID assignment follows event order, not batch boundaries,
so live/replay bytes and flushed-prefix importability stay identical.
Each table admits at most 4096 entries and 1 MiB of string content, with a 1024-byte
per-string limit. Keep existing IDs valid and fall back to inline encoding when
admission is exhausted; never reuse an IID. Unique cycle/sequence strings stay
inline. These limits bound dictionary memory, not whole-epoch graph retention.

Use non-thread tracks under a custom design group with
`child_ordering = LEXICOGRAPHIC`; process/thread descriptors ignore that hint.
Disable sibling merging to keep repeated labels distinct. Track labels remain
site labels. An explicit enum label field uses the compiler-supplied symbol table,
with fixed-width hex for unknown values and unchanged numeric capture arguments.
Validate unique fitting symbol values/names and at most one selected label; include
both symbols and selection in JSON/C++ descriptor equality. Never duplicate domain
opcode tables in RHEG or infer label selection from a field's name. Without an
explicit label, only a site with exactly one `riscv` field gets mnemonic slice names.
Use the full formatted field as the argument and its first token as the name;
unknown hex and ambiguous multi-instruction sites follow the README fallback.

Legacy `s`/`f` flow records sit inside each occurrence slice. Give each source
identity one flow start if its site has any outgoing static dependency, and each
child a non-closing end per parent. Omit starts only for statically terminal
sites; retain them for possible sources even when no child is currently known,
including same-site dependencies. This
preserves the original source through delayed fanout and supports joins without
introducing sibling dependencies or predicting future edges. Modern flow-step
semantics are not an interchangeable encoding. Topologically order same-cycle
events and reject cyclic dependencies before writing the batch.

Use wide integer arithmetic for both cycle boundaries, especially N+1; validate
signed-64-bit nanosecond overflow before output. Quantize boundaries independently
to prevent drift. Each successful batch flush must leave an importable prefix.
Invalid input must not advance writer state; an I/O-poisoned writer is not
resumable. Converter diagnostics go to stderr and failures return nonzero;
stdout may already contain a partial trace.

## Instruction decoder

Validate `riscv` capture widths and PC references in the standard-library-only
collector. Validate full ISA configurations when constructing the exporter,
before writing any bytes. Disassembly is presentation, not legality validation.

Keep Spike headers private. CMake checksum-pins its source and builds the three
disassembler sources into the Perfetto archive, not the simulator or FESVR.
The build-local ISA parser replaces its two abort sites with exceptions and
registers the opcode-free `zic64b` cache-block property, absent from the pinned
parser. Verify the adaptation sites and leave downloaded sources and licenses
unchanged. Keep the full ISA in trace metadata; do not discard unknown extension
tokens. No subprocess or simulator state is involved.

Own decoder instances per ISA per writer. Cache by ISA, PC, bits, and capture
width; clear the bounded cache at 4096 entries. Resolve only the full `pc + ` or
`pc - ` operand syntax, not a mnemonic suffix such as `auipc`. Wrap targets at
XLEN, normalize spacing, and retain raw hex for unknown/unsupported encodings.
No symbol lookup or graph mutation belongs here.

## Focused validation

Build/test commands run from the repository root. Use a fresh compiled root for
the collector script's Racket JSON check, following
[AGENTS.md](../AGENTS.md#verification):

```sh
export PLTCOMPILEDROOTS="$(mktemp -d /tmp/rheg-tests.XXXXXX)"
bash rheg/tests/run-event-collector.sh
TRACE_PROCESSOR=/path/to/native/trace_processor_shell bash rheg/tests/run-event-perfetto.sh
```

Use an official native Trace Processor executable, not its Python download
wrapper (verified with v58.2). The runner reports its version. Offline dependency
variables are listed in the [build guide](README.md#streaming-to-perfetto).
No Python package, launcher, or RPC server participates in these tests.

| Boundary | Required evidence |
|---|---|
| Collector callbacks | All 120 order permutations, incomplete data, binding misuse, payload errors, cycle order, edge deduplication and same-site distinct parents |
| Captures | Unaligned fields, bool/signed/unsigned values, a 65-bit decimal value, malformed schemas and exact raw preservation |
| Snapshots and timing | Binding order, missing/invalid timing, exact 64-bit JSON values, immutable copies, initial/held/empty reset epochs and exhaustion |
| Streaming and replay | Byte-identical output, watermarks, every flushed prefix, delayed fanout and same-cycle joins with reversed site ordering; interning across batches and capacity fallback; terminal-start omission and possible-source retention |
| Native display | One-cycle durations, fractional periods, N+1 overflow, track hierarchy/order, repeated labels without thread association, flow attachment, metadata even in empty traces and no parser errors |
| Disassembly | RV32/RV64, compressed/FP/CSR instructions, PC-relative targets and wraparound, `auipc`, unknown fallbacks, explicit aliases, ordinary fields named instruction, multi-instruction fallback and live/replay parity |
| Failure handling | Strict JSON rejection, invalid batches, poisoned output streams, nonzero converter errors and empty/malformed inputs |

Use the real native importer for wire-format changes, not just a matching local
decoder. After ABI or descriptor changes, run the
[compiler/RTL fixtures](../rhodium/event/DEVELOPING.md#focused-validation). For
simulator integration, run the [trace smoke](../sims/DEVELOPING.md#event-export-integration).
Run `make check-boundaries` and `bash tools/check-ci-changes.sh` after package or
dependency changes. Keep traces, downloaded tools, and build artifacts untracked.
