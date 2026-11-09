<!-- Defines reusable processor-component ownership, extension points, and focused verification. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing common processor components

Read [README.md](README.md) for public APIs and timing contracts, then
[core ownership](../DEVELOPING.md) for the dependency graph. This guide owns
standalone helpers and routes changes to the cache, CSR, translation, prediction,
floating-point, and vector component families.

## Architecture and ownership

Common components may consume architectural RISC-V definitions, public Rhodium
libraries, Flow, CHI, and HardFloat where their owning guides permit it. They
must not import pipeline implementations, named-core configuration or payloads,
simulators, backends, examples, or tests. See the repository
[package graph](../../rhodium/DEVELOPING.md).

Components own physical controls, reusable transaction state, and documented
latency. Integrating pipelines retain admission, cancellation, stage context,
architectural fault priority, and retirement. Keep opaque caller tags opaque.
Direct tests live in `tests/` or the owning child family's `tests/`; integration
fixtures stay with the consuming pipeline.

## Implementation map

Small shared integration modules remain at the root:
`profile.rhm` composes common ISA-name groups and MISA capabilities from explicit execution choices;
`udb.rhm` composes shared bank/cache/MMU/vector declarations for callers opting
into the precise-execution contract. Named profiles retain optional extension
selection, qualification claims, and physical integration bounds.
Keep configuration defaults and supervisor qualification predicates local. In
particular, RV5Stage advertises its supervisor guarantees only for RV64 Sv39;
RV2Wide also advertises the non-page-table guarantees for RV64 Bare.
`chi-hart.rhdl` owns physical-region/Home mapping and requester attachment;
`misaligned-access.rhdl` owns fragment sequencing with opaque caller context;
`memory-lookup.rhdl` defines the fixed-cycle lookup and store-commit interface
specialized by the cache and superscalar adapters. `memory-arbiter.rhdl` owns
scalar/vector lookup grants, MEM response routing, and WB store ownership. A
contested vector lookup receives its native Slow result. Callers specify how to
handle an absent lookup response. The same file owns N-client transaction
arbitration. It exposes the live grant for admission sidebands and routes replies
by their returned tag. Callers supply the priority order, tag mapping, and any
outstanding limit.
`memory-response.rhdl` owns opaque data/context replies, optional-response pairing, and one-cycle LSU result capture;
`vector-layout.rhm` owns pure physical row geometry; `cosim-source.rhm` owns
passive observation declarations. None selects a named core's profile or
implements its retirement policy. Architectural encodings and stateless
architectural policy stay under `riscv/isa/` and `riscv/rtl/`.

The misaligned engine's saved request has one XLEN data word. Store replies
must leave it intact for a possible second request. Loads use it for assembly:
the first reply supplies all lanes, and the second replaces only those below
the original byte offset. The address, width, signedness, and caller context
remain those of the original request through completion, including on faults.

`integer-execution.rhdl` supplies opaque arithmetic request/result ownership;
`integer-service.rhdl` carries those tags through iterative multiply/divide and
feed-forward multiplication, with explicit scheduled or buffered return adapters.
`register-write.rhdl` supplies a GPR destination/value payload independent of
physical register-file ports. Named-core adapters retain service selection,
launch/authorization timing, and architectural writeback scheduling.
`register-file.rhdl` owns explicitly read/write-port-sized storage (two writes by default).
Named cores wire physical ports directly and retain forwarding, same-destination
write policy, and completion reservations. Only x1–x31 have storage. Each
read port decodes bits [2:0] once across four groups of eight (x0 is a constant),
then selects a group with bits [4:3]. Reads are pre-edge; the storage
does not allocate ports or select architectural winners. GPR storage has no
reset value; fixture scoreboards track written registers instead of assuming
zero initialization. Validate retention across reset with idle writes, x0,
read/write timing, independent writes, and invalid write collisions with the
`register-file` and `register-file-rv32`
rsim fixtures, each exercising 2R2W, 4R2W, and 4R3W storage. RV5Stage owns its
write-to-ID forwarding in `core.rhdl`; the shared storage never bypasses writes.
Use `rv5stage-core`, `rv5stage-multiply`, and CSR/FP completion fixtures for
that timing boundary. RV2Wide integration additionally uses `rv2wide-core`
and its FP/vector completion fixtures when those paths change.
`integer-format.rhdl` owns scalar M-extension word-operand normalization and
product/quotient/remainder projection, without scheduling or owner types.
`reservation-wait.rhdl` owns the shared wait modes, WRS timer, and wake-versus-timeout priority;
named cores retain the instruction and qualify TW/VTW from their CSR state.
Capture resets the timer before any same-edge increment. Completion remains a
level rather than consuming or replacing the caller's retained owner.
`reservation-wait-decode.rhdl` maps canonical WRS encodings to those modes.
Named decoders compose the relation into their own partial controls, retaining
inactive-resource and serialization policy and leaving unused operands free.

`writeback-calendar.rhdl` owns registered write-cycle occupancy and ordered
same-cycle claims. Keep `registered_free` independent of all reservation
inputs and pause; `prior_claims_available` observes only earlier accepted
claims and honors pause. Queries share the reservation delay numbering and
add no state. Callers choose a dependency-safe view and retain same-group
collision policy, result ownership, and authorization. The existing
`rv5stage-writeback` rsim fixture checks both views, multiple simultaneous
queries, reset, paused aging, and invalid reservations; pair it with named-core
FP/multiply/vector fixtures when changing a caller.

`operand.rhdl` owns scalar source-use, ALU routing, and immediate controls;
`operand-decode.rhdl` owns the common I/M/A/B/Zicond relations and reusable partial
patterns. Named decoders compose their domains and retain different MOP
routing policies; dedicated M services leave ALU operand routing free. Preserve
care masks when moving relations: sharing a type is not permission to constrain
an unobserved field.

UDB declarations follow implementation ownership: `csr/udb.rhm` describes the
bank, `cache/udb.rhm` describes L1D reservation/maintenance policies, and
`mmu/udb.rhm` describes translation modes. `udb.rhm` defines an explicitly
selected precise-execution and split-access contract, not a guarantee inferred
from instantiating those components. Named
projections retain address/map constraints and responsibility for satisfying
that execution contract. `vector/profile.rhm` validates scalar/vector execution
compatibility without selecting either core's ISA defaults or scheduling.

Keep accepted split-access fragments irrevocable and retain their owner until
the response. Translation/PMA checks, interrupt/drain policy, and retirement
belong to the caller. Keep vector row geometry pure host code with an explicit
row width; it must not imply ports, SRAM, or scheduling.

`MemoryResponseCapture` joins a live caller context with its same-cycle response
or fallback. Its context output is combinational; only the result is registered.
Capture the caller's instruction/control state in parallel and rejoin at the
next stage without extra storage. Keep early fault/replay decisions and commit
authorization in the caller, and keep the physical cache response combinational.
For its RV5Stage integration, run the EX/MEM/WB load-path fixtures listed in
[`../rv5stage/DEVELOPING.md`](../rv5stage/DEVELOPING.md#focused-validation).

`pc.rhdl` owns the compact-PC representation and type-family capture, prediction,
and representability operations described in the [public contract](README.md#compact-pc-storage).
It depends only on XLEN and the public authoring surface. Fetch owners qualify admission to the fixed RV32/Sv39 PC domain; the type must not import their
policies or allocate exception escape storage. Fetch fault outcomes retain only
a positive sequential-boundary bit; shared pipeline prefixes remain stage-owned.
Both backends suppress nonrepresentable redirects and retain their exact target
only after the originating instruction commits. `fetch/redirect.rhdl` classifies
that fault using post-retirement privilege/translation state, without owning
pipeline ordering, drain, or CSR commands.
`RiscvHartCHIConfig.check_compact_fetch` validates executable address-set maxima;
runtime fetch checks also cover guest-Bare translation to low host PAs. Shared
predictors retain generic widths; core callers select the fixed PC width.
For changes, run the `compact-pc` rsim fixture and `tests/pc-test.rhm`, then
the affected [predictor](bpred/DEVELOPING.md#validation) and fetch/window fixtures.

`cosim-source.rhm` bridges public core metadata and frontend read/domain APIs.
Its adapter contracts, local taps, explicit child views, and sibling contexts
are remappable read-only declarations, not capture hardware. All selected
components must share the hart's clock/reset domain. DPI producers, adapters,
event reconstruction, and reference checking belong to
[`../../sims/cosim/`](../../sims/cosim/DEVELOPING.md); functional declarations must not
import compiler passes, simulators, or reference models.

1. Define already-decoded physical controls and leave instruction recognition,
   operand routing, pipeline scheduling, and architectural destination policy
   with the caller.
2. Specify combinational or ready-valid timing, backpressure stability, reset,
   and exceptional fixed-width behavior in [README.md](README.md).
3. Use a host test only for pure configuration or public elaboration-time
   rejection. Test datapath results, state, backpressure, and timing through a
   rsim fixture; do not snapshot internal operations just because a
   component is new.
4. Integrate the component into a named core only after its standalone contract
   is stable.

When adding a named core, create `cores/<name>/` and keep its decode, datapath,
architectural state, adapters, documentation, and tests together. Give it a
public README and companion DEVELOPING guide before adding system integration.
When named cores use one implementation family, keep that implementation and
its tests in the family instead: RV2Wide and RV3Wide own separate configuration
classes containing `SuperscalarPipelineConfig` and hart wrappers that use it
directly, with no cross-import
between the named directories. Pipeline behavior remains shared.

### Packed SIMD implementation

The packed integer unit in `simd-alu.rhdl` takes `XLen.X32` or `XLen.X64` and
specializes to that architectural width. Guard
bits around four or eight bytes isolate each element within one shared adder;
one additional top bit retains full-word carry. Byte comparison pairs feed a
shared reduction tree. The tapered shifter bank is 32/16/8/8 or
64/32/16/16/8/8/8/8 bits, routing element widths through the same slots. Left
and right shifts share each slice by reversing around the right shifter.
Barrel stages select fill or wrap bits for rotation; repeating a narrow
operand across its slot makes the same network rotate at every element width.
Byte population counts and directional zero counts feed shared reduction
trees. Byte-local bit reversal also serves the permutation path, whose
remaining byte-order changes are wiring and width-selected routing.
Width-specific logic only routes operands and packs results/masks, rather than
instantiating full datapaths per width. These design techniques follow
[Saturn's integer unit](https://github.com/ucb-bar/saturn-vectors/blob/master/src/main/scala/exu/int/IntegerPipe.scala)
and [shift unit](https://github.com/ucb-bar/saturn-vectors/blob/master/src/main/scala/exu/int/ShiftPipe.scala);
the public operation and widening contracts are in [README.md](README.md#packed-simd-integer-alu).
`SimdWidenOperands` independently sign- or zero-extends source halves, or
passes an already destination-width left source alongside one extended source,
without adding another ALU or coupling widening to instruction decode.
`SimdCompress` performs only stable word-local element compaction and returns
its selected-element count; cross-word suffixes and architectural progress
belong to the caller. Its exclusive lane ranks and total share the standard
`prefix_scan` network with a count width that includes a fully selected word.
Keep the destination-routing priority and unused-lane zeroing separate from
rank computation. Keep RVV register layout and architectural policy outside
these reusable components.

`simd-multiplier.rhdl` shares the existing three-stage `PipelinedMultiplier`
arithmetic through banks derived from physical word width and maximum element
width, independently of XLEN. Each byte-offset slot gets the largest supported
multiplier aligned to that offset. A 64-bit bank's byte-offset order is
64/8/16/8/32/8/16/8; a 32-bit bank uses 32/8/16/8. Wider slots extend narrower
operands; product routing selects each lane's full result. Lowering the maximum
element width repeats smaller banks and leaves larger elements to caller-owned
execution resources. This follows the
sharing principle in [Saturn's segmented multiplier](https://github.com/ucb-bar/saturn-vectors/blob/master/src/main/scala/exu/int/SegmentedMultiplyPipe.scala),
using the repository's existing multiplier implementation rather than importing
another arithmetic implementation.
Result context advances separately for the same three cycles; a fourth stage
registers the numeric projection. The formatter's guarded-byte adder prevents
cross-element carry and remains independent of vector scheduling or writeback.
Keep scalar-product formatting usable without instantiating another multiplier;
the pipeline prunes unused projections through `~max_element_width`.

## Change workflow

Update the owning public contract when behavior changes, and change producers,
consumers, and fixtures together for payload changes. Preserve storage kind,
latency, transfer qualification, cancellation, and accepted-owner lifetime.
Move tests with their component and update all importers and fixture manifests;
do not leave forwarding modules at old paths. Follow the child family's guide
for its architectural invariants and use the repository RTL style guide.

## Focused validation

The direct SIMD fixture checks exhaustive byte operand pairs and directed or
random wider elements, including rotation with dirty fill controls, zero counts, byte permutations,
both widening halves, compaction masks, and enable remapping against independent
per-element models:

```sh
FIXTURES='simd-alu simd-alu32' python3 tools/testing/rsim/run.py
```

Pure host contracts can use the package tests directly. Cycle-visible behavior
is owned by the matching backend fixtures:

`pipelined-multiplier` checks the three-cycle contract at widths 2, 8, 32, and 64.
Small widths exhaust every operand/signedness pair; RV32/RV64 combine signed
extremes, half-width carry boundaries, and seeded random products. Its independent
full-width oracle checks every valid cycle, including bubbles and reset with
work in flight. Before the first register, merge the low and cross products,
upper product, and both sign corrections into one carry-save pair. Add its
sum and already-shifted carry in independent 16-bit chunks (one smaller chunk
for narrow products). Register each subtotal with its carry into the next
chunk. Propagate those boundary carries before the second register; discard
only overflow beyond twice the operand width for a full-product client.
For a selected-half client, register just its chunks: a low result starts
with zero carry; a high result starts with the carry out of the entire low
half. The same oracle checks this two-cycle result at widths 8, 32, and 64
while varying half selection between launches.

`iterative-multiplier` checks every eight-bit operand pair in all four signedness
combinations, plus RV32/RV64 edge cases, exact latency, and replacement of a
stalled response. Its accumulator starts with the multiplier in the low half;
each right shift consumes one operand bit and installs one product bit. Keep the
carry from the upper-half addition when shifting. Only the final iteration
corrects the product sign.

`iterative-divider` exhausts all eight-bit operand pairs in both signed modes,
checking the quotient, remainder, and exact response cycle. Keep the trial
subtraction one bit wider than the operands. Finalization corrects signs in
the working registers themselves; a blocked response holds both words until
transfer, including when a new request replaces it on that same edge.

```sh
tools/run-racket-tests.sh cores/common/tests/branch-resolver-test.rhm
FIXTURES='rv32i-alu rv64i-alu load-store iterative-multiplier pipelined-multiplier iterative-divider' python3 tools/testing/rsim/run.py
```

`simd-multiplier` checks `(32, 16)`, `(64, 32)`, and self-contained `(64, 64)`
geometry, exhaustive byte products under
all signedness combinations, directed and randomized wider lanes, both widening
halves, accumulating/subtracting results, all fractional rounding modes and
saturation, exact three/four-cycle latency, bubbles, and reset while occupied.
It also checks scalar-produced full-width products through the common formatter.

```sh
python3 tools/testing/rsim/run.py --fixture simd-multiplier
```

`memory-decode.rhdl` generates active scalar/atomic/cache-block rows into caller-shaped
controls and owns the guest-to-ordinary memory instruction mapping. Named decoders
add inactive-domain rows, validity columns, and issue/retirement policy; shared
relations must not constrain unused fields just to match another core\'s bundle.
Use both cores\' composed decode checks when changing these mappings.

Shared instruction mappings and row geometry use
`cores/common/tests/{alu-decode,multiply-decode,divide-decode,vector-layout}-test.rhm`.
Scalar integer formatting changes use `rv5stage-multiply`, `rv5stage-divide`,
`rv2wide-core`, and `rv2wide-rv32`. Reservation-wait changes use
`rv5stage-zawrs` and `rv2wide-core`, including timeout-edge reservation loss;
`rv5stage-hypervisor-core` additionally checks VTW and simultaneous TW/VTW.
Tagged integer service changes use `rv5stage-integer-execution` for held
results and scheduled returns, and `rv2wide-core-vector` for shared-client
ownership and scalar WB authorization.
Shared test-only [`tests/rsim/port-observers.rhm`](tests/rsim/port-observers.rhm)
adds clocked scalar-field observations of public component ports for both named
cores. Keep architectural and occurrence scoreboards with each caller's tests;
the helper changes no functional signature and belongs to no production core.
CSR, translation, and floating-point checks are routed by their respective
[CSR](csr/DEVELOPING.md#change-workflow-and-validation) and
[MMU](mmu/DEVELOPING.md#validation) and [FP](fp/DEVELOPING.md#validation) guides. For shared split-engine changes,
cover both callers with `rv2wide-mmu` and `rv5stage-hypervisor-core` fixtures.
Changes to passive observation metadata need the cosim pass tests.
Shared predictor behavior is covered by the `bpred-btb` and `bpred-ras`
fixtures; use the [predictor guide](bpred/DEVELOPING.md#validation) for
RV5Stage integration checks.

The protocol-neutral tree-PLRU policy and its `cache-replacement` fixture are
owned by [`../../rhodium/std/plru.rhdl`](../../rhodium/std/plru.rhdl); core cache
changes should run that fixture together with their integration fixtures.

Pass several paths to one invocation when a contract spans components. The
wrapper supplies the persistent worktree-specific root when none is selected.
After changing imports or package layout, run:

```sh
bash cores/check-boundaries.sh
```

For changes crossing reusable components and RV5Stage integration, run the
end-to-end core fixture set:

```sh
moon run rhodium:rv5stage-test
```

Use [`../rv5stage/DEVELOPING.md`](../rv5stage/DEVELOPING.md) for narrower ownership
and backend-fixture guidance.
