<!-- Documents the ordered downstream patch series layered over the pinned riscv-isa-sim submodule. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# riscv-isa-sim patches

[`../riscv-isa-sim/`](../riscv-isa-sim/) is the single pinned upstream source
for both RHEG instruction disassembly and the simulator's FESVR library. The
ordered [`series`](series) file lists the narrow Rhodium-owned integration
changes needed by those consumers.

The shared [`patched_submodule.py`](../patched_submodule.py) tool copies the
pristine submodule into a consumer's build directory and applies the series
there. The submodule remains unmodified. The resulting source remains subject
to riscv-isa-sim's BSD license in
[`LICENSE.riscv-isa-sim`](LICENSE.riscv-isa-sim).

The queue is ordered so each change can be reviewed and removed independently:

1. `0001-return-parser-errors-as-exceptions.patch` converts process-abort paths into embedding-safe exceptions.
2. `0004-notify-simif-of-icache-flush.patch` notifies external physical instruction caches only for architectural FENCE.I.
3. `0005-enable-fiom-with-supervisor.patch` keeps FIOM writable on S-mode harts, including Bare.
4. `0006-zero-unimplemented-hpm-controls.patch` keeps absent HPM control bits read-only zero.
5. `0007-separate-supervisor-interrupt-pins.patch` keeps external supervisor interrupt levels separate from writable pending state.
6. `0008-check-lrsc-physical-access.patch` checks complete LR/SC physical ranges before native reservation-set matching.
7. `0009-route-cbo-zero-through-simif.patch` routes full-block CBO.ZERO through the coherent embedding.
8. `0010-scope-amo-memory-access.patch` exposes a translated AMO operand and exception-safe load/store scope.
9. `0011-check-cbo-management-physical-access.patch` checks full-block management permissions independently of reservations.
10. `0012-keep-mxr-writable-with-bare-supervisor.patch` retains Bare-only S-mode MXR readback.
11. `0013-keep-tvm-writable-with-bare-supervisor.patch` retains Bare-only TVM readback and interception.
12. `0015-keep-medeleg-page-fault-bits-in-bare-mode.patch` retains S-mode page-fault delegation bits without paging.
13. `0017-delegate-only-implemented-hypervisor-interrupts.patch` keeps zero-GEILEN SGEIP nondelegatable.
14. `0019-implement-sscofpmf-counter.patch` implements counter 3, mode filters, precise inhibit/write suppression, overflow and local interrupts.
15. `0022-mask-unimplemented-guest-interrupt-enable.patch` keeps both SGEIE aliases zero while preserving VS interrupt enables.
16. `0024-keep-hedeleg-fault-bits-writable.patch` keeps H-mandated software-check and hardware-error delegation writable.

At upstream pin `5a760667bf0d0ab893173b9eeff9c109c8d7dd9a`, ISA profile
properties, RV32 delegation CSRs, P1P13 gating, CSRIND gating, and overflow
interrupt ordering are native. Their former downstream patches are retired.
See [the RISC-V contributor guide](../DEVELOPING.md) for validation ownership.

When advancing the submodule, apply each patch with `git apply --check`, remove
changes that have landed upstream, rebase the remaining patches, and run the
RHEG Perfetto tests plus the FESVR simulator smoke. The materializer rejects a
patch that no longer applies cleanly.
