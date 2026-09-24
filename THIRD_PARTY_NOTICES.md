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

- `sw/opensbi`
- `sw/riscv-arch-test`
- `riscv/riscv-isa-sim`
- `sw/riscv-isa-tests`
- `sw/coremark`
- `sw/embench-iot`
- `sw/bringup-bench`
- `sw/litmus-tests-riscv`
- `vlsi/double_wide_openframe`

CoreMark is used only from its pinned upstream checkout. Its license, result
reporting conditions, acceptable-use terms, and trademark notice remain in
[`sw/coremark/LICENSE.md`](sw/coremark/LICENSE.md).
Rhodium uses CoreMark as a short functional workload and does not report a
benchmark score.

Embench-IoT is used from its recorded upstream development-tree revision under
GPL-3.0, with additional per-workload terms identified by upstream source
headers; its license text remains in
[`sw/embench-iot/COPYING`](sw/embench-iot/COPYING).
Rhodium runs it as a bounded functional suite and does not report an Embench
performance score.

Bringup-Bench is used from its pinned upstream checkout. The suite's own code
is Apache-2.0; adapted benchmark components retain their individual upstream
notices. The upstream license remains in
[`sw/bringup-bench/LICENSE`](sw/bringup-bench/LICENSE).
Rhodium checks output hashes as a functional workload suite, not a benchmark
score. The ordered downstream patch series in
[`sw/bringup-bench-patches/`](sw/bringup-bench-patches/)
is applied only to build-local copies; the pinned submodule remains pristine.

The RISC-V litmus corpus and its precomputed Herd result log are used from the
pinned upstream checkout under BSD-2-Clause. Its license remains in
[`sw/litmus-tests-riscv/LICENCE`](sw/litmus-tests-riscv/LICENCE). The
Rhodium-owned bare-metal runtime and builder are separate Apache-2.0 files.
The optional litmus7 path compiles generated C and helper sources from an
externally supplied herdtools7 installation. Those generated sources identify
themselves as CeCILL-B-licensed; distributing their compiled ELFs requires
preserving the applicable herdtools7 license and notices. The generator and
its generated sources are not checked into Rhodium.

## Spike disassembler

The RHEG Perfetto exporter compiles the ISA parser and disassembler from the
pinned [`riscv/riscv-isa-sim`](riscv/riscv-isa-sim/) submodule, and simulator
setup builds FESVR from the same source. Both use the ordered Rhodium patch
series under
[`riscv/riscv-isa-sim-patches/`](riscv/riscv-isa-sim-patches/). The upstream
source remains under the University of California BSD license reproduced in
[`LICENSE.riscv-isa-sim`](riscv/riscv-isa-sim-patches/LICENSE.riscv-isa-sim).

## OpenSBI

OpenSBI is used unmodified from its pinned upstream checkout under its
BSD-2-Clause license, which remains in
[`sw/opensbi/COPYING.BSD`](sw/opensbi/COPYING.BSD).

External tools, PDK collateral, libraries, and workloads downloaded or supplied
during setup and testing are not distributed as original Rhodium content. Their
own licenses apply.
