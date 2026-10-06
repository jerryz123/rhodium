<!-- Guides maintenance of target-specific RTL emission and its semantic regression suites. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing hardware backends

Read [README.md](README.md) for imports, supported operations, representation,
DPI ABI, and deliberate limits. This guide owns implementation boundaries,
extension workflow, and validation. The [package graph](../DEVELOPING.md) owns
allowed imports; [test maintenance](../../tools/testing/DEVELOPING.md) owns CI
and generated-artifact policy.

## Architecture and ownership

`compile_program` selects a target. Each backend uses `RTLTarget`: ordinary
preparation calls the shared `prepare_rtl` helper to obtain a fresh, verified
reachable graph and manifest, then constructs its emission plan. The same
target's `.plan(prepared)` accepts an existing `PreparedRTL` without preparing
again, for composition inside another target. Both paths return the backend's
artifact set: one RTL artifact, the rsim C++ model (with optional support header), or
that model with an SV/DPI binding.
Compilation owns requests/results; lowering owns copying and portable
expansion; each backend owns its representation. Frontend and Flow libraries
need no backend-specific branches.

The concrete emitters consume public core IR and cannot import frontend,
compilation, or analysis policy. Only target adapters import compilation.
`emit_circt`, `emit_module_circt`, and `emit_verilog` are internal implementation
utilities for targets and focused backend tests. Production drivers use an
explicit target and top through `compile_program`; there is no whole-inventory
program compilation API.

CIRCT emission produces MLIR; the external CIRCT pipeline owns optimization,
sequential/memory lowering, and SV export. Direct emission owns the complete SV
artifact without loading CIRCT. Keep representation helpers backend-local unless
a shared semantic responsibility actually belongs in core.

## Implementation map

| File | Responsibility |
|---|---|
| `circt-target.rhm`, `verilog-target.rhm` | RTL targets with shared ordinary/prepared plan construction |
| `circt.rhm` | CIRCT types, aliases, operation lowering, and textual MLIR |
| `verilog.rhm` | Opcode inventory, packed types, names, nets, state, and SV rendering |
| `rsim-target.rhm` | Standalone and SV-binding targets sharing one prepared model plan |
| `rsim/plan.rhm` | Recursive type capability checks, occurrence bindings, dependency schedule, register/memory sinks, per-occurrence assertions, and scalar foreign calls |
| `rsim/array-updates.rhm` | Exact recovery of single-index array updates and unused-value pruning |
| `rsim/cse.rhm` | Exact scalar expression sharing and complete schedule-value remapping |
| `rsim/evaluation.rhm` | Pre-edge/output evaluation bodies, materialized constants, value/storage dependencies, and ordered final consumers |
| `rsim/regions.rhm` | Contiguous cost-based regions, exact value boundaries, shared constant references, and current-storage reads |
| `rsim/layout.rhm` | Structured backing references, aggregate borrowing, and typed scratch allocation for each evaluation body |
| `rsim/arrays.rhm` | Element, fill, and affine-gather runs for existing vector constructors |
| `rsim/types.rhm` | C++ structs/arrays, per-artifact type interning, leaf normalization, canonical packing casts, typed masked merges, and decode comparisons/constants |
| `rsim/emit.rhm` | C++ rendering of expressions, storage and array plans, bounded helpers, evaluation frames, and simultaneous commits |
| `rsim/rsim-bits.hpp` | Portable wide scalar carrier and explicit bit conversions |
| `rsim/sv-binding.rhm` | Scalar boundary validation, state-only output dependencies, SV wrapper, and C++ DPI bridge |
| `tests/rsim/` | Builder fixtures, standalone native harness, integer/state oracles, and optional direct-SV comparison |
| `tests/*-test.rhm` | Host contracts, prepared graph reuse, invalid uses, determinism, and source preservation |
| `tests/verilog/fixtures.rhm` | Builder-owned semantic fixtures shared by both backends |
| `tests/verilog/emit-{direct,circt}.rhm` | Explicit-target fixture emission |
| `tests/verilog/support.py` | Tool execution, logs, and packed stimulus tables |
| `tests/verilog/scalar_cases.py`, `state_cases.py` | Independent integer/state oracles |
| `tests/verilog/rtl.py` | Combined scalar, aggregate, hierarchy, and state bench |
| `tests/verilog/memory.py` | Synchronous-memory oracle and isolated per-width simulations |
| `tests/verilog/assertions.py`, `dpi.py` | Assertion and foreign-effect families |
| `tests/verilog/run.py` | Family selection and optional CIRCT comparison |
| `tests/verilog/run-integration.py` | Existing SyncRam and UART benches/native model |

### Representation invariants

Rsim keeps graph scheduling separate from C++ rendering. Its schedule is a
backend-private representation, not another public hardware IR. Resolve wire
and hierarchy bindings before scheduling typed dependencies. Forward static
record/vector projections through their constructors without scheduling sibling
fields; otherwise legal field-wise feedback appears as a whole-bundle cycle.
Carry the requested field/index path through lookup and one-hot muxes: schedule
the whole selector and the same projection from every branch, following core's
direct dependency semantics. Memoize by occurrence, source value, and projection;
whole-value requests remain distinct. Other computed aggregates retain whole
execution followed by typed projection, so packed casts/extracts do not yet gain
bit-range dependency precision. Register reads terminate dependency walks;
next-state expressions are separate sinks. Allocate
register slots per occurrence, even when preparation reuses a module definition.
Retain occurrence paths for diagnostics and emitted state comments. Scheduling
maps and storage descriptors belong to one compilation, never an elaboration cache.
Verified `cdc.sync_level` metadata has no separate simulation action: keep its
ordinary register stages and apply the same root-clock checks to them.
Steps, ports, and state descriptors retain hardware types; only scalar operations
query a scalar width.

Both rsim targets apply `share_scalar_expressions` after scheduling and before
binding validation or evaluation planning. It retains the first matching pure
scalar expression in dependency order, matching opcode, ordered remapped operands,
semantic attributes, and hardware type equality. Only the diagnostic value `name`
is excluded from attribute comparison; the retained step keeps its original
attributes. Width is only a lookup bucket; nominal
types with equal widths or descriptions remain distinct. Static scalar projections
may share their aggregate source; other admitted expressions require scalar
operands. Mutable roots, aggregate computations, and partial operations remain
distinct. This pass performs no algebraic rewriting or cross-evaluation caching.

After scalar CSE, `compact_array_updates` recognizes a complete vector whose
lanes are `index == lane ? replacement : base[lane]`. All lanes must use the
same base, index, and replacement, with exact lane constants and original types.
Wrapped constants or mismatched lanes remain ordinary computations. Equality may
place the constant on either side; indices remain at most 64 bits. The pass uses
the existing `rtl.vector_inject` schedule operation, then removes unused value
producers in dependency order. Inputs, outputs, state sinks, every assertion, and
foreign/memory operands remain roots; shared lane consumers and all resources
survive. No high-level library or core IR changes are required.

Region and storage planning run only after this rewrite. Array injection copies
the base once into a fresh local or unique scratch destination, then conditionally
replaces the selected lane. The destination cannot alias an earlier operand;
out-of-range indices retain the original array as before. Current state and
constants remain immutable, and sinks still copy into the inactive bank/frame.

The returned schedule remaps ports, register next/reset values, memory read/write
controls and data, assertions, and foreign arguments/enables. Resource indices,
occurrence paths, labels, locations, and effect ordering do not change. Derive
evaluation bodies, dependencies, regions, and storage from this returned schedule;
never carry old step indices across the rewrite. The emitter renders its supplied
schedule without another optimization pass. The forced-region fixture target
uses the same two passes before applying its smaller partition budget.

`RsimSchedule` checks its immutable step list at construction; `emit_rsim` checks
the incoming schedule. Private renderer helpers receive that same list and use
the static-only `:~ List.of(RsimStep)` annotation. A checked `:: List.of(...)`
parameter would traverse the whole schedule on every per-step call, making
emission quadratic. Preserve the checked entry boundary when adding callers;
do not extend this trust to unrelated lists or externally supplied values.

`build_evaluation_plan` derives the two evaluation bodies from the existing
schedule without preparing or copying RTL. Each body references original step
indices and ordered final consumers. The dependency inventory separates value
operands from current-storage reads: registers, synchronous-read results, held
foreign results, and asynchronous memory contents. Input steps remain explicit
value roots. Memory resources never pull in writers. Final consumers also record
sink-only uses, including reset operands, read/write controls, masked-write old
words, assertion predicates, call arguments, and every held foreign result even
when unused. Storage dependencies are conservative; they do not permit caching
or skipping an effect. Both bodies sample afresh and can observe different state.
When adding a storage-reading step or a pre-edge consumer, update the planner's
dependency inventory together with its renderer and dependency tests.

`build_regions` partitions each body's unchanged step-then-consumer sequence.
The private default budget of 4096 estimates expanded carrier work: aggregate
leaves, wide limbs, selection/decode arms, and frame/output copies. Masked writes
also charge merge granules. It is a starting heuristic, not a compiler resource
limit. An oversized step or consumer occupies a region alone; an empty body has
no regions. Keep the cost model aligned with emitted work without materializing
large host lists for vector shapes.

Regions retain original step IDs and consumer descriptors. Inputs name values
consumed in the region but defined earlier; outputs name values defined in the
region and consumed later, including by frame/output consumers. A value that
passes across an unrelated region is not an input or output of that region.
Boundary lists use ascending value IDs; current-storage reads are deduplicated in
encounter order. Constants have a separate inventory and never require boundary
temporary storage. Constant casts refer directly to their materialization rather
than another step's alias. Planning rejects missing producers and reordered
steps before emission. Boundary values describe one evaluation invocation, not a
cache across edges.

`layout.rhm` plans one authoritative value-to-backing map for each body. A
`StorageRef` contains a state, constant, or scratch root and an ordered static
field/index path. Scratch roots identify a typed group, slot, and indirection;
group descriptors retain the physical C++ carrier and allocation count. Whole
scratch ownership and borrowing are derived from these references, not parallel
maps of rendered expressions. `emit.rhm` owns C++ names, dereferences, projection
parentheses, and declarations. Keep allocation deterministic and independent of
rendering; this is private backend storage policy, not a new RTL representation.

The emitter renders multi-region bodies as private C++ helpers and leaves
single-region bodies inline in their coordinator. Aggregate current-state roots
bind directly to their model's active `State` bank by const reference, including
registers and synchronous-read results with nested/wide payloads. Crossing roots
need no scratch payload or pointer slot; each consumer helper resolves its binding
afresh. Local roots use the same reference policy. State stays unchanged until
all pre-edge consumers finish; next-state, frame, and output sinks still copy
their values.
Static aggregate record-field and vector-index projections also borrow subobjects
when their parent has evaluation-long backing in state, constants, scratch, or a
borrowed binding. Follow only static projection chains when finding that backing;
parenthesize dereferenced parents before appending a field or index. Such crossing
projections need no additional slots. A computed helper-local parent cannot back
a crossing reference: materialize the projection without promoting its parent.
Local projections may reference earlier locals in the same helper. Resolve constant
symbols before layout so projected consumers need no undeclared ancestor aliases.
External input normalization, dynamic/asynchronous reads, and scalar projections
keep their existing materialization. Other nonconstant values consumed in later regions
receive scratch slots. Slots are grouped in typed arrays by C++
carrier; a producer writes its slot directly, and consumers bind const references.
Aggregate lookup-mux results use const-pointer slots when every branch already
has evaluation-long backing storage: an immutable constant or an earlier boundary
value, including a direct state binding or another borrowed mux. Selection takes the chosen lvalue's address
without copying its aggregate. Helper-local branches retain materialized results;
do not promote extra values merely to enable borrowing. Scalar results keep their
ordinary normalization and materialization. Boundary slots are never recycled
within an evaluation, so later consumers can safely follow borrowed chains.
Aggregate decode rows and their default share the immutable constant pool,
with uncared bits still deterministically zero. Each decode selects a const
reference locally or a const-pointer slot across regions; nested projections
borrow the selected row through the ordinary backing map. Every arm has the
same physical type and static lifetime, including empty-table defaults.
Row selection runs anew in each evaluation; this is not a cache or conditional
dependency scheduling. State/output consumers still copy values, and scalar
decodes retain their existing expression form. Region costs charge selection
instead of constructing each row's aggregate leaves.
Nonconstant vector constructors with boundary slots write each normalized operand
directly into its indexed element before binding the result alias. The destination
is unique and cannot alias any earlier operand, including a borrowed mux result;
this avoids the temporary required by general C++ aggregate assignment. Keep this
optimization confined to those slots, not arbitrary frame or state destinations.
`arrays.rhm` uses structured scratch locations to plan `ArrayElement`,
`ArrayFill`, and `ArrayGather` runs, covering every constructor element exactly
once in logical order. Gathers match affine runs in one group, including
descending indices and strides; projected subobjects are not whole scratch
locations. Repeated operands become fills. Runs of at least four elements use
loops; irregular elements retain assignments. The emitter renders these runs
without rediscovering their structure. Never infer storage from generated C++
strings or move an operand's computation into these loops.
Local constructors may use fresh local array storage for such kernels; other
locals retain ordinary initializers, and constants retain their immutable
materialization. Every element is assigned before the const result alias is
bound. Region order and costs remain unchanged: this is compact rendering of an
existing operation, not fusion, rescheduling, or a new public array IR.
Other intermediates remain function locals, while constants reference the shared
immutable pool. Each phase has its own model-owned scratch, initialized once and
overwritten before every use. This physical reuse provides no cross-invocation
validity: do not skip a helper based on old scratch contents. Helpers cannot retain
references in the sampled frame or outputs; sinks still copy values. Pointer slots
are refreshed before every read, including after a model copy or move, and never
provide validity across phases or calls. No evaluation allocates or clears a
scratch buffer, and separate models have independent buffers.

Region helpers use a local no-inline annotation for Clang/GCC and MSVC so native
optimization cannot reconstruct the monolithic evaluator; other compilers may
ignore this performance boundary. Expression/runtime helpers retain ordinary
optimization. All regions execute in order, including regions containing only
frame consumers. Scratch layout and function policy belong to C++ emission, not
the schedule or public target options. The budget remains a soft work estimate:
a single oversized expression or consumer can still produce a large function.

The model owns two initialized `State` banks and an active-bank index. Banks
contain registers, synchronous-read results, and held foreign results; memories
remain separate. The pre-edge evaluator receives explicit `const State& current`
and `State& next` references to distinct banks. It reads only current state and
writes every next-state field before returning a frame of pending memory writes,
assertion flags, and foreign-call arguments/enables. Both inline bodies and
bounded helpers follow this protocol; helpers receive the bank references rather
than resolving the index at each state access. Logical dependency and backing
plans remain independent of physical bank identity.

Every next-state and frame field is assigned on every attempt, including
register holds, disabled-call holds, and inactive read/write/check controls.
`Frame frame;` omits redundant clearing; persistent banks are zeroed once.
Never use the inactive bank's old contents as a hold value. A failed assertion
can dirty that bank and scratch, but changes neither active storage nor cached
outputs. Retry recomputes all pending fields. After assertions and foreign calls,
`tick()` switches the active index, commits the sampled memory writes, and
refreshes outputs. Publication performs no whole-state assignment, bank swap,
allocation, or clearing. The extra persistent bank replaces state formerly held
in the stack frame; model copying now copies both banks.

An index rather than persistent self-pointers preserves model copy/move ownership.
Borrowed state references belong to one evaluation phase; output evaluation binds
the newly active bank. Scratch pointers are refreshed before use after bank
switches, failed attempts, and model copy/move construction or assignment.
Adding a next-state or frame field requires an unconditional sink assignment;
never rely on initial zeros or a preceding successful evaluation.
`eval()` instead emits only the planner's output dependency closure: seed output
step indices, mark operands in one reverse schedule scan, and render marked indices
in their original forward order. Region traversal preserves this sequence.
State roots stop the walk; asynchronous reads include their addresses but not
their writers. Both bodies use the same step renderer and immutable constant
pool. `eval()` computes a local `Outputs` and publishes it after all output
expressions finish, without constructing a frame,
checking assertions, or invoking foreign calls. Empty output lists have no
evaluation steps; their checks and effects remain live in `tick()`.
Persistent memory arrays live outside the frame: evaluation copies no complete memory. Register
reads and asynchronous memory contents both come from old storage; a memory
read schedules its address but never its writers. Collect all memory declarations,
including unused/read-only/write-only resources, and intern their element types.
Synchronous read results terminate dependency walks like registers; collect their
slots before scheduling address, enable, mode, and write-data sinks. This permits
feedback through a stored read result without inventing a combinational cycle.
`RsimRead` identifies that result's memory and sampled controls; shared reads and
writes test opposite values of the same mode. Root-clock verification covers
every memory write and synchronous-memory declaration through occurrence aliases.
`tick()` samples the complete frame, commits registers, synchronous read results,
and guarded memory writes, then calls output-only `eval()` for post-edge outputs. A consuming
register therefore captures the preceding read result. Register reset does not
gate reads or writes. Disabled/shared-write read results use zero as this backend's
choice for unspecified data; tests must not make that choice a portable guarantee.
Masked writes retain their mask value and granularity in `RsimWrite`. During
evaluation, a guarded old-word load and the sampled mask/data produce the complete
pending word. `CppTypes.masked_merge` maps canonical granules onto individual
scalar leaves and constructs a typed result without packing the aggregate.
Narrow leaves and masks retain constant intersections. When either is wide,
the support helper builds a selection mask for each 32-bit limb from the leaf's
canonical offset and the sampled mask. This handles granules spanning limbs,
fields, or vector elements without growing expressions of wide constants.
Disabled granules preserve their old bits. All merging happens
before the shared commit phase; mask zero never changes shared-port read/write mode.
Collect assertions independently of value reachability and include their clocks
in root-clock validation. `RsimAssertion` retains condition/reset/guard references,
the occurrence path, label, and source location. Evaluation stores failed-check
flags in the private frame without throwing. `tick()` reports a failed sampled
check with `std::runtime_error` before committing any state or refreshing outputs.
Keep reporting separate from evaluation so retries and ordinary `eval()` remain
side-effect free. Do not implement hardware checks with the C++ `assert` macro.
Collect live foreign operations independently of value reachability, including
outputless children and unused function results. Validate scalar ABI capabilities
at these operations, not at unused design imports. Allocate foreign result roots
before scheduling argument sinks so feedback terminates at stored state. Each
result is keyed by both its call occurrence and its position in the result list.
`RsimForeign` retains the import plus enable/argument references. Evaluation
samples them into the frame and copies held results into pending state. After
all assertion checks, `tick()` invokes enabled functions/procedures using only
sampled arguments, then commits all results and ordinary state together. Native
`out` parameters point to temporary objects of their exact ABI types, never
directly to model state. Collect their values and the return value from a single
invocation, normalize each result by its own width,
and store them in the inactive state bank. Allocate and pass every declared out
parameter even when its RTL result is unused.
Declare C symbols once in a private namespace; use width-selected native types
and bit-preserving argument conversion rather than implementation-defined signed
casts. Foreign declarations, helpers, and includes are emitted only for live
calls. Foreign frame and result fields use hardware-derived scalar carriers.
Collect all live import types before header emission, including unconsumed outs,
so signature-only wide results still package the support header. Marshal
non-native inputs into `ceil(width / 32)` separate `std::uint32_t` words from the
sampled frame, low word first. Packed outs receive separate word arrays. Wide
marshalling uses explicit word copies in the support header, clearing outgoing
padding and discarding incoming padding without reinterpreting object layouts.
Narrow packed values retain their unsigned shifts and masks. Packed returns are
scalar uint32 values and remain limited to 32 bits; the native width-64 return
is unchanged. Never introduce scope emulation or a callback registry in this ABI.
For widths up to 64, unsigned 64-bit carriers plus an
explicit mask at every scalar value boundary preserve modular arithmetic, including
width 64. Width conversions and concatenation use scheduled operand widths.
Variable shifts check the full unsigned count before executing a C++ shift;
arithmetic right shifts construct sign fill explicitly. Sign extension and signed
comparison transform unsigned bit patterns, avoiding signed C++ overflow and
implementation-defined right shifts. Preserve normalization before widening.
Wide scalar storage uses `WideBits<W>` with low-word-first 32-bit limbs. Keep
normalization at value boundaries, explicit conversions, and operation-level
signedness. `check_wide_operation` admits only implemented operand/result
combinations; inspect nested leaves and operands even when results are narrow.
Foreign ABI checks retain their separate return-value limits. Scalar reinterpretation
casts preserve bits; aggregate casts use the canonical packed layout.
Wide add/subtract propagate carry/borrow through the 32-bit limbs. Truncated
schoolbook multiplication accumulates each limb product, stored limb, and carry
in `uint64_t`; their sum is at most `2^64 - 1`. Discard overflow above the declared
hardware width before subsequent operations, including widening. Unsigned ordering
visits limbs from high to low. Signed ordering first compares declared sign bits,
then uses unsigned ordering for equal signs. Keep signedness in the operation and
preserve the existing machine-word emission for widths through 64.
Wide concatenation places normalized operand limbs at canonical bit offsets into
one zeroed result; unaligned fields split across adjacent limbs. Left shifts use
the same placement with truncation, and right shifts reuse slicing. Arithmetic
right shifts fill bits above `width - count` from the declared sign bit.
Saturate shift counts to the value width only after inspecting all count limbs;
never discard high count bits during host-index conversion. Mixed narrow/wide
data and counts use the support helpers. Preserve local uint64 helpers when
both widths are at most 64, emitting those helpers only when actually called.
`CppTypes` selects carriers and splits host constants; the support header owns
portable limb operations. The emitter reads that header relative to its installed
module at emission time, preventing cached bytecode from embedding stale support.
Package it only for models that need it. The hyphen in `rsim-bits.hpp` prevents a
collision with any legal module header name. Keep the shared include guard and
contents identical across models compiled by the same compiler revision.
Struct declarations are interned by recursive representation shape, independent
of preferred names and global IDs. Arrays retain logical index order. Normalize
external aggregate inputs leaf by leaf; copies and constructors preserve that
invariant without packing aggregates into machine words. Packing casts use
explicit canonical offsets. For totals above 64 bits, place every leaf into one
zeroed packed carrier and unpack using slices of its original width. Preserve
the shift/OR path for narrow totals. Aggregate-to-aggregate casts evaluate their
packed temporary once, then construct the destination's typed leaves. Support
packaging must detect wide cast temporaries even when every stored leaf is narrow.
Decode compares cared bits leaf by leaf using `CppTypes.cube_match` and builds
outputs with `CppTypes.constant`. Host-side slicing of arbitrary-width constants
keeps every emitted integer literal within a scalar carrier; wide scalar masks
and lookup keys use the same limb constants as ordinary data. Evaluation planning
propagates canonical constant bits through casts; the emitter renders their
destination leaves directly. Equal rendered values of the same C++ type share
namespace-level
`static constexpr` storage with internal linkage. Each constant step aliases its
canonical object directly, so rendering a step does not require another step's
local alias. `emit_constants` owns the per-emission pool; `step_declaration`
renders an original schedule index with the same expressions and normalization.
This avoids per-evaluation ROM unpacking and array copies without changing schedule
identities, dynamic casts, or per-occurrence state. The constant maps belong to
one emission; they are not elaboration caches. One-hot selection
tests a bit in its containing limb, retaining machine-word masks for narrow
selectors. Verified disjoint
rows permit a conditional chain without giving row order meaning. One-hot muxes
select whole typed values; their invalid-selector fallback is unconstrained by
core. `dont_care` and uncared decode bits choose zero without introducing runtime
unknown-state tracking. Keep these partial choices out of behavioral expectations.
Dynamic vector helpers check bounds before subscripting. Injection and write
sets modify value copies, keeping scheduled operands and old register state
immutable. Partial operations may execute behind unselected muxes, so invalid
indices cannot throw or invoke C++ undefined behavior. The helper's zero read,
skipped invalid writes, and write-loop order are implementation choices for
unconstrained results, not circuit guarantees or collision priorities.
Core requires vector selectors and write-set indices to have exactly
`count_index_width(length)` bits. Small vectors cannot receive arbitrary wide
indices; a vector requiring more than 64 index bits cannot fit in a host array.
Keep this verified type constraint rather than broadening core to exercise a
backend index-conversion path.
Extend the capability list and renderer together with an independent behavioral
oracle. Do not introduce dirty flags, state-update elision, or a public optimizer
framework while extending baseline semantic coverage.

Direct SV gives each IR result a width-constrained net. This preserves modular
arithmetic before later widening; changes to expression inlining must retain
those boundaries. Signed operations opt in explicitly and shifts retain every
count bit. Packed layouts follow core record/array ordering. Projected drives
are already materialized by preparation and must not produce duplicate drivers.

`SvTypes` is local to one artifact and owns an ordered shared type package.
Reserve authored scope and member names before allocating aliases. Physical
shape identity ignores preferred names; equal shapes reuse the first alias.
Package-prefixed types prevent local names from hiding typedefs. Internal net
names use module-local traversal order, independently of global IR IDs. Allocate
references before rendering statements so forward-driven wires remain legal.

CIRCT shape/render caches also belong to one emission. Collect aliases before
rendering types, index aliases by canonical shape, and preserve first-seen
naming/order. Standalone module emission has an empty alias environment. Never
reuse rendered types between those environments.

Register, memory, and DPI result updates use nonblocking assignments so other
edge-triggered consumers see pre-edge values. DPI calls receive blocking private
temporaries before publishing result state. Never reorder or merge observable
foreign effects. Each memory has one procedural write owner; separate valid
writes retain simultaneous semantics. Core owns collision and invalid-address
preconditions, and the emitter must not invent priorities or defined values.

Keep assertion sampling synchronous: condition, guard, and reset are sampled
together before state updates. `SYNTHESIS` removes assertions but must reject
live DPI, whose functional effects cannot be discarded. CDC attributes attach
only to verified synchronizer stages; clocking analysis stays a separate target.

The CIRCT adapter folds a one-bit whole-word mask into write enables because
CIRCT 1.155 omits that mask from generated memory helpers. On shared ports,
select the masked enable only in write mode so zero-mask reads remain enabled.
The focused regression belongs in `tests/sync-memory-masked-test.rhm`; both
backend memory oracles cover the runtime behavior.

## Change workflow

1. Confirm the verified core meaning and public representation contract.
2. Add or adjust the owning backend's lowering. Keep target adapters thin and
   avoid adding frontend-specific decisions or broadening undefined behavior.
3. Update the explicit direct opcode inventory when core schemas change. All
   live operations must be supported before an artifact is published.
4. Add the smallest host or behavioral regression that distinguishes the change.
   Use an independent oracle rather than copying the emitted implementation.
5. Update the README support contract and this guide's implementation map when
   responsibilities move. Run boundary checks after import/module changes.

Do not add tests merely asserting that an unimplemented feature is absent.
Invalid type/operand combinations of supported operations are valid host cases.
Keep generated RTL, DPI headers, executables, and stimulus tables temporary.
Canonical example goldens are owned by the [CIRCT guide](../../tools/testing/circt/DEVELOPING.md#verilog-references),
not by the direct runner. Explain intentional output changes before updating any
checked-in reference.

## Validation

All Racket/Rhombus commands use repository wrappers and their worktree-owned
cache. The root targets below already do so.

| Scope | Command |
|---|---|
| Compiler/backend host contracts | `make backend-test` |
| Standalone C++ simulator, with undefined-behavior sanitization | `make rsim-test` |
| C++ simulator and direct SV against the same oracle | `make rsim-differential-test` |
| Direct SV and authored integrations, without CIRCT | `make verilog-test` |
| Both backends against oracles and each other | `make backend-differential-test` |
| CI no-CIRCT smoke | `make ci-verilog-direct-test` |
| CI compiler comparison and authored direct behavior/manifests | `make ci-backend-differential-test` |
| One family | `python3 rhodium/backend/tests/verilog/run.py --family sync-memory --differential` |
| One authored integration | `python3 rhodium/backend/tests/verilog/run-integration.py --fixture uart-dpi --differential` |

`--family` accepts `rtl`, `assertions`, `sync-memory`, and `dpi`, may be repeated,
and defaults to all. `--differential` additionally requires the pinned CIRCT
executable (`CIRCT_OPT`, PATH, or repository installation). Both routes use
Verilator with runtime assertions; direct-only execution must not require CIRCT.
Failed runs retain their temporary logs/artifacts; successful runs remove them.

The authored runner's `--check-manifests` compares both targets' signatures,
module inventories, portable-lowering decisions, trace descriptors, and source
preservation, but simulates only direct SV. CI uses this mode because the
language, standard-library, and protocol lanes already run the same authored
CIRCT benches. The planner always selects those owners alongside the
differential lane. Local `--differential` retains both behavioral routes for
self-contained integration checks. The full direct suite remains available
locally; CI's independent direct lane runs only SyncRam with an invalid
`CIRCT_OPT` to protect backend independence without repeating the full suite.

`tests/rsim/wide.py` checks widths 64/65/127/128/129/512 against Python integers and
optionally direct SV, compiling native execution with ASan and UBSan. It covers
bitwise operations, equality/ordering, modular arithmetic, muxes, constants, cross-word extraction, narrowing,
zero/sign extension, dirty external padding, reset/hold behavior, simultaneous
state sampling, and repeated instances. Arithmetic patterns exercise carry/borrow
chains, multiplication overflow, signed extrema, comparisons whose low bits match,
and widening of already-wrapped results. Shift counts cover limb boundaries,
width-minus-one/width/width-plus-one, and high bits through bit 128, including
dirty padding beyond a 129-bit count. Narrow 5/64-bit data also uses wide counts.
Concatenation covers mixed-width operands, all-narrow operands forming a wide
result, unaligned fields, and extraction round trips. Selection covers full-width keys,
selectors whose low bits match but high bits differ, every bit of a 129-bit
one-hot selector, and narrow/aggregate results selected by wide values. Decode
checks cared bits across limbs, wide output constants, row permutation,
empty/catch-all tables, and partial output masks. Invalid one-hot results and
uncared decoder bits are excluded from portable expectations, while guarded
contexts and output padding are checked. Multiple generated headers share one
support header in the same translation unit. Run the focused slice with
`python3 rhodium/backend/tests/rsim/wide.py --differential`; both ordinary rsim
suites include it. Host tests check support packaging, deterministic emission,
manifest identity, and preservation of source/prepared graphs.

The same wide runner covers nested records/vectors with 21/65/129/512-bit data
leaves and mixed narrow fields. It observes both packed bits and typed leaves,
including different-layout casts, to prevent opposing layout bugs from cancelling.
It checks dirty input padding, constants/decode, aggregate one-hot selection,
guarded dynamic reads/injection/write sets, write-port permutation, reset, and
pre-edge capture during state updates. A separate 96-bit cast between aggregates
with only narrow leaves verifies support packaging without any wide scalar IR
value. Native execution uses ASan/UBSan, and the packed Python oracle is shared
with direct SV.
The same runner checks constant ROMs with 5- and 65-bit data leaves, nested
record/vector cast chains, dynamic reads and injection, zero constants, and
repeated instances with independent addresses. Repeated evaluation verifies
that a modified vector cannot mutate shared constant storage. Host emission
checks protect one-array materialization and the absence of runtime unpacking
for these constant casts.

Rsim's native fixtures exercise widths 1/5/63/64, overflow, forward connections,
reset sampling, repeated evaluation, simultaneous swaps, holds, priority, constant-input
feedback, resetless sampling, keyword/underscore names, and independent child/model
state. Aggregate copy/move construction and assignment checks destroy the source
after populating evaluation bindings, then evaluate and tick the destination
against an independent copy at both bank parities.
The native driver default-constructs models over nonzero byte patterns
and checks inputs, cached outputs, and evaluated cold state, so static-storage
zeroing cannot mask a missing persistent initializer. To expose missing frame
assignments as well, run the native suite with a compiler supporting automatic
variable pattern initialization, for example
`CXX="clang++ -ftrivial-auto-var-init=pattern" python3 rhodium/backend/tests/rsim/run.py --region-budget 24`.
The same suite's ordinary default budget covers inline evaluation of small models;
forcing 24 exercises region boundaries. `tests/rsim/arithmetic.py` supplies
independent integer expectations for
signed extrema, shift counts at and beyond width/64 (including large 64-bit counts),
concatenation and cross-boundary slices, and modular results before widening.
`tests/rsim/aggregate.py` adds field-level oracles for nested records/vectors,
lengths 1/3/4, totals through 341 bits, preferred-name collisions, aggregate
reset/swap/hold behavior, independent instances, and 64-bit canonical packing.
A nested cast-to-record/vector projection covers computed parents whose subobjects
may need copies across helper boundaries.
Its record/vector feedback through lookup and one-hot muxes has acyclic field
dependencies. Separate observations check both selectors and repeated module
instances, while nested projections and aggregate state share the integer oracle.
These deliberate whole-aggregate loops require the Verilator `UNOPTFLAT`
allowance; its `class` field also requires `SYMRSVDWORD`. Other diagnostics remain
fatal. SV binding host checks separately verify that projected muxes retain
selector dependencies but omit unrelated sibling-field dependencies.
`tests/rsim/dynamic.py` covers all selector/index/enable combinations at lengths
1/3/4 for scalar and nested record elements, write-port permutation, disabled
invalid indices, and pre-edge read capture with vector state updates. Invalid
partial results are masked by fixture muxes; only defined outputs are compared.
Its separate native harness uses address/undefined-behavior sanitization at
`-O0` to keep eager partial computations visible to bounds checks; the baseline
harness retains its optimized build.
`tests/rsim/memory.py` tracks per-word definedness across two occurrences at
depths 1/3/4 with scalar and nested aggregate data. It covers multiple readers
and writers, register capture before writes, simultaneous read-dependent writes,
addresses from old registers, writes during reset, disabled/invalid addresses,
and storage-only declarations. Address/undefined-behavior sanitization checks
guarded access; the oracle discards knowledge after conflicting or invalid writes.
`tests/rsim/sync_memory.py` covers 1R/1W/1R1W/1RW ports at depths 1/3/4,
scalar and aggregate payloads, stored-read feedback, consuming-register latency,
independent occurrences, and control changes between edges. Mask cases include bit,
byte, whole-word, and cross-field granules on 133-bit aggregates; bit 63 of the mask;
partial initialization; zero-mask writes with unknown data; and masks from old read
results. Its packed-integer oracle tracks definedness per bit independently of C++
leaf traversal, excluding disabled, uninitialized, invalid, and colliding read bits
while checking later reads of written storage. The native harness also uses address
and undefined-behavior sanitizers.
The wide runner reuses both memory fixtures and oracles at 65/129/512-bit leaf
widths, with two independent occurrences at depth 3. Its synchronous matrix
includes unmasked, bit, byte, whole-word, and cross-field writes; masks through
512 bits; and a 133-bit mask over an aggregate containing only narrow leaves.
It checks each mask position during partial initialization and preserves the
same definedness rules for old-read data/mask feedback, collisions, disabled
reads, and invalid addresses. Native carriers receive dirty input padding;
the direct-SV adapter masks every control and copies only declared input bits.
`tests/rsim/selection.py` covers scalar widths 1/5/63/64, one-hot selectors of
1/3/64 bits, unknown choices hidden by selection, and nested aggregate decode
inputs/outputs through 139 bits. Its packed-integer oracle checks care masks,
defaults, empty/catch-all/reordered tables, scalar-carrier boundary crossings,
and register capture with reset and repeated evaluation. Invalid one-hot results
and uncared bits are excluded from comparisons; surrounding defined behavior is
still checked. The native family uses address/undefined-behavior sanitizers.
`tests/rsim/assertions.py` runs three passing and eight expected-failure scenarios
on native C++ and assertion-enabled direct SV. It checks reset/guard suppression,
pre-edge state and reset sampling, repeated instances, outputless verification
children, top-level/unlabeled checks, and repeated `eval()` calls. Native failures
also exercise repeated rejection and retry at both bank parities, verifying
unchanged cached outputs, registers, asynchronous/synchronous memory, and
read-result state. Native builds use `NDEBUG` plus
address/undefined-behavior sanitizers, and check quoted/multiline source diagnostics.
`tests/rsim/foreign.py` links the same host implementation into native and
Verilator models, compiling it against Verilator's generated DPI declarations.
It compares effect multisets and held/consumer state at every observation for
widths 1/8/16/32/64, including signed and high-bit patterns, disabled holds,
feedback, resets, repeated evaluation/instances, and outputless/unused-result
calls. A mixed-width function exercises every native out-pointer type, a return
whose type differs from the first out, simultaneous result publication, separate
enables, and feedback through both an out result and the return. Other calls
consume only the return or leave every result unused. Packed widths 3/5/31/33/63
exercise mixed native/packed signatures, low-word-first input/output arrays,
scalar packed returns, feedback, and unused packed results. Host code deliberately
sets high padding bits in results; the oracle checks they cannot reach held state
or later procedure arguments. Boundary and asymmetric-word inputs distinguish
word order and correct input padding. The native sanitizer build additionally
checks independent model objects
and that repeated assertion failure suppresses all calls and preserves state before
retry at both bank parities.
`tests/rsim/wide_foreign.py` extends that ABI proof to 65/129/512/513-bit
inputs and outs, mixed native/wide results, and signed bit patterns. It compares
all argument words, effect counts, held results, and pre-edge consumers with a
Python oracle. It includes unused wide outs and a separate model whose only wide
type is an entirely unconsumed foreign result, plus dirty input/result padding,
feedback, repeated occurrences, independent objects, and assertion suppression.
The native build uses ASan/UBSan; direct SV compiles the same host implementation
against its generated DPI declarations. Run it with
`python3 rhodium/backend/tests/rsim/wide_foreign.py --differential`; both rsim
suites include it through the foreign-call family.
`tests/rsim/uart.py` compiles one authored pair of production `UartDPI` instances
through rsim and optionally direct SV, checking compatible manifests and source
preservation. Both executables link the unchanged device PTY model and its
existing test access helpers. A shared C++ serial/PTY scoreboard checks two model
IDs at different baud rates, queued input under backpressure, bidirectional bytes,
reset retention, bad-stop delivery, framing-error history, and absence of duplicates.
The native build uses address/undefined-behavior sanitizers. Host I/O is bounded;
compare protocol outcomes rather than OS-dependent polling latency. This smoke
runs in the ordinary rsim suites; for a focused run use:

```sh
python3 rhodium/backend/tests/rsim/uart.py --differential
```

Omit `--differential` to run without HDL tools. Changes to the production UART,
its PTY model, or shared test helpers must also select the host/backend CI lane.

`rsim_sv_target.plan(prepared)` reuses `rsim_target.plan(prepared)` once, validates
its schedule, and wraps that model plan without another graph preparation or
schedule. The binding emitter propagates external-input dependency flags in
schedule order; state roots break those paths. Keep this check separate from
native model capabilities. The bridge retains unsigned 64-bit DPI carriers for
narrow ports and uses two-state packed vectors for wider scalar ports. Wide
inputs/outputs use the same explicit word-copy helpers as functional DPI, with
padding normalization and no reinterpretation of model storage. Keep the wrapper
ABI distinct from functional foreign signatures. Both representations use
model-specific C symbols and private SV names allocated outside the authored
port namespace. Initialize outputs with a
side-effect-free `eval()` so constant outputs are available before the first
edge. Keep DPI output buffers private and publish through NBA assignments.

`tests/rsim/sv_bridge.py` compiles the same Builder circuits through the binding
target and direct SV. It runs both with unchanged `sims/TestDriver.v` and its native lifecycle runtime, a separate
two-instance scoreboard, and a scalar-port conversion bench. The shared host
callback verifies pre-edge state, context scope, VPI arguments, and exactly one
call per edge. The scoreboard checks independent resets, wraparound, output
stability between edges, and external synchronous consumers. Both routes must
terminate on a hardware assertion; the bound model's exception must appear as
an SV fatal. Conversion coverage includes widths 1/5/31/33/63/64, high bits,
authored keywords/private-name collisions, a nonstandard clock name, and constant
outputs before the first edge. A two-instance wide bench checks 65/129/512/513-bit
ports, initial constants, reset/hold behavior, pre-edge consumers, and output
stability between edges against direct SV and an SV state oracle. Wide assertion
failures must also cross the bridge as SV fatals. A standalone ASan/UBSan driver
calls the generated bridge ABI with dirty input padding, checks output padding,
and verifies unchanged output buffers and held state after a caught failure and
retry. The separate benches force-include Verilator's generated DPI header when
compiling the bridge to check its C ABI against the SV declarations. The
production-driver run links the runtime with its own `noexcept` declarations.

```sh
python3 rhodium/backend/tests/rsim/sv_bridge.py
```

This requires Verilator but no CIRCT. It runs in the rsim differential suite,
not the HDL-free native suite. Changes to `sims/TestDriver.v` or its native
lifecycle runtime also select that CI lane. Generated models, wrappers, builds, and logs remain temporary; failed
runs retain them for diagnosis. Host contracts in `tests/rsim-sv-test.rhm` cover
boundary diagnostics, source preservation, deterministic artifacts, shared
manifests, identical standalone model artifacts, and single provider expansion.
This fixture's scope is the binding contract. Separate full-SoC smoke
qualification is described in the [simulator guide](../../sims/DEVELOPING.md).

`tests/rsim/chi_memory.py` qualifies one production `CHIDPIMemory` with 512-bit
DAT and its native registry/storage implementation through SV-hosted rsim and
direct SV. Testbench launch registers place the combinational CHI ready/valid
interface inside the clocked wrapper; they do not alter the memory controller.
The scoreboard compares cycle/response transcripts, read data, byte masks across
word boundaries, independent RSP/DAT stalls, capacity backpressure, and reset
of an incomplete transaction. A host helper freezes and checks one reset-time
registration, preloads bytes through the real store, and verifies reset retention.
Compile the production DPI adapter against generated declarations on the direct
route; the rsim route also checks its wrapper declarations. Run it with
`python3 rhodium/backend/tests/rsim/chi_memory.py`. It runs in the differential
suite, and CHI protocol/subordinate changes select that CI lane.


The differential runs compare only defined values, using identical stimuli and
observation points. Register fixtures initialize through reset; memory fixtures
initialize through writes and never assume portable zero contents.
`CXX` selects the native compiler. The host/backend CI target includes the small
native run without HDL tools; the existing backend differential lane also runs
the rsim/direct-SV comparison. Host tests additionally cover source preservation,
deterministic naming independent of global IDs, and prepared/provider reuse.
Structural emission checks protect output-only evaluation at state roots and
empty output dependency sets while retaining pre-edge updates and foreign
effects. Behavioral scoreboards cover the resulting output and edge semantics;
CI does not impose runtime thresholds for this optimization.
`tests/rsim-evaluation-test.rhm` checks planned consumers, value availability,
storage dependencies, shared output computation, and occurrence identity with
Builder fixtures. Small forced budgets check region boundaries against an
independent backward liveness walk, including shared constants and final consumers.
`tests/rsim-cse-test.rhm` checks transitive expression sharing, operand order,
type/attribute distinctions, deterministic and idempotent rewriting, complete
consumer remapping (including optional value zero), and preserved effect/resource
identity. Native and direct-SV scoreboards exercise the optimized target with
independently authored duplicate arithmetic and existing state, memory, assertion,
and DPI fixtures at ordinary and forced-small region budgets.
`tests/rsim-array-updates-test.rhm` checks exact lane matching, wrapped-index
non-matches, retained shared consumers, deterministic pruning, and effect/resource
remapping. Dynamic native/direct-SV scoreboards observe expanded updates, shared
lanes, and old-state capture across reset, eval-only calls, and out-of-range
indices. Constant-ROM fixtures cover aggregate updates containing wide leaves.
Wide/aggregate copies check the cost heuristic and oversized-item handling.
Planner checks validate structured backing, borrowing and helper-local fallback,
deterministic slot allocation, and array-run expansion against the original
ordered operands, including direct/indirect and descending/strided gathers.
For rendering-neutral refactors, compare complete generated fixture artifacts
before and after at both default and forced small region budgets; use the simple
RV5Stage SoC to check large-layout emission as well.
Emission checks additionally protect local-only intermediates, crossing-value
slots, aggregate borrowing and its local-operand fallback, direct vector
construction into distinct boundary slots, deterministic helper order, and
effect-free helper bodies. The aggregate native/direct-SV scoreboard
checks chained and multi-arm selections with constants, nested records/vectors,
dirty padding, repeated constructor operands, and pre-edge register capture;
it also checks forward/reverse/strided gathers, mixed loop/scalar runs, fills,
and gathered state before and after an edge. The wide scoreboard repeats fills
and direct gathers through wide carriers. Structural checks ensure the split
aggregate fixture actually emits both direct and indirect loops; behavioral
oracles check their values under ASan and UBSan. Force small boundaries through
the existing native Builder scoreboards and sanitizers with:

```sh
python3 rhodium/backend/tests/rsim/run.py --region-budget 24
```

This fixture-only control covers scalar/aggregate/wide evaluation, reset and
simultaneous updates, asynchronous/synchronous/masked memory, assertion failure
and retry, DPI arguments/results, independent models, and repeated `eval()`.
Add `--differential` for the same direct-SV comparisons. Production integration
fixtures (UART, CHI, and the SV bridge) keep the ordinary target and default
budget. Keep these dependency contracts alongside behavioral tests when changing
evaluation planning or frame construction. For helper/layout changes, run both
the default differential suite and the forced-boundary native command, then
qualify optimized SoC compilation and smoke execution separately.

### Behavioral coverage

| Family | Required distinctions |
|---|---|
| RTL | Widths 1/5/65; modular arithmetic and overshifts; hierarchy/portable expansion; packed layouts/casts; dynamic vectors and write sets; partial outputs; reset/hold/edge timing; asynchronous memories; CDC stage latency |
| Synchronous memory | Depths 1/3/4; 1R/1W/1R1W/1RW; scalar/aggregate data; bit, granule, and whole-word masks; per-bit definedness; old read results; independent instances |
| Assertions | Guard/reset suppression; pre-update sampling; occurrence/label diagnostics; two passing and six expected failures; all eight scenarios with synthesis defined |
| DPI | Native and packed widths through 129 bits; wide inputs/out results; held results; independent clocks/enables; call multisets and instance scopes; generated C-header ABI; explicit synthesis rejection |
| Authored integrations | Existing SyncRam timing/masks, UART bidirectional serial/PTY behavior, and event runtime/elastic lineage and functional equivalence, reusing package-owned benches and native models |

Only defined output bits enter comparisons or differential transcripts. Oracles
track startup and partial initialization explicitly; undefined addresses,
collisions, invalid write sets, and unconstrained decode bits do not gain a
promised value. Stimuli use temporary hex tables rather than enormous unrolled
SV benches. Wide transcripts are chunked to respect Verilator argument limits.

CIRCT memory helpers are isolated per artifact/width. The reference pipelines
lower high-level memories before sequential lowering and use memory simulation
with randomization disabled and undefined disabled-read results. Synchronous
memory benches run with `SYNTHESIS` defined. DPI runs separately because it
rejects that mode. The reference type fixture renames a `Packet` output to
`restored` to avoid the pinned CIRCT exporter's typedef-shadowing issue; the
direct fixture retains the collision and both share the same bit-level oracle.

Event integrations select `event_trace_pass` through the RTL pipeline and compare
JSON/C++ descriptors across targets. Their event and Flow dependencies select
both backend CI lanes.

Authored integrations compare logical signatures, module inventories, portable
lowering decisions, and source preservation. Their shared behavioral benches
are not complete cycle-trace equivalence proofs. The UART minimum-divider
fixture permits `UNSIGNED` for a legal constant comparison; other diagnostics
stay fatal. SoC smoke validation and its additional warning allowances belong
to [simulation maintenance](../../sims/DEVELOPING.md).

After changes, run boundary, license, parameter-annotation, and whitespace audits
as applicable. CI runs the direct lane without CIRCT and the differential lane
with both tools. Keep dependency routing current when fixtures reuse another
package's circuit, bench, or native model.
