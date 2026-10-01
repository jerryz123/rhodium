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

1. `0001-return-parser-errors-as-exceptions.patch` converts the two process-abort
   paths into exceptions suitable for an embedded library.
2. `0002-recognize-zic64b-property.patch` registers the opcode-free Zic64b
   cache-block property missing from the pinned parser.
3. `0003-recognize-supm-property.patch` registers the opcode-free Supm
   execution-environment property missing from the pinned parser.
4. `0004-notify-simif-of-icache-flush.patch` exposes architectural FENCE.I
   synchronization to external physical instruction caches. Translation and
   privilege changes still invalidate Spike's decoded cache without notifying
   the external physical cache.
5. `0005-enable-fiom-with-supervisor.patch` makes FIOM writable in machine and
   supervisor environment configuration CSRs whenever S-mode is supported,
   including Bare-only harts.
6. `0006-zero-unimplemented-hpm-controls.patch` keeps the HPM counter-control
   bits read-only zero when Zihpm exposes aliases for zero implemented counters.
7. `0007-separate-supervisor-interrupt-pins.patch` preserves software-writable
   supervisor pending bits when the external interrupt levels change.
8. `0008-check-lrsc-physical-access.patch` lets embedded platforms check the
   full LR/SC physical range and permissions before testing reservation state.
9. `0009-route-cbo-zero-through-simif.patch` lets embedded coherent caches
   perform CBO.ZERO without exposing a direct host-memory pointer.
10. `0010-scope-amo-memory-access.patch` gives embedded coherent caches the full
    physical AMO operand and an exception-safe scope around Spike's load/store.
11. `0011-check-cbo-management-physical-access.patch` checks full-block CBO
    management permissions independently of LR/SC reservation support.
12. `0012-keep-mxr-writable-with-bare-supervisor.patch` retains MXR CSR
    readback with S-mode even when paging is unavailable, while SUM stays zero.
13. `0013-keep-tvm-writable-with-bare-supervisor.patch` retains TVM CSR
    readback and S-mode virtual-memory intercepts even when paging is absent.
14. `0014-expose-rv32-medelegh.patch` exposes the architectural high-half
    exception-delegation CSR on RV32 S-mode harts.
15. `0015-keep-medeleg-page-fault-bits-in-bare-mode.patch` retains the
    page-fault delegation bits on S-mode harts even when paging is unavailable.
16. `0016-recognize-sha-properties.patch` registers Sha and its opcode-free
    guarantees, mapping Ssstateen to the existing state-enable implementation.
17. `0017-delegate-only-implemented-hypervisor-interrupts.patch` leaves
    `mideleg.SGEIP` zero because the pinned model implements no guest external
    interrupt sources (GEILEN=0).
18. `0018-gate-stateen-csrind-on-sscsrind.patch` exposes the state-enable CSRIND
    bit only when the controlled supervisor indirect CSRs exist.
19. `0019-implement-sscofpmf-counter.patch` adds counter 3 when Sscofpmf is
    selected, with 64-bit state, privilege filtering, RV32 halves, sticky
    overflow and precise local interrupt delivery. It extends patch 0006's
    enable mask only for this implemented slot. Event 1 counts functional
    execution attempts and event 2 counts retirement; other slots stay zero.
20. `0020-fix-hs-overflow-interrupt-priority.patch` places HS counter overflow
    below guest-external and virtual interrupt sources, preserving the H
    extension's specified order without changing target-privilege arbitration.
21. `0021-recognize-supervisor-properties.patch` accepts Ssccptr, Sstvecd,
    Sstvala, Sscounterenw, and Ssu64xl as opcode-free architectural properties,
    retaining the exact published ISA string without changing execution.
22. `0022-mask-unimplemented-guest-interrupt-enable.patch` keeps `mie.SGEIE`
    and its `hie` write alias read-only zero for the pinned model's GEILEN=0,
    while retaining the implemented virtual-supervisor interrupt enables.

When advancing the submodule, apply each patch with `git apply --check`, remove
changes that have landed upstream, rebase the remaining patches, and run the
RHEG Perfetto tests plus the FESVR simulator smoke. The materializer rejects a
patch that no longer applies cleanly.
