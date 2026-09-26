<!-- Defines certified vector memory handoff, downstream retry ownership, and focused validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Certified memory handoff

MEM-certified single-page instructions carry their mapping into execution.
They do not install or acquire the shared fallback translation window. Convert
each issued address with that captured mapping before retaining its request.
Potentially faulting and fallback two-page operations keep their existing
sequencer checkpoint and page-window lifetime.

Retain certified requests, not completed results, in a bounded LSU retry ring.
Capture once, resolve their execution slots at the ordinary execute boundary,
and retry cache rejection locally. Accepted transactions never replay. Cache
hit results either write immediately or repeat the side-effect-free lookup;
variable responses remain backpressurable at their producer.
Store-hit commit is independent of result-port readiness; only acknowledgement
tags, not data, may wait after that irreversible boundary.

Certified ordinary memory releases the single sequencer on its final scheduled
beat, like compute. Zero-stride splats release after their first active probe
is captured downstream, keeping their existing mask-prefix and drain owner.
Completion metadata, destination hazards, packed carry, and memory ordering
outlive sequencing. Requests capture store direction and physical addresses;
neither late feedback nor retries may consult a younger descriptor.

Validate consecutive certified loads and independent successors, rejected first
and final beats, different captured mappings, hit/slow-response contention,
masked splats, packed assembly, and the existing faulting fallback. Run focused
admission, overlap, packed RV32, integrated vector-memory, MMU, and trace checks.
Regenerate EDN with the persistent simulator build to check the motivating case.

## Validation

The focused assertion-enabled CIRCT/Verilator run passes admission, overlap,
RV32 packed memory (298 cases), integrated vector memory with eight and one
completion slots (256 signatures each), MMU replay, and vector events.
Admission checks consecutive distinct physical mappings, first/final request
rejection, retained older responses, and independent younger work. Boundary
checks and `git diff --check` pass.

The incremental SimpleSoC trace build runs the same EDN ELF successfully with
assertions enabled. Total traced cycles fall from 75,512 to 71,739 (5.0% fewer).
Across 1,250 executions of the VLE at `0x80000082`, average MEM-to-first-sequence
latency falls from 12.36 to 9.42 cycles. Core, frontend, and vector-memory event
SQL checks pass, with no Perfetto import errors. These are whole-run simulation
measurements, not an isolated steady-state kernel score.
