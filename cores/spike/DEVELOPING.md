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

Run the focused host contract check with:

```sh
tools/run-racket-tests.sh cores/spike/tests/profile-test.rhm
tools/run-racket-tests.sh cores/spike/tests/elaboration-test.rhm
make -C sims spike-core-test
make -C sims spike-dpi-compile-check VERILATOR_ROOT=/path/to/verilator/share
make -C sims spike-dpi-abi-check VERILATOR_ROOT=/path/to/verilator/share
```
