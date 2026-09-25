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
4. `0004-notify-simif-of-icache-flush.patch` exposes Spike's architectural
   instruction-cache flush to simulators that keep an external instruction
   cache model.
5. `0005-enable-fiom-with-supervisor-translation.patch` makes FIOM writable
   in machine and supervisor environment configuration CSRs for translated harts.
6. `0006-zero-unimplemented-hpm-controls.patch` keeps the HPM counter-control
   bits read-only zero when Zihpm exposes aliases for zero implemented counters.
7. `0007-separate-supervisor-interrupt-pins.patch` preserves software-writable
   supervisor pending bits when the external interrupt levels change.
8. `0008-check-lrsc-physical-access.patch` lets embedded platforms check the
   full LR/SC physical range and permissions before testing reservation state.
9. `0009-route-cbo-zero-through-simif.patch` lets embedded coherent caches
   perform CBO.ZERO without exposing a direct host-memory pointer.

When advancing the submodule, apply each patch with `git apply --check`, remove
changes that have landed upstream, rebase the remaining patches, and run the
RHEG Perfetto tests plus the FESVR simulator smoke. The materializer rejects a
patch that no longer applies cleanly.
