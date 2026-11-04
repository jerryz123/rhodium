<!-- Documents pinned target software and the supported SoC workload entry points. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Target software

`sw/` contains software built to run on Rhodium SoCs: pinned upstream sources,
Rhodium bare-metal ports, downstream source patches, and ELF builders. The
upstream submodules remain pristine. Generated sources, firmware, and ELFs live
under the build roots, not in this directory.

The RISC-V, CoreMark, Embench-IoT, Bringup-Bench, SKL, and Highway workloads use
`-O3`. CoreMark-scalar also disables auto-vectorization. The generated manifests
record the compiler flags and identify each binary's target and source revision.
The first-party bare-metal ports isolate HTIF mailboxes in 64-byte-aligned,
padded cache-line storage to avoid false sharing with workload data during host polling.

| Software | Source and adaptation |
|---|---|
| RISC-V ISA tests and benchmarks | [`riscv-isa-tests/`](riscv-isa-tests/), [`riscv-isa-tests-patches/`](riscv-isa-tests-patches/), and [`build/`](build/) |
| UDB-driven architectural tests | [`riscv-arch-test/`](riscv-arch-test/) and [`riscv-arch-test-patches/`](riscv-arch-test-patches/) |
| OpenSBI firmware | [`opensbi/`](opensbi/) and [`build/opensbi.py`](build/opensbi.py) |
| Linux boot-and-poweroff smoke | [`buildroot/`](buildroot/) and [`buildroot-riscv/`](buildroot-riscv/) (Linux build host required) |
| CoreMark | [`coremark/`](coremark/) and [`coremark-riscv-baremetal/`](coremark-riscv-baremetal/) |
| Embench-IoT | [`embench-iot/`](embench-iot/), [`embench-iot-patches/`](embench-iot-patches/), and [`embench-iot-riscv-baremetal/`](embench-iot-riscv-baremetal/) |
| Bringup-Bench | [`bringup-bench/`](bringup-bench/), [`bringup-bench-patches/`](bringup-bench-patches/), and [`bringup-bench-riscv-baremetal/`](bringup-bench-riscv-baremetal/) |
| SiFive Kernel Library (RVA23 vector suites) | [`skl/`](skl/), [`skl-patches/`](skl-patches/), and [`skl-riscv-baremetal/`](skl-riscv-baremetal/) |
| Highway (bounded native-RVV workloads) | [`highway/`](highway/) and [`highway-riscv-baremetal/`](highway-riscv-baremetal/) |
| RISC-V memory-model litmus tests | [`litmus-tests-riscv/`](litmus-tests-riscv/) and [`litmus-riscv-baremetal/`](litmus-riscv-baremetal/) |

Use the [simulator workflows](../sims/README.md) to select a concrete SoC,
generate its target descriptor, build software, and run the resulting ELFs.
For example:

```sh
moon run rhodium:program-test-setup
SOC=simple-rv5stage-rva23 moon run rhodium:isa-elfs
SOC=simple-rv5stage-rva23 moon run rhodium:coremark-elf
SOC=simple-rv5stage-rva23 moon run rhodium:sim-isa-test
SOC=simple-spike-rva23 moon run rhodium:sim-bringup-test
SOC=simple-rv5stage-rva23 COSIM=1 moon run rhodium:sim-skl-test rhodium:sim-highway-test
ACT_CONFIGURATION=simple-rv5stage-rva23 moon run rhodium:sim-arch-test
SOC=simple-rv5stage-rva23 moon run rhodium:sim-opensbi-test
SOC=tiled4core-rv2wide-rva23 LITMUS7=/path/to/litmus7 HERD7=/path/to/herd7 moon run rhodium:sim-litmus-smoke-test
SOC=tiled4core-spike-rva23 LITMUS7=/path/to/litmus7 HERD7=/path/to/herd7 moon run rhodium:sim-litmus-smoke-test
```

The Spike/FESVR upstream under [`riscv/riscv-isa-sim`](../riscv/riscv-isa-sim/)
is a host simulator dependency, not target software. Simulator validation
payloads remain under [`sims/tests/`](../sims/tests/) and
[`sims/opensbi/tests/`](../sims/opensbi/tests/) because they test harness behavior.

The litmus7 profiles share one target-bound builder and port.
`sim-litmus-smoke-test` runs a checked-in 64-case selection on `tiled4core` with all three core implementations
in CI, while `sim-litmus-full-test` is a local/manual run of every target-supported
case. Source-tree builds also need `LITMUS7_LIBDIR` and `HERD7_LIBDIR`; see the
[simulator workflow](../sims/README.md) for sharding, result paths, and the
CI-pinned herdtools7 revision. Neither profile installs these tools. Run Moon
directly from the repository root, with options in the environment.

All litmus builds use upstream's RISC-V litmus7 code generator. Supply an
installed executable with
`LITMUS7=/path/to/litmus7` and `HERD7=/path/to/herd7` to a profile target;
source-tree builds also need `LITMUS7_LIBDIR=/path/to/herdtools7/litmus/libdir`
and `HERD7_LIBDIR=/path/to/herdtools7/herd/libdir`. The port runs
litmus7's static worker pool on physical harts without pthreads. Its output
histogram is checked against independently enumerated Herd RVWMO states with
`lrscdiffok`, allowing SC to another address in the LR reservation set. The
model abstracts reservation-set layout; it does not check physical granule bounds.
litmus7's own condition result is not a memory-model pass/fail result. The
bare-metal port sends the test identity and complete histogram in one HTIF write,
without computing or formatting the unused witness footer. Histogram storage is
bounded by the requested sample count rather than all possible outcome
combinations; overflow fails explicitly without accepting a partial histogram.
The builder discovers model-backed candidate cases, including branches and
memory outcomes. It filters
Zalasr acquire/release ordinary loads and stores when the selected target lacks
`zalasr`; a case is validated only after its ELF builds and runs on the selected
SoC. `sim-litmus-test` uses the same builder for explicit subsets, with
`LITMUS_CASES=MP,LB+ctrls` and `LITMUS_RUNS` controlling selection and samples.
A passing run means no forbidden state was observed, not that every allowed
state appeared or that the full upstream corpus is covered. The builder
does not download or install herdtools7. For a whole-inventory
build survey, the builder's `--keep-going` option retains successful ELFs and
records failed case names and stages in `manifest.json`, then exits nonzero if
any case failed. Its discovered-case count is not a runnable-case count.

Contributors changing a source, port, builder, or patch series should read
[`DEVELOPING.md`](DEVELOPING.md).
