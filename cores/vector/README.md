<!-- Describes the shared XLEN-wide vector execution engine and its caller-owned integration boundary. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Shared vector execution

This package supplies the reusable RVV execution machinery: decoded controls,
legality checks, one sequencer, a vector register file, packed SIMD execution,
shared arithmetic-service adapters, and persistent completion ownership.
It supports XLEN-wide rows for RV32 and RV64 with configurable VLEN. It does
not select a hart profile or implement scalar-pipeline retirement.
See [DEVELOPING.md](DEVELOPING.md) for maintenance and validation.

## Entry points

```rhdl
import:
  lib("cores/vector/pipeline.rhdl") open
  lib("cores/vector/bundles.rhdl") open

inst execution(VectorExecution(xlen, vlen, Context, ~vector_completion_slots: 8))
```

`Context` is caller-owned metadata carried with memory attempts. The caller
connects decoded descriptor admission, memory decisions and completions, shared
FP/multiply/divide services, scalar write reservations, and architectural update
outputs. [RV5Stage's adapter](../rv5stage/vector.rhdl) and
[RV2Wide's adapter](../rv2wide/vector.rhdl) connect this service to their
respective scalar WB and LSU boundaries through `VectorBackend`.

[`backend.rhdl`](backend.rhdl) composes `VectorAdmission` with `VectorExecution`
for WB-admitted descriptors. It exposes memory issue, decision, completion,
store acknowledgement, and precise-outcome streams, with caller-typed fault
provenance. `~pipe` explicitly selects admission credit timing; `~dispatch_depth`
selects descriptor capacity independently of completion slots. The caller supplies
LSU `memory_start_ready` and memory-drained indications and selects arithmetic timing
and packed multiplication. The composition adds no queues beyond admission and
execution themselves; scalar retirement policy and LSU staging remain outside.

`decode.rhdl` provides `VectorControl`, authored decode relations, and
`VectorOperationLegality`. A named core composes those relations with its scalar
source, writeback, and system columns. `register-file.rhdl`, `packing.rhdl`, and
`execute.rhdl` can also be used independently.
`instruction-metadata.rhm` provides the selected base vector instruction domain
and scalar source/configuration/destination roles without choosing core controls.

[`profile.rhm`](profile.rhm) provides `check_vector_profile` for ELEN and scalar
FP dependencies, and `vector_execution_half_precision` for the union of scalar
and vector half-precision execution needs. That execution capability does not
enable additional scalar ISA instructions. Named cores still choose VLEN,
queue capacities, scheduling, and their advertised ISA profile.

## Ownership and timing

The current implementation has the following structure:

```text
nonspeculative descriptor
  -> single sequencer -> synchronous operand fetch -> SIMD / shared services
        |                         |                         |
        +---- row dependencies ---+--- completion slots ---+-> VRF write
                                  |
                                  +-> caller's LSU -> decisions / tagged returns
```

The sequencer advances only when operands, completion ownership, and any
fixed-latency write reservation are available together. It releases an instruction
after its sequencing obligation ends; accepted results may remain outstanding.
Instruction owners and completion slots preserve identity across successor
admission. Source/destination row dependencies prevent RAW and WAW violations.
The VRF has three general read ports, one write port, and a separate `v0` mask
shadow updated by the same writes.

Compute and certified-memory beats may overlap older delayed results. Faultable
memory retains an ordered authorization checkpoint while aligned elements
pipeline within the completion-slot capacity: retry discards only its
unaccepted suffix. Misaligned elements hold younger sequencing until their
split-access decision; fault-only-first elements remain serialized. Accepted
requests and responses survive. A scalar redirect must not cancel a descriptor
that has already become nonspeculative.

`precheck.rhdl` computes conservative memory footprints; the reusable
[MMU contracts](../mmu/vector.rhdl) and [page window](../mmu/vector-window.rhdl)
support certification. The caller owns the DTLB/PTW arbitration and WB context
revalidation. A failed certificate selects precise execution, not an exception.
`vector_fast_start` prepares a pointer-normalized start and wrap proof from the
fast geometry. `vector_fast_range` checks that start for alignment and one-page
eligibility and rounds the range outward to whole XLEN memory beats. Both are
combinational, so callers can place a stage boundary between them. Empty ranges
are reported separately and must not issue a fast check.

[`VectorAdmission`](admission.rhdl) supplies a descriptor queue, head
certification, pinned-window release, and registered early/precise outcomes.
`~dispatch_depth` is a positive host integer, defaulting to two stored descriptors
in addition to the active sequencer. RV2Wide selects six; RV5Stage retains two.
Its explicit `~pipe` option selects whether admission credit can depend on
same-cycle dequeue. Fault provenance is an opaque caller-supplied type.
`VectorBackend` can install uncertified memory on an older instruction's final
sequencing edge. It holds the first faultable read until `memory_start_ready`
guarantees that the fixed-latency operand result can enter the LSU next cycle.
A successful final certified attempt supplies same-edge release credit; a retry,
split, blocked return, or new durable capture does not. Accepted slow responses
may remain outstanding. Per-row data, index, and mask hazards still apply;
arithmetic results need not drain first. The wait appears as a normal sequencing
stall, without an extra request or result queue.

[`VectorMemoryIssue`](memory-issue.rhdl) supplies the common LSU issue frontend.
Its Decoupled `issue` accepts vector memory tokens; Valid `lookup` presents
ordinary beats one cycle later and packed loads in the issue cycle when no
older registered beat owns lookup. Certified beats remain retained across
retries and report `durable` authorization one cycle after capture, even during
recovery. Faultable beats wait behind retained requests and the caller's
`blocked` condition. The caller supplies recovery, retry, split-skip, and ordered
release decisions and retains responsibility for suppressing younger effects.
`occupied` and `oldest_tag` describe only retained certified requests;
`empty_after_release` accounts for same-edge release and capture, not downstream
lookup stages or accepted responses. Core adapters combine that credit with
their own pipeline and split state to compute `memory_start_ready`.

[`VectorReplay`](replay.rhdl) retains certified requests through local retries;
attempts may run ahead, but release remains ordered. A split takeover can move
the attempt cursor past the oldest retained request without releasing its owner.
Store hits report `store_completed` independently of the backpressurable data
completion port, so load returns cannot postpone store-hit bookkeeping. Accepted
slow stores still complete through the tagged memory-return port, but consume
no VRF write grant when their acknowledgement arrives.

Fixed shared services must honor their declared return latency and reserved
write cycle. Variable services retain tagged results until accepted. Scalar
results similarly require the caller to honor `scalar_writeback` reservations.
Completion, sequencing release, and architectural retirement are separate events.
Packed completion slots are reclaimed in order. A completed store does not need
a VRF write-port grant to release its slot. Complete, lane-aligned packed load
rows reserve their hit write cycle at sequencing and write directly on return,
without waiting for the ordered head or shared byte aligner. Packed loads launch
their cache lookup at issue, without the ordinary LSU's extra EX register. An
uncontended full-row hit writes three cycles after sequencing and reclaims its
slot on that same edge when it is the ordered head and no carry barrier remains.
Writes ahead of the head retain their metadata until ordered reclamation. Delayed full-row
returns remain backpressurable until a write cycle is available. Partial,
rotated, and segmented rows retain ordered assembly and carry arbitration.
Tracing distinguishes `vector/complete` (execution results and actual packed-load
VRF writes) from `vector/slot.release` (ordered packed transport bookkeeping,
including stores). A slot release can coincide with an unrelated result completion.

### Packed integer multiplication

`VectorExecution(..., ~packed_multiply: #true)` selects dedicated packed
subword multiplication. RV2Wide enables it; RV5Stage retains the default shared
scalar service. A 32-bit row uses `SimdMultiplyAccumulate(32, 16)` and a 64-bit
row uses `(64, 32)`. Full-row-width multiplication and all division still use
the caller's scalar services. These choices do not change the numeric component's
physical-width API or the architectural instruction profile.

Low/high products, MAC/subtract, widening multiply/MAC, and fractional multiply
issue one packed destination beat per cycle when row dependencies, completion
slots, and write reservations permit. Widening issues two half-source beats per
source row, each producing one destination row. Masking, partial VL, and vstart
qualify destination bytes and saturation; inactive bytes remain undisturbed.
The third VRF port supplies old vd for MACs, while the separate v0 shadow
supplies predication. Full-width fallback and packed multiplication can overlap.

The read scheduler reserves the sole VRF port six cycles ahead: one operand-read
cycle, one execute stage, then the numeric pipeline's four cycles. Tags advance
with the feed-forward result; there is no multiplier backpressure or result
queue. All-masked beats use the same delayed completion path without writing
the VRF or changing saturation. Accepted owners survive successor admission
and memory replay. Variable memory/FP/divide returns wait for unreserved cycles.

## Integration boundaries

The scalar core owns profile selection, vector CSR state, scalar operand capture,
WB admission, memory ordering, precise trap retirement, and interrupt draining.
The LSU adapter owns cache lookup/commit timing, certified-request retention,
physical response routing, and fault provenance. None of these are inferred from
the execution engine's current descriptor.

See [RV5Stage's vector contract](../rv5stage/vector/README.md) for its supported
instruction profiles and scalar overlap policy, and [shared SIMD](../README.md#packed-simd-integer-alu)
for the underlying integer datapath.
