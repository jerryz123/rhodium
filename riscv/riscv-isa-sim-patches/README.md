<!-- Documents the ordered downstream patch series layered over the pinned riscv-isa-sim submodule. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# riscv-isa-sim patches

[`../riscv-isa-sim/`](../riscv-isa-sim/) is the single pinned upstream source
for both RHEG instruction disassembly and the simulator's FESVR library. The
ordered [`series`](series) file lists the Rhodium-owned changes needed to embed
the parser safely and accept architectural properties that do not add
instruction encodings.

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

When advancing the submodule, apply each patch with `git apply --check`, remove
changes that have landed upstream, rebase the remaining patches, and run the
RHEG Perfetto tests plus the FESVR simulator smoke. The materializer rejects a
patch that no longer applies cleanly.
