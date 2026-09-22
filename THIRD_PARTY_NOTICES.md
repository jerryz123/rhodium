<!-- Identifies separately licensed source and external repositories associated with Rhodium. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Third-party notices

Except where a file or directory states otherwise, original Rhodium content is
licensed under the [Apache License 2.0](LICENSE).

## Berkeley HardFloat

The complete [`hardfloat/`](hardfloat/) package is a Rhodium port derived from
Berkeley HardFloat. It remains under the BSD-style University of California and
SiFive terms reproduced in [`hardfloat/LICENSE.md`](hardfloat/LICENSE.md).
Distributions containing that package, including binary or generated-hardware
forms derived from it, must preserve its applicable notices and license terms.

## Git submodules

The following paths are independently maintained Git submodules. Their contents
are separate works governed by the license at each pinned upstream revision;
the repository's Apache-2.0 license does not replace those terms:

- `riscv/riscv-arch-test`
- `riscv/riscv-isa-sim`
- `riscv/riscv-isa-tests`
- `sims/program-test/coremark`
- `sims/program-test/embench-iot`
- `vlsi/double_wide_openframe`

CoreMark is used only from its pinned upstream checkout. Its license, result
reporting conditions, acceptable-use terms, and trademark notice remain in
[`sims/program-test/coremark/LICENSE.md`](sims/program-test/coremark/LICENSE.md).
Rhodium uses CoreMark as a short functional workload and does not report a
benchmark score.

Embench-IoT is used from its recorded upstream development-tree revision under
GPL-3.0, with additional per-workload terms identified by upstream source
headers; its license text remains in
[`sims/program-test/embench-iot/COPYING`](sims/program-test/embench-iot/COPYING).
Rhodium runs it as a bounded functional suite and does not report an Embench
performance score.

## Spike disassembler

The RHEG Perfetto exporter compiles the ISA parser and disassembler from the
pinned [`riscv/riscv-isa-sim`](riscv/riscv-isa-sim/) submodule, and simulator
setup builds FESVR from the same source. Both use the ordered Rhodium patch
series under
[`riscv/riscv-isa-sim-patches/`](riscv/riscv-isa-sim-patches/). The upstream
source remains under the University of California BSD license reproduced in
[`LICENSE.riscv-isa-sim`](riscv/riscv-isa-sim-patches/LICENSE.riscv-isa-sim).

External tools, PDK collateral, libraries, and workloads downloaded or supplied
during setup and testing are not distributed as original Rhodium content. Their
own licenses apply.
