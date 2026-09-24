<!-- Documents pinned target software and the supported SoC workload entry points. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Target software

`sw/` contains software built to run on Rhodium SoCs: pinned upstream sources,
Rhodium bare-metal ports, downstream source patches, and ELF builders. The
upstream submodules remain pristine. Generated sources, firmware, and ELFs live
under the build roots, not in this directory.

| Software | Source and adaptation |
|---|---|
| RISC-V ISA tests and benchmarks | [`riscv-isa-tests/`](riscv-isa-tests/) and [`build/`](build/) |
| UDB-driven architectural tests | [`riscv-arch-test/`](riscv-arch-test/) and [`riscv-arch-test-patches/`](riscv-arch-test-patches/) |
| OpenSBI firmware | [`opensbi/`](opensbi/) and [`build/opensbi.py`](build/opensbi.py) |
| CoreMark | [`coremark/`](coremark/) and [`coremark-riscv-baremetal/`](coremark-riscv-baremetal/) |
| Embench-IoT | [`embench-iot/`](embench-iot/) and [`embench-iot-riscv-baremetal/`](embench-iot-riscv-baremetal/) |
| Bringup-Bench | [`bringup-bench/`](bringup-bench/), [`bringup-bench-patches/`](bringup-bench-patches/), and [`bringup-bench-riscv-baremetal/`](bringup-bench-riscv-baremetal/) |
| RISC-V memory-model litmus tests | [`litmus-tests-riscv/`](litmus-tests-riscv/) and [`litmus-riscv-baremetal/`](litmus-riscv-baremetal/) |

Use the [simulator workflows](../sims/README.md) to select a concrete SoC,
generate its target descriptor, build software, and run the resulting ELFs.
For example:

```sh
make -C sims program-test-setup
make -C sims isa-test SOC=single CORE=rv5stage
make -C sims bringup-test SOC=single CORE=spike
make -C sims arch-test ACT_CONFIGURATION=single-core-rv5stage-soc
make -C sims opensbi-test SOC=single CORE=rv5stage
make -C sims litmus-smoke-test SOC=tiled CORE=rv5stage LITMUS7=/path/to/litmus7
make -C sims litmus-smoke-test SOC=tiled CORE=spike LITMUS7=/path/to/litmus7
```

The Spike/FESVR upstream under [`riscv/riscv-isa-sim`](../riscv/riscv-isa-sim/)
is a host simulator dependency, not target software. Simulator qualification
payloads remain under [`sims/tests/`](../sims/tests/) and
[`sims/opensbi/tests/`](../sims/opensbi/tests/) because they test harness behavior.

The litmus7 profiles share one target-bound builder and port.
`litmus-smoke-test` runs a checked-in nine-case selection on both tiled cores
in CI, while `litmus-full-test` is a local/manual run of every target-supported
case. A source-tree litmus7 build also needs `LITMUS7_LIBDIR`; see the
[simulator workflow](../sims/README.md) for sharding, result paths, and the
CI-pinned herdtools7 revision. Neither Make target installs litmus7.

The older bare-metal litmus adapter discovers every case in the pinned upstream
non-mixed-size inventory that its branch-free RV64I translator can preserve
exactly and that has an unambiguous precomputed Herd RVWMO state set. The
current pin yields 84 cases, including MP, SB, and IRIW+addrs. Use a tiled SoC
for their two, three, or four active harts. `LITMUS_RUNS` changes the bounded
repetitions; `LITMUS_CASES` defaults to `all` or accepts a comma-separated
subset of supported names. A passing run means no forbidden state was observed,
not that every model-allowed state appeared or that the full upstream corpus
is covered.

The litmus7 profiles use upstream's RISC-V code generator instead of the
branch-free instruction translator. Supply an installed executable with
`LITMUS7=/path/to/litmus7` to a profile target; source-tree builds
also need `LITMUS7_LIBDIR=/path/to/herdtools7/litmus/libdir`. The port runs
litmus7's static worker pool on physical harts without pthreads. Its output
histogram is checked by the host runner against the pinned Herd states, since
litmus7's own condition result is not a memory-model pass/fail result. The
bare-metal port emits the test identity and complete histogram, omitting the
unused witness footer to limit simulated HTIF traffic. The path discovers
additional candidate cases, including branches and memory outcomes. It filters
Zalasr acquire/release ordinary loads and stores when the selected target lacks
`zalasr`; a case is qualified only after its ELF builds and runs on the selected
SoC. `litmus-test` remains the explicit 84-case adapter target, with
`LITMUS_CASES=MP,LB+ctrls` for a bounded adapter selection. The profile path
does not download or install herdtools7. For a whole-inventory
build survey, the builder's `--keep-going` option retains successful ELFs and
records failed case names and stages in `manifest.json`, then exits nonzero if
any case failed. Its discovered-case count is not a runnable-case count.

Contributors changing a source, port, builder, or patch series should read
[`DEVELOPING.md`](DEVELOPING.md).
