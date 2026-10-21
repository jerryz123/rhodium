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
respective scalar WB and LSU boundaries.

`decode.rhdl` provides `VectorControl`, authored decode relations, and
`VectorOperationLegality`. A named core composes those relations with its scalar
source, writeback, and system columns. `register-file.rhdl`, `packing.rhdl`, and
`execute.rhdl` can also be used independently.
`instruction-metadata.rhm` provides the selected base vector instruction domain
and scalar source/configuration/destination roles without choosing core controls.

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
memory instead retains an authorization checkpoint: retry discards only its
unaccepted suffix. Accepted requests and responses survive. A scalar redirect
must not cancel a descriptor that has already become nonspeculative.

`precheck.rhdl` computes conservative memory footprints; the reusable
[MMU contracts](../mmu/vector.rhdl) and [page window](../mmu/vector-window.rhdl)
support certification. The caller owns the DTLB/PTW arbitration and WB context
revalidation. A failed certificate selects precise execution, not an exception.

[`VectorAdmission`](admission.rhdl) supplies a descriptor queue, head
certification, pinned-window release, and registered early/precise outcomes.
`~dispatch_depth` is a positive host integer, defaulting to two stored descriptors
in addition to the active sequencer. RV2Wide selects four; RV5Stage retains two.
Its explicit `~pipe` option selects whether admission credit can depend on
same-cycle dequeue. Fault provenance is an opaque caller-supplied type.
[`VectorReplay`](replay.rhdl) retains certified requests through local retries;
attempts may run ahead, but release remains ordered. A split takeover can move
the attempt cursor past the oldest retained request without releasing its owner.
`VectorStoreAcknowledgements` retains one acknowledgement per completion slot,
independently of cache commit readiness.

Fixed shared services must honor their declared return latency and reserved
write cycle. Variable services retain tagged results until accepted. Scalar
results similarly require the caller to honor `scalar_writeback` reservations.
Completion, sequencing release, and architectural retirement are separate events.

## Integration boundaries

The scalar core owns profile selection, vector CSR state, scalar operand capture,
WB admission, memory ordering, precise trap retirement, and interrupt draining.
The LSU adapter owns cache lookup/commit timing, certified-request retention,
physical response routing, and fault provenance. None of these are inferred from
the execution engine's current descriptor.

See [RV5Stage's vector contract](../rv5stage/vector/README.md) for its supported
instruction profiles and scalar overlap policy, and [shared SIMD](../README.md#packed-simd-integer-alu)
for the underlying integer datapath.
