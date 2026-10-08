<!-- Defines co-simulation ownership, reconstruction invariants, build integration, and focused validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing architectural co-simulation

Read [README.md](README.md) for public hooks, pass selection, checking semantics,
and current limitations. This guide owns the implementation. The parent
[simulation guide](../DEVELOPING.md) owns harness selection and simulator builds;
the [package graph](../../rhodium/DEVELOPING.md) owns repository dependencies.

## Architecture and ownership

Dependency arrows point toward consumed APIs:

```text
pass.rhm -> rv5stage/*.rhdl / rv2wide/capture.rhdl -> core observation contracts
runtime/session -> events/DPI + Sail checker
rv5stage/adapter -> events/DPI + collector
rv2wide/adapter -> events/DPI + collector
Sail checker -> event records + Sail reference
collector -> event records
```

Functional cores declare passive taps through `cores/cosim-source.rhm`.
Metadata retains a versioned contract, immutable host configuration, and
remappable hardware references, not observer closures. The pass selects an
adapter, elaborates it separately, checks its input/domain contract and absence
of outputs, then imports it into selected occurrences. Capture has no functional
feedback or bookkeeping registers. Keep eligibility policy in the adapter.

The event package has no named-core, Sail, or session dependency. Hart adapters
understand microarchitectural ownership, not reference-model semantics. The
checker consumes architectural records, never named-core raw callbacks. Only
`sail/reference.cc` includes generated Sail model headers. Runtime assembly owns
the active session, exact SoC configuration, FESVR mirroring, and shutdown.

### Reconstruction and comparison invariants

Resolve all callbacks at the settled sample barrier. Old returns and writes use
pre-edge owners before same-edge allocation or tag reuse. Retain instruction
generations through replay and delayed responses. Vector execution drain, not
sequencer release, seals the producer. Intermediate reduction results remain
private; only architectural writes become register effects. Shared-service flags
accumulate per instruction. Do not reconstruct physical CSR accumulation or
export bank snapshots solely to compare deterministic state.
Vector scalar-result ports carry their compute-maturity owner into the vector
effect stream, including FPR writes that dirty FS without an arithmetic flag
contribution. Preserve integer x0 suppression without suppressing FP register zero.

`SailChecker::check` captures reference pre-state, prepares environmental inputs,
steps Sail, then compares control flow, traps, registers, reported FP flag effects,
and memory. Sail owns deterministic CSR evolution, privilege transitions, and
translation. Compare CSR reads through GPR results. Generic explicit CSR effects
remain supported, but are not mandatory observations.
`VectorCheck` owns one instruction's reconstructed pre-state and optional bit coverage;
only the unknown-bit map survives instructions. It does not implement vector
arithmetic or an opcode qualification whitelist. Validate vector execution geometry
only after Sail determines legality; IllegalInstruction still checks its trap and
unchanged architectural state. `MemoryCheck` compares physical
store outcomes independently of beat decomposition and supplies physical-address
MMIO and registered external-memory replay. `SailReference::external_memory`
owns validated, nonoverlapping ranges inside private backing; it does not change
PMAs or update RAM with observed load bytes. The checker prepares separate read
queues, and the reference requires exact address/width and complete consumption.
Fetch and PTW callbacks never use external-memory replay. FESVR discovers mailbox
addresses after ELF loading; the session registers their eight-byte ranges before
boot publication. Keep admission-time environment samples unchanged when a
delayed load sees a later host write. Sail embedding uses existing register, trap,
PTW, and host-memory callbacks, not patches for virtual addresses, explicit
attempts, or FP contributions.
Atomic attempt addresses use Sail's effective-address helpers on pre-execution
state, including PMM, MPRV, and virtual/Bare extension policy, without a second walk.
An SC rejected after translation need not report a physical transaction; compare
its effective VA, width, fault, and absent writes. Completed SC attempts still
require translated physical provenance, and any reported PA must match Sail.
Vector issue captures retain both raw scheduling addresses and RTL-normalized
effective VAs. Packed element identities use the former; fragment/fault offsets
and architectural memory effects use the latter. Physical provenance stays separate.
WFI and WRS use Sail's two-call wait lifecycle for one architectural record.
WRS retirement permits early release without changing the reference reservation;
a reported timeout trap invokes Sail's independent TW/VTW checks. Never infer a
trap cause in the checker or execute the following instruction during release.

HPM counting is an explicit external-input boundary. Capture implemented raw
64-bit counter values before the edge, independently of returned GPR values.
Carry pre-edge overflow pulses to the following architectural boundary, retaining
them across idle cycles and deferred completion. The collector freezes these
inputs at admission. Supply counter values before authorized Sail CSR reads,
never overwrite the resulting GPR or resample in post-write callbacks. Reject
inputs for unimplemented slots. Sail owns selector state, sticky OF, pending
interrupt state, permissions, and RMW semantics; component RTL tests own event
counting, inhibition, and overflow generation. Clear overflow input on the second
call of a wait-release step so one event cannot be replayed twice.

RAM stores collapse repeated physical bytes to their last written value; device
writes retain order and multiplicity. Ordinary reads compare architectural
results, not access counts or addresses. Preserve successful store prefixes before
later faults. Sail computes traps, restart cursors, and FOF shortening independently.
Normalize RV5Stage and RV2Wide block zero to physical store fragments, including translated
addresses. Cache maintenance reports no byte mutation; its ordinary instruction
outcome checks permissions and faults without modeling cache residency.
Track permitted partial-segment/FOF divergence without repairing reference state;
unsupported uses of divergent bits fail. Fold DUT FP contributions into sticky
state and compare `fflags`, not each instruction's exact contribution.
Keep every current explicit rejection until its observation path is implemented.

Freeze interrupt/time inputs at architectural admission, not delayed completion.
Do not feed deterministic DUT CSR state into Sail or extract it merely for
comparison. Raw HPM counts and overflow events are the explicit exception above;
base cycle/time/instret behavior is unchanged. Autonomous inputs must not become another
instruction's contribution. Privilege includes virtualization; select M, HS, or
VS trap EPC/TVAL from Sail's actual target mode. Session
sample barriers surround evaluated rising edges. HTIF exit drains only the
admitted prefix under the original cycle limit. Never inject core stalls or reset
to help the checker. Warm reset requires resetting both reference and collection.

## Implementation map

| Owner | Files and responsibility |
|---|---|
| Compilation | `pass.rhm`: occurrence selection, tap export/remapping, adapter lookup, descriptor emission |
| Generic events | `events/record.h`: event schema; `collector.*`: delayed assembly; `dpi.*`: binding/error barrier; `hooks.rhdl`: typed producer; `transport.rhdl`: lane widening |
| RV5Stage | `rv5stage/capture.rhdl`, `vector.rhdl`: raw capture; `adapter.*`, `vector.*`: settled-cycle reconstruction |
| RV2Wide | `rv2wide/capture.rhdl`: stateless dual-slot capture; `adapter.*`: WB age, deferred services, and split-prefix ownership |
| Sail | `sail/reference.*`: embedding; `checker.*`: ordered comparison; `memory.*`, `vector.*`: comparison helpers; `diagnostic.h`: record-scoped errors |
| Simulator session | `runtime/session.*`: lifecycle; `write-config.rhm`, `configure.py`: exact environment and embedded configuration |
| Tests | `tests/events`, `rv5stage`, `rv2wide`, `sail`, `runtime`, `pass`: owning contracts; `tests/circt`: transport integration following repository fixture discovery |

RV2Wide allocates identities only at successful WB admission or split capture,
never at speculative EX multiply launch. It assigns two slots in age order and
retains a split owner through its final retirement/trap, including completed
store prefixes. Three native service FIFOs correspond to accepted memory,
WB-authorized multiply, and divide. Variable-return arbitration schedules load
and divide owners through three feed-forward stages; multiply writes directly
on its authorized WB+3 edge. The versioned `rv2wide.v4` source contract samples
virtual entry/return context, guest trap provenance, and counter-3 timing as well
as both the direct source and RF destination on that edge, independently of
same-cycle variable-return admission. Only the actual RF-write edge seals its GPR
producer. CSR traps allocate after draining older services; a successful older
slot retains its retirement when the younger faults. No callback order, PC-only
matching across generations, observer registers, or architectural CSR snapshots
provide ownership. Native tests shuffle callback order and cover epoch changes,
draining, reordered service results, and partial stores.

Shared architectural projection and `SailModelConfig.cmake` belong in
[`../sail/`](../sail/README.md), not here. ACT uses the same projection with its
own environment. Sail patches remain under `riscv/sail-riscv-patches/`.

## Change workflow

1. Change the owner of a semantic boundary. Keep raw callback interpretation in
   its hart adapter and architectural comparison in Sail helpers.
2. Keep DPI symbols and argument order synchronized between capture and C++.
   Never infer architectural ownership from callback invocation order.
3. Update imports, includes, CMake sources, Make prerequisites, fixture source
   lists, and CI routing together when moving files. `runtime.stamp` tracks all
   four native source directories; don't restore a root-only source wildcard.
4. Keep the public README current and internal rules here. The parent guide
   links here rather than duplicating a second source map.
5. Run the focused checks below. Generated model libraries, RTL, records,
   configurations, simulators, and software images stay in build directories.

`COSIM_WITH_SAIL=OFF` builds event/adapter tests without Sail or FESVR. With Sail,
the `cosim_runtime` CMake target produces only the reference/checker, hart
adapter libraries, and link flags consumed by the simulator. `sail-cosim-build`
configures with `BUILD_TESTING=OFF` and selects that target, not CMake's default
build of all test executables. CI builds these archives in the shared Sail job.
`PREBUILT_COSIM_DIR` selects downloaded runtime libraries and verifies the
`runtime/artifact.py` checksum manifest against native source content, commit,
platform, workspace, and absolute link paths. Missing or incompatible libraries
fail before linking; this mode never falls back to compilation. Verification is
an order-only prerequisite so it does not force otherwise unnecessary relinking.
`sail-cosim-test` builds and runs the full native test suite; the vector and
configuration test targets build only their respective executables. These test
targets explicitly re-enable `BUILD_TESTING` in the incremental CMake cache.
`runtime/session.cc` is compiled in the config build because it
consumes that config's generated `runtime-config.h`. Configuration generation
must preserve the exact selected profile, ROM/DTB, PMAs, and fingerprints.
Session reset geometry comes from that configuration, not a fixed XLEN/VLEN.
Keep integer, FP, and vector widths independent. SATP mode and vector `vill`
follow XLEN, while physical addresses and simulation counters retain 64-bit
transport lanes. RV32 high-half counter writes replace their clock edge just
like low-half writes.

## Validation

Use the minimum set covering the modified boundary:

```sh
make -C sims cosim-hooks-test
make -C sims sail-cosim-test
tools/run-racket-tests.sh sims/cosim/tests/pass/pass-test.rhm
make -C sims simulation-runtime-test sail-config-adapter-test
FIXTURES='cosim-hooks rv5stage-cosim32 rv5stage-cosim-vector' bash tools/testing/circt/run.sh --simulate-only
make check-boundaries ci-plan-test
```

Native tests cover callback order, ownership, replay/tag reuse, epochs, malformed
streams, mismatch detection, and reference independence. Representative RTL
fixtures check passivity and real callback transport, not a second ISA inventory.
`make -C sims sail-cosim-vector-test` compares its real vector stream with Sail.
For exporter/environment changes, use `sail-cosim-config-test SOC=<config>`.

Instruction coverage belongs to the unchanged ISA/ACT/platform/benchmark suites
with `COSIM=1`. CI enables it in every existing Mini/Simple RV5Stage build,
including the direct-SystemVerilog variant. Artifact consumers
verify the variant; native and ACT runners require nonempty checker completion.
Native build grouping preserves cosim selection only on execution rows, so ELF
sharing and suite inventories remain independent of instrumentation. ACT uses
the common harness variant for attestation and passes `--cosim` to its runner.
Consumers need GMP, not a second Sail installation. Profile selection does not
certify complete checking. Native regressions cover virtual privilege, delegation,
and guest traps; full H software coverage belongs to the existing CI lanes.
Do not add a new
qualification matrix, opcode-specific gates, weaken checks, exclude
failing workloads, or add bespoke software fixtures to declare support. Each
workload must exit successfully and produce a nonempty successful checked stream.
