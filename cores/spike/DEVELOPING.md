<!-- Defines ownership and validation rules for the Spike-backed core. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing the Spike-backed core

Read the package [README](README.md) for the public surface. Keep Spike runtime,
DPI, private-cache, and transaction policy in this named-core package. Reuse pure
architectural descriptions from `riscv/` and implementation-neutral attachment
contracts from `cores/riscv/`; do not add Spike policy to either shared layer.

The DPI ABI in [`dpi/spike-dpi.rhdl`](dpi/spike-dpi.rhdl) and
[`dpi/spike_dpi.h`](dpi/spike_dpi.h) is one contract. Keep their argument and
result ordering synchronized. The C++ model must not infer a SoC address map;
all physical-memory classification goes through `SpikeAddressTransactions` and
the RTL-owned `RiscvPhysicalMemoryMap`.
The configured MMU type also crosses this boundary; set Spike's maximum virtual
address width before resetting its CSRs, matching standalone Spike initialization.
The external SSIP/STIP levels overlay, rather than overwrite, Spike's
software-writable `mip` state; keep the corresponding pinned Spike patch and
the DPI bridge in sync when changing interrupt delivery.

[`udb.rhm`](udb.rhm) owns the pinned Spike implementation's ACT/UDB projection.
Keep its ISA, CSR, counter, PMP, and trap claims aligned with the configured
Spike revision and [`profile.rhm`](profile.rhm); the SoC UDB catalog adds only
integration-owned platform facts. In particular, Spike's RV64 PMP CSR mask
uses its 56-bit physical-address limit, not the fabric's 44-bit CHI address
width. Validate changes with the SoC UDB test and
the Spike ACT configuration and ELF generation targets.

Run the focused host contract check with:

```sh
tools/run-racket-tests.sh cores/spike/tests/profile-test.rhm
tools/run-racket-tests.sh cores/spike/tests/elaboration-test.rhm
make -C sims spike-core-test
make -C sims spike-dpi-compile-check VERILATOR_ROOT=/path/to/verilator/share
make -C sims spike-dpi-abi-check VERILATOR_ROOT=/path/to/verilator/share
make -C sims smoke SOC=single-core-spike-soc
```
