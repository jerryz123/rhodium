// Preserves the rv2wide-rv32 cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
struct Finished {};
auto &retired(unsigned i) { return i ? retired_1_out : retired_0_out; }
inline auto &completed = completed_out;
inline auto &redirect = redirect_out;
using retirement_t = std::remove_cvref_t<decltype(retired_0_out.pbits)>;
using CHIReqFlit =
    std::remove_cvref_t<decltype(instruction_chi_out.preq.pbits)>;
// Exercises RV32 scalar execution, Bare PMAs, split accesses, and precise traps through the production fetching top.

std::uint8_t backing[131072];
std::uint32_t expected[131072];
bool expected_write[131072];
bool seen[131072];
int pc = UINT64_C(1024), cycles = 0, commits = 0, dual = 0, faults = 0,
    completed_count = 0;
int ireads = 0, dreads = 0, ureads = 0, uwrites = 0;
std::uint32_t trap_pc, trap_value;
bool iactive = 0, dactive = 0, wactive = 0;
CHIReqFlit irequest, drequest, wrequest, urequest;
int idue, ipacket, ddue, dpacket, ustate = 0, udue = 0;

std::uint32_t ri(int op, int rd, int rs, int imm) {
  return (((uint128(((imm)&low_mask(12))) & mask128(12)) << 20) |
          ((uint128(((rs)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(((op)&low_mask(3))) & mask128(3)) << 12) |
          ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(19)) & mask128(7)) << 0));
}
std::uint32_t rr(int func, int op, int rd, int rs1, int rs2) {
  return (((uint128(((func)&low_mask(7))) & mask128(7)) << 25) |
          ((uint128(((rs2)&low_mask(5))) & mask128(5)) << 20) |
          ((uint128(((rs1)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(((op)&low_mask(3))) & mask128(3)) << 12) |
          ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(51)) & mask128(7)) << 0));
}
std::uint32_t csr(int op, int address, int rd, int rs) {
  return (((uint128(((address)&low_mask(12))) & mask128(12)) << 20) |
          ((uint128(((rs)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(((op)&low_mask(3))) & mask128(3)) << 12) |
          ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(115)) & mask128(7)) << 0));
}
std::uint32_t ld(int op, int rd, int base, int offset) {
  return (((uint128(((offset)&low_mask(12))) & mask128(12)) << 20) |
          ((uint128(((base)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(((op)&low_mask(3))) & mask128(3)) << 12) |
          ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(3)) & mask128(7)) << 0));
}
std::uint32_t st(int op, int rs, int base, int offset) {
  return (((uint128(((offset >> 5) & low_mask(7))) & mask128(7)) << 25) |
          ((uint128(((rs)&low_mask(5))) & mask128(5)) << 20) |
          ((uint128(((base)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(((op)&low_mask(3))) & mask128(3)) << 12) |
          ((uint128(((offset)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(35)) & mask128(7)) << 0));
}
void put(int address, std::uint32_t word) {
  for (int b = 0; b < 4; b++)
    backing[address + b] = sv_slice(word, b * 8, 8);
}
void emit(std::uint32_t word, std::uint8_t writes = 0,
          std::uint32_t value = 0) {
  put(pc, word);
  expected_write[pc] = writes;
  expected[pc] = value;
  pc += 4;
}
void check_result(retirement_t result, std::uint8_t completion) {
  int address = int(result.pfetched.ppc);
  if (address == UINT64_C(2048) || address == UINT64_C(2052) ||
      address == UINT64_C(2056)) {
    CHECK(result.pwrite &&
          result.pdata == (address == UINT64_C(2048)   ? trap_pc
                           : address == UINT64_C(2052) ? trap_value
                                                       : trap_pc + 4));
    return;
  }
  if (result.pwrite && (completion || !result.pdeferred)) {
    CHECK(expected_write[address] && result.pdata == expected[address]);
    CHECK(!seen[address]);
    seen[address] = 1;
  }
}

int split_fault_pc;

void drive() {
  {
    uncached_chi_in = {};
    uncached_chi_in.preq.pready = ustate == 0 && cycles % 7 != 0;
    uncached_chi_in.prsp.prequester.pready = 1;
    uncached_chi_in.pdat.prequest.pready = ustate == 3 && cycles % 3 != 0;
    if (ustate == 1 && cycles >= udue) {
      uncached_chi_in.pdat.presponse.pvalid = 1;
      uncached_chi_in.pdat.presponse.pbits.popcode = UINT64_C(4);
      uncached_chi_in.pdat.presponse.pbits.psrc_uid = urequest.ptgt_uid;
      uncached_chi_in.pdat.presponse.pbits
          .phome_unid_uor_upbha_uor_umismatched_umecid = urequest.ptgt_uid;
      uncached_chi_in.pdat.presponse.pbits.ptgt_uid = UINT64_C(4);
      uncached_chi_in.pdat.presponse.pbits.ptxn_uid = urequest.ptxn_uid;
      uncached_chi_in.pdat.presponse.pbits.pbyte_uenable = UINT16_MAX;
      for (int b = 0; b < 16; b++)
        sv_slice(uncached_chi_in.pdat.presponse.pbits.pdata, b * 8, 8) =
            backing[(int(urequest.paddress) & ~15) + b];
    }
    if ((ustate == 2 || ustate == 4) && cycles >= udue) {
      uncached_chi_in.prsp.presponse.pvalid = 1;
      uncached_chi_in.prsp.presponse.pbits.popcode =
          ustate == 2 ? UINT64_C(6) : UINT64_C(4);
      uncached_chi_in.prsp.presponse.pbits.psrc_uid = urequest.ptgt_uid;
      uncached_chi_in.prsp.presponse.pbits.ptgt_uid = UINT64_C(4);
      uncached_chi_in.prsp.presponse.pbits.ptxn_uid = urequest.ptxn_uid;
      uncached_chi_in.prsp.presponse.pbits.pdbid_uor_ugroup_uid = UINT64_C(11);
    }
    instruction_chi_in = {};
    instruction_chi_in.preq.pready = !iactive && cycles % 5 != 0;
    instruction_chi_in.prsp.prequester.pready = cycles % 4 != 0;
    if (iactive && cycles >= idue) {
      instruction_chi_in.pdat.presponse.pvalid = 1;
      instruction_chi_in.pdat.presponse.pbits.popcode = UINT64_C(4);
      instruction_chi_in.pdat.presponse.pbits.psrc_uid = UINT64_C(1);
      instruction_chi_in.pdat.presponse.pbits.ptgt_uid = UINT64_C(2);
      instruction_chi_in.pdat.presponse.pbits
          .phome_unid_uor_upbha_uor_umismatched_umecid = UINT64_C(1);
      instruction_chi_in.pdat.presponse.pbits.ptxn_uid = irequest.ptxn_uid;
      instruction_chi_in.pdat.presponse.pbits.pdbid_uor_umecid = UINT64_C(5);
      instruction_chi_in.pdat.presponse.pbits.pdata_uid =
          ((ipacket ^ 1) & low_mask(2));
      instruction_chi_in.pdat.presponse.pbits.pbyte_uenable = UINT16_MAX;
      instruction_chi_in.pdat.presponse.pbits.presp_uerr = 0;
      for (int b = 0; b < 16; b++)
        sv_slice(instruction_chi_in.pdat.presponse.pbits.pdata, b * 8, 8) =
            backing[int(irequest.paddress) + 16 * (ipacket ^ 1) + b];
    }
    data_chi_in = {};
    data_chi_in.prequests.pready = !dactive && !wactive && cycles % 5 != 0;
    data_chi_in.prequester_uresponses.pready = cycles % 4 != 0;
    data_chi_in.prequest_udata.pready = cycles % 3 != 0;
    if (wactive) {
      data_chi_in.presponses.pvalid = 1;
      data_chi_in.presponses.pbits.popcode = UINT64_C(5);
      data_chi_in.presponses.pbits.psrc_uid = UINT64_C(1);
      data_chi_in.presponses.pbits.ptgt_uid = UINT64_C(3);
      data_chi_in.presponses.pbits.ptxn_uid = wrequest.ptxn_uid;
      data_chi_in.presponses.pbits.pdbid_uor_ugroup_uid = UINT64_C(9);
    }
    if (dactive && cycles >= ddue) {
      data_chi_in.presponse_udata.pvalid = 1;
      data_chi_in.presponse_udata.pbits.popcode = UINT64_C(4);
      data_chi_in.presponse_udata.pbits.psrc_uid = UINT64_C(1);
      data_chi_in.presponse_udata.pbits.ptgt_uid = UINT64_C(3);
      data_chi_in.presponse_udata.pbits
          .phome_unid_uor_upbha_uor_umismatched_umecid = UINT64_C(1);
      data_chi_in.presponse_udata.pbits.ptxn_uid = drequest.ptxn_uid;
      data_chi_in.presponse_udata.pbits.pdbid_uor_umecid = UINT64_C(6);
      data_chi_in.presponse_udata.pbits.presp =
          drequest.popcode == UINT64_C(7) ? UINT64_C(2) : UINT64_C(1);
      data_chi_in.presponse_udata.pbits.pdata_uid = ((dpacket)&low_mask(2));
      data_chi_in.presponse_udata.pbits.pbyte_uenable = UINT16_MAX;
      for (int b = 0; b < 16; b++)
        sv_slice(data_chi_in.presponse_udata.pbits.pdata, b * 8, 8) =
            backing[int(drequest.paddress) + 16 * dpacket + b];
    }
  }
}

void observe() {
  if (!reset) {
    defer(cycles, cycles + 1);
    CHECK(cycles < 30000);
    if (retired_count == 2)
      dual++;
    for (int lane = 0; lane < 2; lane++)
      if (retired(lane).pvalid) {
        commits++;
        check_result(retired(lane).pbits, 0);
      }
    if (completed.pvalid) {
      check_result(completed.pbits, 1);
      completed_count++;
    }
    if (redirect.pvalid && redirect.pbits.presolution.pdisposition == 1) {
      CHECK(redirect.pbits.presolution.pcause == 2 ||
            redirect.pbits.presolution.pcause == 5);
      faults++;
      trap_pc = redirect.pbits.ppc;
      if (redirect.pbits.presolution.pcause == 2)
        for (int b = 0; b < 4; b++)
          sv_slice(trap_value, b * 8, 8) = backing[int(trap_pc) + b];
      else
        trap_value = redirect.pbits.ppc == ((split_fault_pc)&low_mask(32))
                         ? UINT64_C(4096)
                         : UINT64_C(4294965248);
      CHECK(redirect.pbits.presolution.pvalue == trap_value);
    }
    if (instruction_chi_out.preq.pvalid && instruction_chi_in.preq.pready) {
      CHECK(instruction_chi_out.preq.pbits.paddress < 4096 &&
            instruction_chi_out.preq.pbits.popcode == UINT64_C(3));
      defer(irequest, instruction_chi_out.preq.pbits);
      defer(iactive, 1);
      defer(ipacket, 0);
      defer(idue, cycles + 12);
      ireads++;
    }
    if (instruction_chi_in.pdat.presponse.pvalid &&
        instruction_chi_out.pdat.presponse.pready) {
      if (ipacket == 3)
        defer(iactive, 0);
      else {
        defer(ipacket, ipacket + 1);
        defer(idue, cycles + 2);
      }
    }
    if (data_chi_out.prequests.pvalid && data_chi_in.prequests.pready) {
      CHECK(data_chi_out.prequests.pbits.paddress < 4096);
      switch (data_chi_out.prequests.pbits.popcode) {
      case UINT64_C(2):
      case UINT64_C(7): {
        {
          defer(drequest, data_chi_out.prequests.pbits);
          defer(dactive, 1);
          defer(dpacket, 0);
          defer(ddue, cycles + 30);
          dreads++;
        }
      } break;
      case UINT64_C(27): {
        {
          defer(wrequest, data_chi_out.prequests.pbits);
          defer(wactive, 1);
        }
      } break;
      default: {
        fail(1, "bad data opcode");
      } break;
      }
    }
    if (data_chi_in.presponse_udata.pvalid &&
        data_chi_out.presponse_udata.pready) {
      if (dpacket == 3)
        defer(dactive, 0);
      else {
        defer(dpacket, dpacket + 1);
        defer(ddue, cycles + 2);
      }
    }
    if (data_chi_in.presponses.pvalid && data_chi_out.presponses.pready)
      defer(wactive, 0);
    if (data_chi_out.prequest_udata.pvalid && data_chi_in.prequest_udata.pready)
      for (int b = 0; b < 16; b++)
        if (sv_slice(data_chi_out.prequest_udata.pbits.pbyte_uenable, b, 1))
          backing[int(wrequest.paddress) +
                  16 * int(data_chi_out.prequest_udata.pbits.pdata_uid) + b] =
              sv_slice(data_chi_out.prequest_udata.pbits.pdata, b * 8, 8);
    if (uncached_chi_out.preq.pvalid && uncached_chi_in.preq.pready) {
      CHECK(!dactive && !wactive);
      CHECK(uncached_chi_out.preq.pbits.paddress >= UINT64_C(8192) &&
            uncached_chi_out.preq.pbits.paddress < UINT64_C(8448) &&
            uncached_chi_out.preq.pbits.psize_uor_unum_ureq <= 2);
      defer(urequest, uncached_chi_out.preq.pbits);
      if (uncached_chi_out.preq.pbits.popcode == UINT64_C(4)) {
        defer(ustate, 1);
        ureads++;
      } else if (uncached_chi_out.preq.pbits.popcode == UINT64_C(28)) {
        defer(ustate, 2);
        uwrites++;
      } else
        fail(1, "bad IO opcode");
      defer(udue, cycles + 20);
    }
    if (uncached_chi_in.pdat.presponse.pvalid &&
        uncached_chi_out.pdat.presponse.pready)
      defer(ustate, 0);
    if (uncached_chi_in.prsp.presponse.pvalid &&
        uncached_chi_out.prsp.presponse.pready)
      defer(ustate, ustate == 2 ? 3 : 0);
    if (uncached_chi_out.pdat.prequest.pvalid &&
        uncached_chi_in.pdat.prequest.pready) {
      std::uint16_t mask;
      mask = (((1 << (1 << urequest.psize_uor_unum_ureq)) - 1) & low_mask(16))
             << sv_slice(urequest.paddress, 0, (3) - (0) + 1);
      CHECK(uncached_chi_out.pdat.prequest.pbits.pbyte_uenable == mask);
      for (int b = 0; b < 16; b++)
        if (sv_slice(mask, b, 1))
          backing[(int(urequest.paddress) & ~15) + b] =
              sv_slice(uncached_chi_out.pdat.prequest.pbits.pdata, b * 8, 8);
      defer(ustate, 4);
      defer(udue, cycles + 20);
    }
  }
}

void falling_update() {}

void stimulus() {
  reset = 1;
  {
    start_in = {};
    for (int i = 0; i < 131072; i++) {
      backing[i] = 0;
      expected[i] = 0;
      expected_write[i] = 0;
      seen[i] = 0;
    }
    emit(UINT64_C(7735), 1, UINT64_C(4096)); // x28 = handler base - 0x800
    emit(ri(0, 28, 28, -2048), 1, UINT64_C(2048));
    emit(csr(1, UINT64_C(773), 0, 28));
    emit(ri(0, 2, 0, -1), 1, UINT32_MAX);
    emit(ri(0, 3, 2, 1), 1, 0); // XLEN wrap
    emit(ri(1, 4, 2, 31), 1, UINT64_C(2147483648));
    emit(ri(5, 5, 2, 31), 1, 1);
    emit(ri(5, 6, 4, UINT64_C(1055)), 1, UINT32_MAX);
    emit(rr(0, 1, 7, 2, 3), 1, UINT32_MAX);
    emit(ri(0, 8, 0, 3), 1, 3);
    emit(rr(1, 0, 9, 2, 8), 1, UINT64_C(4294967293));
    emit(rr(1, 1, 10, 2, 8), 1, UINT32_MAX);
    emit(rr(1, 2, 11, 2, 8), 1, UINT32_MAX);
    emit(rr(1, 3, 12, 2, 8), 1, 2);
    emit(rr(1, 4, 13, 4, 2), 1, UINT64_C(2147483648)); // signed divide overflow
    emit(rr(1, 5, 14, 2, 8), 1, UINT64_C(1431655765));
    emit(rr(1, 6, 15, 4, 2), 1, 0);
    emit(rr(1, 7, 16, 2, 8), 1, 0);
    emit(rr(1, 4, 17, 8, 0), 1, UINT32_MAX);
    emit(rr(1, 6, 18, 8, 0), 1, 3);
    emit(ri(5, 19, 8, UINT64_C(1688)), 1, UINT64_C(50331648));   // RV32 REV8
    emit(ri(5, 20, 8, UINT64_C(1537)), 1, UINT64_C(2147483649)); // RV32 RORI
    emit(rr(UINT64_C(4), 4, 21, 2, 0), 1, UINT64_C(65535));      // RV32 ZEXT.H
    emit(ri(0, 22, 0, 32), 1, 32);
    emit(rr(0, 1, 23, 8, 22), 1,
         3); // register shift amount masked to five bits
    emit(ri(0, 24, 0, UINT64_C(3072)), 1, UINT64_C(4294966272));
    emit(UINT64_C(7223), 1, UINT64_C(4096));
    emit(ri(0, 24, 24, -1024), 1, UINT64_C(3072));
    for (int lane = 0; lane < 4; lane++) {
      emit(st(0, 8, 24, lane));
      emit(ld(4, 25, 24, lane), 1, 3);
    }
    emit(st(2, 4, 24, 1)); // cross-word misaligned store and load
    emit(ld(2, 25, 24, 1), 1, UINT64_C(2147483648));
    emit(st(2, 8, 24, 60));
    emit(ld(2, 25, 24, 60), 1, 3);
    emit((((uint128(UINT64_C(2)) & mask128(5)) << 27) |
          ((uint128(UINT64_C(3)) & mask128(2)) << 25) |
          ((uint128(UINT64_C(0)) & mask128(5)) << 20) |
          ((uint128(UINT64_C(24)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(2)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(25)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(47)) & mask128(7)) << 0)),
         1, UINT64_C(3)); // LR.W
    emit((((uint128(UINT64_C(3)) & mask128(5)) << 27) |
          ((uint128(UINT64_C(3)) & mask128(2)) << 25) |
          ((uint128(UINT64_C(8)) & mask128(5)) << 20) |
          ((uint128(UINT64_C(24)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(2)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(26)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(47)) & mask128(7)) << 0)),
         1, 0); // SC.W
    emit((((uint128(UINT64_C(0)) & mask128(5)) << 27) |
          ((uint128(UINT64_C(3)) & mask128(2)) << 25) |
          ((uint128(UINT64_C(8)) & mask128(5)) << 20) |
          ((uint128(UINT64_C(24)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(2)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(25)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(47)) & mask128(7)) << 0)),
         1, 3); // AMOADD.W
    emit(ld(2, 25, 24, 0), 1, 6);
    emit(UINT64_C(11575), 1, UINT64_C(8192));
    emit(st(2, 4, 26, 4));
    emit(ld(2, 25, 26, 4), 1, UINT64_C(2147483648));
    emit(st(0, 8, 26, 7));
    emit(ld(4, 25, 26, 7), 1, 3);
    emit(csr(1, UINT64_C(384), 0,
             2)); // attempted Sv32 satp is WARL zero in Bare
    emit(csr(2, UINT64_C(384), 25, 0), 1, 0);
    emit(csr(1, UINT64_C(832), 0, 4));
    emit(csr(2, UINT64_C(832), 25, 0), 1, UINT64_C(2147483648));
    emit(csr(2, UINT64_C(769), 25, 0), 1,
         UINT64_C(1075056903)); // RV32 MISA: IMACB + S/U
    emit(ri(0, 27, 0, 5), 1, 5);
    emit(csr(1, UINT64_C(800), 0,
             27)); // Freeze cycle/instret to check RV32 high halves.
    emit(csr(1, UINT64_C(2944), 0, 8));
    emit(csr(2, UINT64_C(2944), 27, 0), 1, 3);
    emit(csr(1, UINT64_C(2946), 0, 8));
    emit(csr(2, UINT64_C(2946), 27, 0), 1, 3);
    while ((pc & 7) != 0)
      emit(ri(0, 0, 0, 0));
    // C.JAL +4 differs from RV64 C.ADDIW. Its skipped parcel cannot retire.
    backing[pc] = UINT64_C(17);
    backing[pc + 1] = UINT64_C(32);
    expected_write[pc] = 1;
    expected[pc] = ((pc + 2) & low_mask(32));
    backing[pc + 2] = UINT64_C(0);
    backing[pc + 3] = UINT64_C(0);
    pc += 4;
    // A 32-bit ADDI beginning at byte six crosses a fetch block.
    backing[pc] = UINT64_C(1);
    backing[pc + 1] = UINT64_C(0);
    pc += 2;
    emit(ri(0, 25, 0, 77), 1, 77);
    // Faults from either slot retain the older retirement and raw instruction.
    emit(ld(3, 25, 24, 0)); // LD is not RV32
    emit(ri(0, 25, 0, 78), 1, 78);
    emit(ri(1, 25, 8, 32)); // RV64 shift-immediate bit 5 is reserved
    emit(ri(0, 25, 0, 79), 1, 79);
    emit(UINT64_C(1311899)); // ADDIW is not RV32
    emit(ri(0, 25, 0, 80), 1, 80);
    emit(ld(2, 25, 0,
            UINT64_C(6144))); // negative unmapped VA must not alias low memory
    emit(ri(0, 25, 0, 81), 1, 81);
    split_fault_pc = pc;
    emit(ld(
        2, 25, 24,
        1021)); // 0xffd succeeds first; the second fragment faults at 0x1000.
    emit(ri(0, 25, 0, 82), 1, 82);
    emit(UINT64_C(273678451)); // drain into WFI
    // Trap handler checks the faulting encoding/address via architectural CSRs,
    // then advances the saved EPC. Writes are checked separately below.
    put(UINT64_C(2048), csr(2, UINT64_C(833), 30, 0));
    put(UINT64_C(2052), csr(2, UINT64_C(835), 29, 0));
    put(UINT64_C(2056), ri(0, 30, 30, 4));
    put(UINT64_C(2060), csr(1, UINT64_C(833), 0, 30));
    put(UINT64_C(2064), UINT64_C(807403635));
    for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
      falling();
    reset = 0;
    start_in.pvalid = 1;
    start_in.pbits = UINT64_C(1024);
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
      falling();
    for (int i = 0; i < 131072; i++)
      if (expected_write[i])
        CHECK(seen[i]);
    CHECK(faults == 5 && dual > 0 && completed_count >= 8 && ireads > 0 &&
          dreads > 0 && ureads == 2 && uwrites == 2);
    ;
    throw Finished{};
  }
}

int main() {
  return run_test([] {
    instruction_node_id = 2;
    data_node_id = 3;
    uncached_node_id = 4;
    try {
      stimulus();
      for (;;)
        rising();
    } catch (const Finished &) {
    }
  });
}
