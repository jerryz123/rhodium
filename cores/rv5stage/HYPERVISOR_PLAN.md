<!-- Tracks the staged native H implementation and later Sha qualification for RV5Stage. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Native hypervisor implementation plan

Implement native H first; qualify Sha afterward. Firmware supplies policy and
device emulation, not a substitute for guest privilege or two-stage translation.
The RVA23 preset now selects H/Sha and its dependencies. ACT consumes that
same profile; profile enablement is not a claim of exhaustive qualification.

## Architecture

- RV64 host and guest; V is independent of nominal U/S/M privilege.
- One resident architectural guest context, switchable by software.
- SATP/VSATP Bare or Sv39 and HGATP Bare or Sv39x4.
- One shared host/guest TLB implementation instantiated for I/D lookup, and one
  shared host/nested walker. Preserve the combinational hit path; no guest service FSM.
- Initially zero ASID/VMID bits, global conservative invalidation, serialized
  nested walks, and software-managed A/D faults.
- Software-injected virtual interrupts first; GEILEN may be zero.
- WB remains the nonspeculative owner of CSR, trap, return, and fence events.
- Capture execution/translation context with accepted work. Cancellation
  suppresses results but must drain already-owned memory responses.

## Milestones

### Current integration cut

The shared translation bank and nested walker are wired into the production
MMU with CSR-owned instruction/data contexts (including MPRV/MPV). The opt-in
RV64 core specialization is selected by the shared RVA23 profile. Guest
fault class and provenance travel with fetch packets and MEM/WB results and
survive replay and pending exception retirement. Physical cache protocols
stay unchanged. HFENCE.VVMA/GVMA decode to WB-owned serializing operations;
conservative whole-bank invalidation is sufficient with zero ASID/VMID bits.
VSATP/HGATP paged modes are enabled with this complete path. The real
core/frontend/MMU fixture executes paged guests and checks precise traps,
delegation, MPRV/MPV, and HFENCE remapping followed by return. Host replay and
guest CSR/translation regressions cover the component contracts.

Software-injected VS software/timer/external interrupts now share the physical
interrupt retirement boundary. CSR regressions cover aliases and destination
priority; the paged guest fixture checks WFI wake, each virtual cause, load
completion, handler acknowledgement/masking, and SRET resumption.

Guest F/D now shares the host FPRs and `fcsr`, with independent HS/VS FS status
and SD summaries, dual enable gates, and dual dirty updates. Existing deferred
completion drain protects context changes. Paged guest arithmetic, FP memory,
disabled-state traps, fault/interrupt ordering, and SRET validate the integration.
Guest vectors share the VRF, vector CSRs and execution services with independent
HS/VS status and dual enable/dirty handling. Certificates include the full
two-stage context; failed prechecks fall back to precise element-owned guest
faults. Fault provenance survives macro retirement and `vstart` restart.
Accepted work drains before context changes, while certified work preserves
ordinary scalar/vector overlap.

Guest environment controls now include `henvcfg.FIOM/CBIE/CBCFE/CBZE`,
independent WARL storage and typed CMO denial precedence. Guest CBOs reuse WB
authorization and the ordinary memory path; full-drain fences cover FIOM.
PBMT routing is integrated through fetch, scalar/vector data and implicit PTE
reads, with separate M/H enables and composed attributes. Guest pointer masking
reuses the existing scalar/vector normalization with VS/VU and explicit HLV/HSV
policy capture, HLVX exclusion, and shared software-switched senvcfg. Optional
Sstc now provides host/guest comparators, STCE/TM access gates, VS aliasing,
and existing precise interrupt delivery. State-enable controls now cover the
current feature set's SE/ENVCFG hierarchy. Broader H/Sha qualification remains;
the remaining coverage work is tracked below.

### Svinval cut

Optional Svinval uses WB's fully ordered SFENCE/HFENCE paths for SINVAL.VMA
and HINVAL.VVMA/GVMA. Ordering-only instructions retain U/VU denial without
TVM/VTVM restrictions and retire without a separate MMU effect. Conservative
whole-bank invalidation and canceled-response ownership remain unchanged.
Qualification covers host/guest access checks, paged remapping, batches, and
late PTE responses. Svinval is a separate RVA23S64 requirement, not a constituent
of Sha; this cut does not change advertised profiles.

### State-enable cut

Optional Smstateen/Ssstateen CSR storage now resides in the commit-owned CSR
unit. SE is implemented in all four M/H register pairs and ENVCFG in pair zero;
unsupported-state and supervisor bits read zero. Keep pure CSR descriptors and
stateless hierarchical denial policy under `riscv/`. Machine denial must
override virtual-instruction classification. Validate WARL/reset, parent-masked
readback, all four access gates, RV32 high halves, and precise guest environment
CSR traps; preserve existing FS/VS and STCE/TM controls and profile claims.

### Explicit guest-memory cut

HLV/HLVX/HSV use ordinary scalar loads/stores plus typed translation
intent. Carry that intent from decode through EX/MEM/WB and the virtual MMU
request payload; consume it before physical cache admission. SPVP selects VS
or VU permissions independently of live V and MPRV. HU gates U-mode use;
execution with V=1 raises virtual-instruction before any memory effect.
HLVX checks X at both translation stages but retains load fault semantics,
ordinary implicit-PTE read permissions, and physical read-plus-execute checks.
Retain WB mutation authorization, replay, accepted-response ownership, and
precise fault provenance. Keep RV64; Ssnpm controls are captured with each
explicit guest access before address normalization. Validate real core execution,
warm/cold translations, signedness/widths, permission changes, and no effects
from denied stores. No H/Sha advertisement or virtual interrupt work here.

1. **Implemented: architectural definitions and standalone guest entry/exit.**
   Pure H instruction/CSR/cause catalogs; typed execution and guest-fault
   provenance; opt-in existing CSR unit specialization. Test M/HS/VS/VU
   transitions, aliases, delegation, access restrictions, fault metadata,
   host interrupt preemption, and commit qualification. Keep FP/vector and
   virtual interrupt injection out of this first bare-guest milestone.
2. **Implemented: standalone nested translation.** Reuse one PTE datapath with explicit VS
   continuation and G-stage ownership. Translate VS PTE addresses through G
   before loading them; then translate the resulting GPA. Test Bare-stage
   combinations, Sv39x4 root geometry, permissions, A/D faults, and cancellation.
3. **Implemented: guest TLB and precise scalar faults.**
   The shared TLB has composed guest 4 KiB entries beside host superpage entries,
   separate permissions, retained GPA provenance, coordinated TLB/walk invalidation,
   and instruction/load/store cause conversion. Guest context and precise
   PTE-read metadata now pass through fetch, scalar memory, and WB. Shared
   lookup/walk ports connect directly to CSR roots without serializing hits.
4. **Implemented: explicit guest accesses.** HFENCE and HLV/HLVX/HSV use existing
   pipeline ownership with distinct guest privilege and execute-read semantics.
   Accepted side-effect ownership remains stable around context changes.
5. **Complete H state.** Software-injected VS interrupts now use
   `hvip`, with aliased M/H/VS views, M/HS/VS destination priority, and existing
   precise WB delivery. Optional Sstc adds the guest timer comparator without
   changing controllers. GEILEN=0; no AIA. Validate masking, delegation, WFI,
   trap/return, and memory drain.
   Guest FP/vector status, shared execution, and vector authorization are integrated.
   FIOM/CMO and PBMT guest environment controls are integrated. Remaining:
   other environment controls and full qualification; guest pointer masking is integrated.
   Validate HS-qualified virtual-instruction versus illegal-instruction rules.
6. **Sha qualification.** Audit the eight constituents below. Sstc and Svinval
   are separate RVA23S64 requirements, not constituents of Sha.
7. **Enable through RVA23 and qualify with ACT.** Keep core profile, UDB,
   device tree, reference-model projection, and CI consistent. Directed
   CSR/MMU/core tests complement the available architectural tests.

## Architectural qualification ledger

The [RVA23 profile definition](https://raw.githubusercontent.com/riscv/riscv-profiles/main/src/rva23-profile.adoc)
defines the following Sha constituents. H semantics follow the
[ratified H 1.0 specification](https://docs.riscv.org/reference/isa/v20250508/priv/hypervisor.html).
This ledger records implementation and directed evidence, not a conformance
certificate. Profile enablement and ACT limitations are tracked below.

| Constituent | Requirement | Implementation and focused evidence |
|---|---|---|
| H | Guest privilege, translation, traps, CSRs, and instructions | `csr.rhdl`, `mmu/`, and the integrated core path; `rv5stage-hypervisor-csr`, `rv5stage-nested-walker`, `rv5stage-guest-translation`, and `rv5stage-hypervisor-core`. Full architectural qualification remains open. |
| Ssstateen | Supervisor and hypervisor state-enable views | Optional `smstateen` specialization in `csr.rhdl`; CSR hierarchy and paged guest access regressions. Must be selected for any future Sha profile. |
| Shcounterenw | Writable enables for nonzero HPM counters | Base enable bits are writable; HPM3–31 and their enables are hardwired zero. The CSR bench checks writable enables and zero-counter writes/readback. |
| Shvstvala | Required address/instruction trap values | VS delegation preserves captured trap values; CSR exception sweep and integrated illegal/compressed/straddled fetch cases. |
| Shtvala | Required faulting guest-physical address | MMU provenance distinguishes explicit accesses from implicit VS PTE reads; core tests check `htval=GPA>>2`, including second-half fetch faults. |
| Shvstvecd | Any valid four-byte-aligned direct vector | Full-width VSTVEC base with mode zero; CSR bench writes each Sv39 address bit and actually takes a delegated trap. |
| Shvsatpa | VSATP supports every SATP mode | Both implement Bare/Sv39 with zero ASID bits; CSR bench sweeps all mode encodings and preserves the old root on unsupported writes. |
| Shgatpa | HGATP Bare and corresponding x4 modes | Bare/Sv39x4 with zero VMID bits and aligned four-page root; mode sweep plus nested-walker and core execution. |

`RV5StageExtensions.hypervisor` owns both the RTL specialization and `misa.H`;
the fixed MISA ignores writes. The RVA23 preset enables H and Smstateen/Ssstateen
and publishes Sha and its six opcode-free guarantees through the shared ISA,
UDB, and device-tree paths. RV32 presets retain their existing capabilities.

The integrated qualification fixture enables C as well as F/D/V. Its fetch
cases distinguish the original instruction PC from the faulting second-half
address for VS page faults, explicit and implicit G-stage faults, and PMA
execute denial. They also check raw illegal 16/32-bit instruction values,
successful cross-page assembly, VS delegation, and younger-store suppression.
Twelve data cases route load/store page faults, access faults, and misalignment
to HS or VS and verify precise values, PCs, and absence of store effects.
CSR-only trap injection checks state capture; it does not replace these
execution-level checks.

### RVA23 enablement and ACT

The existing RVA23 product, not a separate opt-in target, is the qualification
vehicle. Its UDB-to-Sail projection includes H, guest translation geometry,
counter enables, VS trap vectors, VS vector status, and state-enable switches.
Keep all applicable privileged ACT tests enabled. Missing H/Sha test inventory,
reference-model differences, build failures, and execution failures must be
reported separately. Directed component coverage is not exhaustive H validation.
A separate hypervisor boot flow is not a prerequisite for this profile cut.
Broader guest software testing and unrelated RVA23 requirements remain separate
qualification work.

The pinned ACT inventory has Svinval tests but no dedicated H/Sha suite.
Canonical test generation and exact-profile UDB/Sail validation are separate
from runtime coverage. The adapter's UDB overlay corrects GEILEN=0 legality
and Shvstvala's explicit software-breakpoint exemption without changing the
hart's capabilities. Keep the directed guest-core and CSR regressions while
upstream architectural coverage is incomplete.

Enablement validation: both Simple RVA23 core choices pass UDB and Sail
configuration validation and both Svinval ACT programs through normal FESVR
execution. Canonical generation produced 251 suites; this is a generation
count, not a claim that all suites ran. Ordinary CSR, hypervisor CSR, and the
integrated paged-guest core fixtures pass. The shared SoC/UDB checks include
uncached RN-I routes to memory Homes as well as narrowed device-Home contracts.

The implementation exposes a standalone CSR specialization and
[shared host/guest translation](mmu/README.md#shared-host-and-guest-translation).
The current integration cut enables paged VSATP/HGATP with scalar and vector
memory composition; the shared RVA23 preset enables H on both core choices. See the
[public milestone contract](README.md#experimental-hypervisor-integration).
