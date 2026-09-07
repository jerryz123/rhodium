<!-- Documents executable SoC harnesses, host models, and simulator bindings. -->

# Simulation harnesses

This directory turns the synthesizable [`socs/` compositions](../socs/README.md)
into executable simulations. It owns the parameterless generated top, FESVR
transport, Verilator binding, simulation-only memory model, clock/reset driver,
and executable workflows. The SoCs continue to own processor, device, CHI,
NoC, and synthesizable-memory structure.

Contributors changing a harness, binding, or build rule should read
[`DEVELOPING.md`](DEVELOPING.md).

## Choose a harness

| `SOC` | Selected system | Memory supplied by the harness | Default core specialization |
| --- | --- | --- | --- |
| `simple` | `SimpleSoC` | `CHIDPIMemory` behind the SoC's external SN-F boundary | RV64IMAFDC plus B and Zicond; C composes Zca and Zcd |
| `mini` | `MiniSoC` | None; the SoC contains its own 64 KiB `CHIRam` | Integer-only, compressed instructions disabled |
| `tiled` | Default 4x4 `TiledSoC` | None; each LLC tile contains its backing `CHIRam` bank | Integer-only; C specializes to Zca |

`SOC` defaults to `simple`. Read the [SoC comparison](../socs/README.md#choose-a-system)
for the hardware differences, then use this guide to build or run the matching
harness.

## Ownership and execution boundary

The stack follows a Chipyard-like boundary:

```mermaid
flowchart LR
  Driver["TestDriver.v<br/>clock, reset, UART pins, and exit"]

  subgraph Harness["Generated SoCHarness top - sims ownership"]
    FESVR["FesvrRequester<br/>RAM + MMIO host RN-F"]
    SoC["Selected SoC instance<br/>BootROM + hardware owned by socs/"]
    DPIMemory["CHIDPIMemory<br/>SimpleSoC only"]

    FESVR <--> SoC
    FESVR -.->|"release after load"| SoC
    SoC <--> DPIMemory
  end

  Driver -->|"clock, reset, idle UART RX"| Harness
  Harness -->|"exit, UART TX + interrupt"| Driver
```

`TestDriver.v` generates clock and reset, holds the UART RX line idle, and
observes the harness exit status. Each SoC has a separate parameterless harness
that instantiates the FESVR requester and connects it to that SoC's common
`SoCHostInterface`. Each harness passes through the synthesizable UART RX, TX,
and interrupt boundary; it does not instantiate the UART PTY DPI model. The
SimpleSoC harness additionally instantiates `CHIDPIMemory`, because only that
SoC exposes an external normal-memory boundary. No SoC contains DPI calls or
simulator dependencies.

The directory owns:

- [`fesvr/`](fesvr/): the simulator-independent direct-memory FESVR transport
  and its Rhodium CHI requester.
- [`verilator/`](verilator/): the Verilator VPI/DPI binding.
- [`tests/`](tests/): independent structural checks for FESVR and each system
  harness, plus the executable smoke payload.

## Build a simulator

Install the pinned FESVR dependency once, then build any system-specific
simulator:

```sh
make -C sims setup
make -C sims simulator SOC=simple
make -C sims simulator SOC=mini
make -C sims simulator SOC=tiled
```

`SOC` accepts `simple`, `mini`, or `tiled` and defaults to `simple`. The
SimpleSoC harness attaches `CHIDPIMemory` to the SoC's exposed ready-valid SN-F
channels as a simulation-only external memory model. MiniSoC instead contains
its own synthesizable `CHIRam`. Each harness has an independent artifact at
`/tmp/rhodium-sims/<soc>/obj/VTestDriver`, so
switching configurations cannot reuse generated RTL for the other SoC. The
shared Verilator `TestDriver` leaves TX and the UART interrupt observable but
unused and drives RX high as an idle 8-N-1 serial line. Set
`BUILD_ROOT` when a different artifact root is required. Building a simulator
does not require or embed a target program.

The Makefile maps `SOC` to one harness module. A shared emitter dynamically
loads only that module's exported `design`, so unrelated SoCs are neither
imported nor elaborated. Every harness emits the same parameterless
`SoCHarness` Verilog top contract, allowing `TestDriver.v` to remain shared;
there is no Rhodium variant enum or conditional harness circuit.

## Run a target

Run any FESVR-compatible target binary through an already-built simulator:

```sh
make -C sims run SOC=simple BINARY=/absolute/path/to/program.elf
make -C sims run SOC=mini BINARY=/absolute/path/to/program.elf
make -C sims run SOC=tiled BINARY=/absolute/path/to/program.elf
```

`HTIF_ARGS` places optional FESVR host arguments before the target binary, and
`TARGET_ARGS` places arguments after it. The simulator passes this process
argument vector through VPI to `DirectMemoryHtif`. FESVR owns ELF parsing,
segment loading, entry-point discovery, `tohost`/`fromhost` polling, and exit
status; the Makefile and RTL do not implement a separate binary loader.

Each harness requires the ELF entry point reported by FESVR to match the SoC's
configured BootROM payload address. It then converts that entry notification
into a one-shot release; every hart starts at the reset address, hart zero
jumps to the loaded payload, and secondary harts park in the ROM.

`DirectMemoryHtif` presents FESVR's abstract memory chunks as one-outstanding,
one-to-eight-byte transactions with 64-bit addresses and data. It never widens
device reads or synthesizes read-modify-write for narrow writes. Target XLEN
may be 32 or 64. `FesvrRequester` uses the SoC's physical map and Home service
descriptions to select coherent `ReadClean`/`WriteUniquePtl` for cacheable RAM
or `ReadNoSnp`/`WriteNoSnpPtl` for non-cacheable regions. RAM chunks may be
fragmented into aligned transfers; MMIO must be an exact, aligned, supported
1/2/4/8-byte access. Unmapped, forbidden, or unsupported accesses fail before
device traffic is issued. Target and protocol errors stop FESVR with failure.

The requester retains no cache lines and reports Invalid for every snoop.
ELF loading and `tohost`/`fromhost` polling still observe dirty RV5Stage cache
lines without reserving a special mailbox address range. The same endpoint
can access platform devices, including the boot-address register and UART.
This does not change the static BootROM jump or ELF-entry equality check above.

## Architectural certification tests

The ACT4 integration selects suites from the configured core's UDB description.
It uses the [generated UDB catalog](../socs/README.md#risc-v-udb-configuration-catalog)
to select the DUT architecture and Sail to compute expected results. Install
Python 3.10+, Ruby 3.2+ with Bundler, and GCC 15+ with Binutils 2.44+ first:

```sh
make -C sims arch-test-setup
make -C sims arch-test ACT_CONFIGURATION=simple-soc
```

Set `PYTHON=/path/to/python3` for setup if the default Python is too old. Setup
initializes the pinned `riscv/riscv-arch-test` submodule, installs Python and
Ruby dependencies under `.tools/`, and downloads checksum-verified Sail 0.13.1
for Apple Silicon macOS or x86-64/AArch64 Linux. Normal simulator dependencies
are still required; see [Build a simulator](#build-a-simulator).
On Apple Silicon it also installs native Z3 5.0.0 in the local UDB cache,
working around the pinned UDB installer's Linux-only library download.

`arch-test-config` only prepares and validates the Sail/platform files;
`arch-test-elfs` also validates UDB through ACT and generates self-checking
ELFs for every test matching the generated UDB configuration. It replaces the
configuration's generated ELF files before building, so an older core profile
cannot leave stale tests in the suite; reference intermediates remain cached.
`arch-test` builds the selected simulator and executes those ELFs through ACT's
upstream runner. After a successful build, `arch-test-run` reruns the existing
ELFs without regenerating the bundle. An empty ELF directory is an error.
Outputs and per-test logs live under
`/tmp/rhodium-arch-test`; set `ACT_BUILD_ROOT` to change that location.
`ACT_SAIL`, `ACT_VENV`, and `ACT_BUNDLE_PATH` select installed tool locations.
`RISCV_CC` and `ACT_OBJDUMP` select compiler tools.

Generation always considers all extensions. ACT selects applicable tests using
UDB's implemented (including implied) extensions and each test's parameter
constraints; there is no separate extension list or selection wrapper in Make.

Selection is not a claim that every candidate has passed. The initial reference
adapter was validated with RV64I and M-mode startup; it is not yet a complete
UDB-to-Sail projection. ACT's `include_priv_tests` remains false as a separate
harness limitation. The pinned ACT filter can still select virtual-memory
suites whose metadata does not directly require its privilege extensions.
The full DUT device and PMA map is not modeled for this stage. Sail retains
the reference-only interrupt devices required by ACT; DUT interrupt hooks
fail if invoked. Build, reference-model, and DUT failures in newly selected suites
are surfaced normally, not silently excluded; they need diagnosis before claiming
coverage.

The runner translates confirmed HTIF completion into ACT's `RVCP-SUMMARY`
protocol. Console printing macros are empty, so failures currently report
completion status and simulator logs without ACT's detailed mismatch console.
`ACT_MAX_CYCLES` defaults to ten million cycles; `ACT_TIMEOUT` defaults to 300
seconds per ELF. `ACT_JOBS` defaults to one simulator at a time. The shared
driver also accepts `HTIF_ARGS='+permissive +max-cycles=N +permissive-off'`
for ordinary `run`; the permissive brackets keep FESVR from treating a
simulator option as the ELF name.

These tests complement the separate `riscv/riscv-isa-tests` (`riscv-tests`)
source dependency. A passing integer run does not establish full architectural
certification. Upstream documents the framework in the
[ACT4 guide](https://github.com/riscv/riscv-arch-test/blob/act4/README.md).

## Focused validation

Run the genuine execution smoke for any system:

```sh
make -C sims smoke SOC=simple
make -C sims smoke SOC=mini
make -C sims smoke SOC=tiled
```

The smoke starts with `tohost` cleared, executes RV64I instructions on
RV5Stage, stores the passing value into a dirty L1D line, and succeeds only
after the coherent FESVR requester observes that write. It uses the same `run`
path as an external target binary.

Contributor binding, structural, and lowering checks are documented in
[`DEVELOPING.md`](DEVELOPING.md#focused-validation).

These simulators always use CIRCT-inferred memories. To validate a
design-and-technology SRAM mapping while reusing this harness, driver, FESVR
transport, and smoke payload, run `make -C vlsi/sim smoke`; see the
[`vlsi/sim` guide](../vlsi/sim/README.md). Keeping mapped simulation there
prevents technology policy from entering this package or the SoCs.
