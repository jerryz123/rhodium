<!-- Records the accepted migration from completed-result queues to issue-scheduled register writes. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Scheduled writeback

Fixed-latency execution must reserve its destination write cycle before launch.
Its result writes on that cycle, without a queue waiting for register-file
arbitration. Reservation state contains timing and ownership, not result data.
Variable-latency producers retain their response until an unreserved write
opportunity. Already-issued fixed operations always keep their reservation.

## Implementation sequence

1. Add a named-core write-cycle calendar and directed collision/reset tests.
   Integrate the shared multiplier and GPR completion path. Admit pipelined
   scalar multiplication in ID with a promised EX launch and GPR return cycle;
   carry WB authorization alongside fixed-latency arithmetic.
2. Replace the vector ordered compute-data drain with direct scheduled writes.
   Preserve metadata lifetimes and memory ordering. Add row-level WAW protection
   alongside existing RAW protection, and prove that older operand reads cannot
   be overwritten. Keep the single registered sequencer and admission FIFO.
3. Schedule fixed FP writes to their actual GPR/FPR/VRF destination. Remove fixed
   result queues. Make iterative FP completion retain its arithmetic state until
   accepted; never discard a one-cycle HardFloat completion pulse.
4. Propagate slow-load backpressure through destination selection. Keep only
   genuine memory response/reassembly/carry storage. Reserve alignment and final
   VRF write timing before releasing packed rows.
5. Update completion lineage and owning documentation. Validate focused scalar,
   vector overlap/muldiv/FP/memory, packed-load, and event fixtures, followed by
   scalar/vector CoreMark traces through the managed build cache.

## Invariants

- Admission and all fixed resource reservations happen atomically. Scalar ID
  may book the next EX cycle; that grant cannot be revoked by service arbitration.
- Latency includes every stage between launch and the architectural write.
- Fixed results never wait; variable results cannot displace fixed writes.
- Unrelated destinations may complete out of order. RAW/WAW/WAR, certification,
  faults, reduction recurrence, flags, saturation, and scalar authorization
  remain correct independently of metadata reclamation order.
- Operand preparation and memory assembly are not completed-result queues.
- Prevent variable-response starvation by withholding new reservations when
  necessary, never by revoking an existing reservation.

## Validation status

The integrated core uses scheduled multiply and fixed FP returns, GPR/VRF
write-cycle calendars, direct vector completion, row-level WAW protection, and
producer-held variable results. Standalone services retain their elastic mode
for clients that do not provide the scheduled-write timing contract.

The direct EX-admission refinement passes the focused multiply, tagged integer
service, base core, RV64D WB authorization, and shared scalar/vector regressions. The trace checks five
cycles from multiply EX to dependent ID, four consecutive independent launches,
and a canceled/retried launch. The scalar adapter checks 47 authorized five-cycle
returns and 16 canceled launches across reset. Shared vector execution checks
5,083 stores over 76,438 cycles after rebasing onto the updated vector reduction
schedule.

The broader scheduled-writeback validation below predates this refinement;
its full-SoC and CoreMark runs have not been repeated for direct EX admission.
Earlier focused RTL simulation passed:

- Calendar: 224 reservations, 392 collisions, 63 simultaneous grants, pause and reset.
- Scalar multiply: dependency, independent progress, and wrong-path cancellation.
- Vector multiply/divide: 5,083 checked stores over 77,790 cycles.
- Scalar/vector FP: 319 checked stores; standalone shared FP: 441 tagged results.
- Scheduled FP: fixed two-cycle return checks, continuous-stream variable fairness,
  arithmetic/tag checks, and reset without completed-result storage.
- Scalar FP pipeline and RV32F/RV64D cores: hazards, memory/FP authorization,
  replay, and fault isolation.
- Vector overlap: independent early completion and conflicting-row RAW/WAW ordering.
- Packed memory: 316 cases, including alignment, carry handling, and write-port stalls.
- Vector events: 108 issues, 96 completions, 15 out-of-order responses, retry and reset.
- Integrated cache: hit latency, hit-under-miss, fences, forwarding, and result lineage.

The boundary audits, 60 focused cache/FP host checks, 105 mux/verifier checks,
and whitespace checks pass. Removing the FP request queue exposed an overly
conservative one-hot mux dependency rule; it now preserves independent aggregate
fields while still rejecting selector feedback and same-field cycles.
Scalar EX multiplication now shares the EX checkpoint's explicit fork with
the pipeline and lookup consumers, preserving launch timing and trace lineage.
Full-SoC elaboration, hardware verification, event instrumentation, and native
simulation pass using the persistent simulator build cache. The trace harness
explicitly leaves the generated event-activity output unconnected.

Both one-iteration CoreMark variants pass their required CRCs: vector-enabled
457,527 benchmark ticks and scalar 457,622 ticks. These are functional runs, not
official ten-second scores. Both traces import without errors and pass the
core-stage, frontend, and stall-lineage SQL checks. The vector trace also passes
the shared-cache memory-lineage checks. All 38,374 vector completions retain an
exact issue ancestor, including 36 slow-memory completions through the intervening
memory-result checkpoint. Direct issue-to-completion delays are one, three, or
six cycles for the paths exercised by this workload.
