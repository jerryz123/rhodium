<!-- Defines RHEG collector invariants, Perfetto encoding, decoder ownership, and validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

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
| `rhodium/event/tests/circt/` | Compiler/RTL integration fixtures, owned by the event package |

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

`record_unknown`/`rheg_unknown` may arrive before or after the node callback,
but never after that occurrence has streamed. Preserve the optional marker in
snapshots and batches, reject duplicates and orphan markers, and omit it from
JSON when false for legacy compatibility. The exporter validates its boolean
JSON type, displays it only when true, and treats changes as stall-run boundaries.
Missing-contract details are static track context, not duplicated per occurrence.

## Instance registrations

Instance scopes are optional typed manifest tables cross-checked against JSON.
Validate unique scope paths, consistent ancestor chains, widths, and site
membership. Keep registrations in epoch-owned maps, not nodes. Streaming tracks
only pending registrations; validate dependent nodes at settlement, allowing
arbitrary same-cycle callback order. Snapshot copies freeze registrations;
reset clears them and `clear()` preserves them.

The additive `rheg_instance` ABI records semantic hierarchy identity, not
presentation-only payload. The compiler owns stable-after-first-use assertions.
The exporter preallocates occurrence-qualified group UUIDs and updates scope
descriptors at registration before their first event. Equal runtime IDs must not
merge scopes. Include registrations in live/replay chronological ordering and
stage them with each batch so rejected batches write no bytes or partial state.
Test byte parity and native imported names, ancestry, and distinct track IDs.

## Residency updates

Keep releases separate from node creation: `Node::end_cycle` is optional, and
`CycleBatch::ends` carries updates to previously streamed identities. Validate
release type, ordering, identity, and watermark without rescanning old nodes.
Captures/parents remain immutable. Parse and cross-check the manifest's residency
indices just as capture schemas; preserve optional ends in snapshot JSON.
The encoder merges starts and ends in chronological order, ending old owners
before same-cycle replacement, while retaining its same-cycle topological start
ordering. Keep graph admission/release cycles unchanged; project both Perfetto
residency boundaries one cycle later to show registered occupancy, including
same-edge replacement. Stage the active-owner map with the batch and reject
overlap before output. Never infer release from child activity, absence, or finalization.
Open residency slices intentionally stay incomplete at export end. Test live/
replay/gzip parity, alternate modes, old-owner descendants, malformed releases,
and native durations/flow attachment.

## Perfetto encoding

`PerfettoWriter` accepts typed settled batches. `read_event_trace` parses saved
snapshots; `write_perfetto` feeds the same writer. Keep JSON private to the exporter.
The parser rejects duplicate keys, excessive nesting, invalid integer forms,
overflow, incomplete nodes, and manifest-invalid edges.

Encode the documented field subset of the Perfetto v58.2 native schema. Run
timing and epoch go once into ChromeEventBundle metadata before descriptors or
occurrences. Site identity, source location, and capture layout go in the JSON
TrackDescriptor description; v58.2 has no arbitrary track annotation field.
Occurrence arguments contain exact cycle, sequence, and captured values.
Keep exact graph identities and interval bookkeeping internal to the exporter;
Perfetto is a visualization projection, not a second graph serialization.

Keep transfer/stall classification and `observation_of` in static site JSON
and track descriptions, not the DPI ABI or occurrence arguments. Missing kind
means transfer for legacy manifests. Validate that a stall observes a transfer
site and has no outgoing dependency. Resolve observation-to-transfer track IDs
after reading every site, independently of manifest order. Emit descriptors only
for transfers, with complete companion schemas in an `observations` array.
Reject same-cycle collisions on
shared tracks before writing output or advancing writer state. Stall names are
always `stall`, regardless of instruction or enum captures. Coalesce only in the
exporter: retain one open run per track, with captured words, exact parent set,
and the last reference. Extend only consecutive cycles and sequence numbers
at the same observation site with equal words and parents. Close on a mismatch,
settled absence, transfer, or finalization. Emit one incoming arrow per parent
per run; never replace the original transfer source with an observer.
Emit begins immediately and close with a timestamped end, without range
annotations. A raw prefix may contain open stalls. Preflight the complete input
batch, stage run-state changes, and commit only after successful output. Decide
run closures for each entire cycle before its begins, in end-cycle/track order,
so packet and intern ordering do not depend on batch partitioning.

Intern categories, event names, annotation names, and captured string values in
separate sequence-local IID tables. Define each string in the first packet that
references it; clear incremental state once at the start and mark event packets
as requiring it. Stage dictionary additions per batch and commit only after a
successful output flush. IID assignment follows event order, not batch boundaries,
so uncompressed live/replay bytes and flushed-prefix importability stay identical.
Each table admits at most 4096 entries and 1 MiB of string content, with a 1024-byte
per-string limit. Keep existing IDs valid and fall back to inline encoding when
admission is exhausted; never reuse an IID. Unique cycle/sequence strings stay
inline. These limits bound dictionary memory, not whole-epoch graph retention.

Use non-thread tracks under a custom design group with
`child_ordering = LEXICOGRAPHIC`; process/thread descriptors ignore that hint.
Disable Perfetto's own sibling merging so only our scoped grouping combines
tracks. Parse explicit label
paths once while describing the manifest, rejecting empty slash-separated segments
before output. Keep hardware-ID fallbacks flat. Deduplicate transfer-track groups
by full prefix, allocate their UUIDs above the site/root range in lexical order,
and emit parent-first custom descriptors with lexicographic child ordering.
Groups carry no occurrences; group/leaf name collisions must not alias UUIDs.
Resolve observers through `track_sites`, never their own display path. Keep full
labels in static track descriptions; track names contain only the leaf.
Resolve explicit `PerfettoTrackGroups` first. Among remaining sites, merge
identical complete labels and kinds when their hardware scopes are the same or
nested; sibling instances stay distinct. Choose the lowest site index as each
group's stable UUID representative, then attach all companions. Neither route
changes the manifest or collector.
Keep display overrides separate from site labels and schemas. Group option
order must not affect bytes; unknown or multiply assigned sites must fail before
output. A grouped descriptor lists complete original descriptions in `sites`,
not a representative schema. Reuse the same-cycle occupancy check and
site-qualified stall continuation rules for all members. Keep core-specific
selection policy out of this library; the caller supplies the mapping explicitly.
Default slice names use only the final dot-separated component of that leaf;
retain the whole leaf if that component is empty. Apply this only to the site-label
fallback, never to enum symbols or dotted instruction mnemonics. An explicit enum label field uses the compiler-supplied symbol table,
with fixed-width hex for unknown values and unchanged numeric capture arguments.
Validate unique fitting symbol values/names and at most one selected label; include
both symbols and selection in JSON/C++ descriptor equality. Never duplicate domain
opcode tables in RHEG or infer label selection from a field's name. Without an
explicit label, only a site with exactly one `riscv` field gets mnemonic slice names.
Use the full formatted field as the argument and its first token as the name;
unknown hex and ambiguous multi-instruction sites follow the README fallback.

Legacy `s`/`f` flow records sit inside each transfer or stall-run slice. Give each source
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
to prevent drift. Each successful uncompressed batch flush must leave an importable prefix.
Invalid input must not advance writer state; an I/O-poisoned writer is not
resumable. Converter diagnostics go to stderr and failures return nonzero;
stdout may already contain a partial trace.

Optional gzip belongs exclusively to the exporter, using system zlib behind the
private implementation. Keep one deflate dictionary across batches, feed bounded
input chunks, and drain output into fixed scratch storage. Do not sync-flush each
cycle: the native reader requires a final gzip footer anyway. `finish()` writes
pending stall ends and that footer, flushes and checks output, and is idempotent;
a write after finishing is invalid. Destructors release zlib state without hiding finalization failures.
Constructor, batch-write, and finalization failures must release resources, and
any compression/I/O failure poisons the writer. Raw mode retains importable
per-batch prefixes; gzip mode requires explicit finalization before native import.
Compare decompressed live/replay bytes, not their compression block boundaries.

## Instruction decoder

Validate `riscv` capture widths and PC references in the standard-library-only
collector. Validate full ISA configurations when constructing the exporter,
before writing any bytes. Disassembly is presentation, not legality validation.

Keep Spike headers private. The repository gitlink pins its source, and the
Perfetto Makefile builds the three disassembler sources into the archive, not the
simulator or FESVR.
The ordered patch series under
[`../riscv/riscv-isa-sim-patches/`](../riscv/riscv-isa-sim-patches/)
replaces the parser's two abort sites with exceptions and registers opcode-free
properties absent from the pinned parser. The shared
[`patched_submodule.py`](../riscv/patched_submodule.py) tool applies it to a
build-local copy; leave the pinned submodule and its
license unchanged.
When advancing Spike, remove
upstreamed patches and rebase the remainder against the new pin. Keep the full
ISA in trace metadata; do not discard unknown extension tokens. No subprocess or
simulator state is involved.

Own decoder instances per ISA per writer. Cache by ISA, PC, bits, and capture
width; clear the bounded cache at 4096 entries. Resolve only the full `pc + ` or
`pc - ` operand syntax, not a mnemonic suffix such as `auipc`. Wrap targets at
XLEN, normalize spacing, and retain raw hex for unknown/unsupported encodings.
No symbol lookup or graph mutation belongs here.

## Focused validation

Build/test commands run from the repository root. The collector script's Racket
JSON check uses the persistent worktree-specific cache through the repository
wrapper, following [AGENTS.md](../AGENTS.md#verification):

```sh
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
| Native display | One-cycle durations, fractional periods, N+1 overflow, track hierarchy/order, scoped same-name merging without thread association, flow attachment, metadata even in empty traces and no parser errors |
| Stall intervals | Stable-run coalescing across batch partitions; capture, parent-set, sequence, gap, transfer and finalization boundaries; open prefixes, exact durations and UINT64_MAX; graph preservation and collapsed parent arrows |
| Shared tracks | Exact-site grouping, alternate modes and schemas, two instances, observer-site switches with equal captures, unchanged flow endpoints, collision atomicity, invalid configurations, and live/replay/gzip/CLI parity |
| Disassembly | RV32/RV64, compressed/FP/CSR instructions, PC-relative targets and wraparound, `auipc`, unknown fallbacks, explicit aliases, ordinary fields named instruction, multi-instruction fallback and live/replay parity |
| Failure handling | Strict JSON rejection, invalid batches, poisoned output streams, nonzero converter errors and empty/malformed inputs |
| Compression | Gzip round-trip equality, live/replay import, multi-buffer incremental output, empty traces/batches, finalization and poisoned write/footer failures |

Use the real native importer for wire-format changes, not just a matching local
decoder. After ABI or descriptor changes, run the
[compiler/RTL fixtures](../rhodium/event/DEVELOPING.md#focused-validation). For
simulator integration, run the [trace smoke](../sims/DEVELOPING.md#event-export-integration).
Run `make check-boundaries` and `bash tools/check-ci-changes.sh` after package or
dependency changes. Keep traces, downloaded tools, and build artifacts untracked.
