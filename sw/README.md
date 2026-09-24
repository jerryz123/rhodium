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

Use the [simulator workflows](../sims/README.md) to select a concrete SoC,
generate its target descriptor, build software, and run the resulting ELFs.
For example:

```sh
make -C sims program-test-setup
make -C sims isa-test SOC=single CORE=rv5stage
make -C sims bringup-test SOC=single CORE=spike
make -C sims arch-test ACT_CONFIGURATION=single-core-rv5stage-soc
make -C sims opensbi-test SOC=single CORE=rv5stage
```

The Spike/FESVR upstream under [`riscv/riscv-isa-sim`](../riscv/riscv-isa-sim/)
is a host simulator dependency, not target software. Simulator qualification
payloads remain under [`sims/tests/`](../sims/tests/) and
[`sims/opensbi/tests/`](../sims/opensbi/tests/) because they test harness behavior.

Contributors changing a source, port, builder, or patch series should read
[`DEVELOPING.md`](DEVELOPING.md).
