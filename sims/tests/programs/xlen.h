// Selects XLEN-sized accesses for RV32/RV64 platform assembly payloads.
// SPDX-License-Identifier: Apache-2.0
#ifndef RHODIUM_PLATFORM_XLEN_H
#define RHODIUM_PLATFORM_XLEN_H
#if __riscv_xlen == 32
#define LOAD_XLEN lw
#define STORE_XLEN sw
#define LOAD_U32 lw
#elif __riscv_xlen == 64
#define LOAD_XLEN ld
#define STORE_XLEN sd
#define LOAD_U32 lwu
#else
#error Unsupported RISC-V XLEN
#endif
#endif
