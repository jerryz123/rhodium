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
The patched Spike `lrsc_accessible` hook supplies the full access size and
read/write direction before reservation matching; keep its physical-map
classification and atomic support bit aligned with the typed DPI response.
The pinned Spike CBO.ZERO hook must keep the full-block PMA check and coherent
unique-line update in `dpi/spike_core.cc`; never expose private cache storage
through `addr_to_mem` as a shortcut.
The configured MMU type also crosses this boundary; set Spike's maximum virtual
address width before resetting its CSRs, matching standalone Spike initialization.
Pass exact VLEN/ELEN through the DPI ABI and check them against the pinned ISA
parser before execution. Do not rely on a runtime default or silently reduce
the advertised ISA to fit the transport. Keep packed-string capacities aligned
with the native decoder; the generated-header ABI check covers argument types.
The external SSIP/STIP levels overlay, rather than overwrite, Spike's
software-writable `mip` state; keep the corresponding pinned Spike patch and
the DPI bridge in sync when changing interrupt delivery.

[`udb.rhm`](udb.rhm) owns the pinned Spike implementation's ACT/UDB projection.
Keep its ISA, CSR, counter, PMP, and trap claims aligned with the configured
Spike revision and [`profile.rhm`](profile.rhm); the SoC UDB catalog adds only
integration-owned platform facts. In particular, Spike's RV64 PMP CSR mask
uses its 56-bit physical-address limit, not the fabric's 44-bit CHI address
width. `tests/udb-test.rhm` covers both scalar and production RVA23 projections.
Keep extension/version mapping fail-closed and expand only architectural
implications; do not import RV5Stage policy. Vector parameters follow the pinned
`vector_unit.cc` and `v_ext_macros.h`, including reserved vset choices and
inactive NaN canonicalization. `sims/arch-test/configure.py` records fixed Sail
behavior differences separately in `reference-model-differences.json`; it must
not rewrite the DUT's parameters or filter tests to hide them. Validate actual
Sail configuration generation as well as UDB serialization.

Run the focused host contract check with:

```sh
tools/run-racket-tests.sh cores/spike/tests/profile-test.rhm
tools/run-racket-tests.sh cores/spike/tests/udb-test.rhm
tools/run-racket-tests.sh cores/spike/tests/elaboration-test.rhm
make -C sims spike-core-test
make -C sims spike-dpi-compile-check VERILATOR_ROOT=/path/to/verilator/share
make -C sims spike-dpi-abi-check VERILATOR_ROOT=/path/to/verilator/share
```

Run `make -C sims smoke SOC=mini-spike-rva23` for the complete BootROM/FESVR
path. The shared RVA23 smoke also checks VLEN, ELEN=64 execution, vector memory,
Zvbb, and binary64/binary16 vector FP results. Repeat with
`HTIF_ARGS=+load-through-chi` to cover both loader paths. This bounded smoke
does not replace ISA/ACT qualification.
