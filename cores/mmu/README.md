<!-- Documents reusable RISC-V translation banks, page-table walking, and cancellation ownership. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Shared RISC-V translation

This package supplies the TLB and page-table walker shared by RV5Stage and
RV2Wide. It implements host Sv39 and nested VS Sv39 / G
Sv39x4 translation, optional Svnapot, and Svpbmt attributes. It is not a
complete core MMU: callers own miss arbitration, replay, architectural
invalidation, final physical permissions, and cache routing.
See [DEVELOPING.md](DEVELOPING.md) for implementation ownership and validation.

## Entry points

```rhdl
import:
  lib("cores/mmu/translation.rhdl") open
  lib("cores/mmu/tlb.rhdl") open
  lib("cores/mmu/walker.rhdl") open

inst itlb(RiscvTranslationTlb(8))
inst dtlb(RiscvTranslationTlb(8))
inst walker(RiscvTranslationWalker(~svnapot: #true))
```

The caller supplies `RiscvTlbLookup` values, selects misses for the walker's
Decoupled request, and routes its Irrevocable completion to the owning bank's
Valid `RiscvTlbFill`. Hits stay combinational; they never pass through the walker.
`protocol.rhdl` additionally provides host-only request/result and PTE-memory
types. `RiscvTlb` and `RiscvPageTableWalker` project host-only ports onto the
same implementations, without additional translation storage or sequencing.
The host-only TLB adapter uses a fixed zero root identity; its caller must
invalidate on address-space changes. Use the full lookup interface to tag roots.

## Translation contract

Illustration of the reusable components and caller-owned composition:

```text
lookup -> ITLB / DTLB -> hit or permission fault
              |
             miss -> caller arbitration -> shared walker -> owning TLB fill
                                              |
                                    one physical PTE read at a time
```

`RiscvTlbLookup` captures the virtual address, access class, virtualization,
execute-read intent, and `RiscvGuestTranslationContext`. Its `vs_*` fields
describe SATP/S-mode state for host accesses and VSATP/VS state for guest
accesses; host requests disable G translation. `RiscvTlbMapping` preserves the
original address/access, physical address, independent VS/G leaves, page size,
and exact guest-fault provenance. `riscv_translation_fault_cause` selects the
instruction/load/store exception from the original access, not an implicit PTE
read. Physical PMA/PMP checks and effective attribute routing remain outside.

Each `RiscvTranslationTlb` has a power-of-two entry count, default eight, with
one bank shared by host and guest mappings. Host mappings cover 4 KiB, 2 MiB,
1 GiB, and optionally 64 KiB NAPOT pages. Guest mappings retain composed 4 KiB
translations and the guest physical page. Tags distinguish host/guest state,
both modes/roots, PBMTE, and HS MXR. Demand hits recheck current privilege,
SUM/MXR, access class, and A/D permissions; VS denial precedes G denial.
HS MXR changes miss because entries do not retain implicit-PTE permission proofs.
Bare translation bypasses the bank and reports address overflow separately.

Successful fills update a matching entry or replace cyclically. Faults never
allocate. Whole-bank invalidation wins over a simultaneous fill, removes global
entries, and resets replacement state. Demand lookup observes pre-edge state;
callers order the invalidation against their pipeline. The separate nonfaulting
probe ignores A/D but requires a common access class permitted by both stages.
Probe PBMT applies the G override followed by a non-PMA stage-one override.

## Walker and memory ownership

One state machine handles ordinary Sv39 and nested translation. Guest walks
save the VS continuation while the same datapath translates each VS PTE address
through G-stage, then translate the final GPA. Both stages may be Bare.
Sv39x4 uses a 16 KiB root and 41-bit GPA. G permissions use U-mode semantics;
HS MXR applies at both stages, while VS MXR/SUM apply to stage one. Implicit
PTE reads retain ordinary read permissions. A/D handling is Svade: missing A,
or missing D for a write-like access, faults rather than updating the PTE.

`RiscvTranslationMemory` offers an address and PBMT for each physical PTE read.
Translated VS PTE reads carry G-stage PBMT; physical G PTE reads use PMA.
The service resolves rejection with `request_access_fault` **on acceptance**,
owing no response for that rejection. Otherwise it owes exactly one
`Valid(Bits(64))` response on that edge or later, with no response backpressure.
At most one read is outstanding. Page faults and physical access faults remain
distinct, including failures encountered while translating an implicit PTE.

`cancel` suppresses request admission and completion immediately. An already
accepted PTE read remains owned and must drain before another walk can begin,
even across repeated cancellation. A stalled completion retains its payload
until accepted or canceled. The caller must forward responses after invalidation;
reset must also reset the memory-service response epoch.
Ordinary speculative recovery may detach a consumer without canceling useful
translation work; that choice belongs to the integrating core.

## Limits and navigation

No Sv32/Sv48/Sv57, hardware A/D updates, ASID/VMID tagging, selective invalidation,
page-walk cache, or concurrent walks are implemented. The host TLB retains
global permissions but whole-bank invalidation removes them too. Svnapot is
opt-in and accepts only the supported level-zero 64 KiB encoding, using one
PTE's permissions without scanning aliases.

Architectural helpers remain in [`riscv/rtl`](../../riscv/rtl/README.md).
RV5Stage's [`mmu/`](../rv5stage/mmu/README.md) owns ITLB/DTLB arbitration,
fetch recovery, physical dispatch, split accesses, and vector certificates.
RV2Wide's [`mmu.rhdl`](../rv2wide/mmu.rhdl) composes the same shared banks and
walker with its own issue, retirement, replay, and memory-response policy.
