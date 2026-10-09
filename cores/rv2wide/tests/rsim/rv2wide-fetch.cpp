// Preserves the rv2wide-fetch cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
struct Finished {};
extern "C" void rv2wide_fetch_trace_bind();
extern "C" void rv2wide_fetch_trace_expect(unsigned lane, std::uint64_t pc,
                                           unsigned instruction,
                                           unsigned prediction,
                                           unsigned ras_mismatch);
extern "C" void rv2wide_fetch_trace_check(unsigned reset);
extern "C" void rv2wide_fetch_trace_finish();
auto &retired(unsigned i) { return i ? retired_1_out : retired_0_out; }
inline auto &completed = completed_out;
inline auto &redirect = redirect_out;
using retirement_t = std::remove_cvref_t<decltype(retired_0_out.pbits)>;
using CHIReqFlit =
    std::remove_cvref_t<decltype(instruction_chi_out.preq.pbits)>;
// Checks production fetch, selected compressed subsets, prediction, and precise memory/fault recovery.

bool trace_reset = 1;

std::uint8_t backing[131072], model_bytes[131072];
std::uint64_t registers[32];
Queue<retirement_t> completions;
int cycles = 0, reference_pc = 0, commits = 0, dual_run = 0, longest_dual = 0;
int ireads = 0, dreads = 0, acks = 0, replay_count = 0, branch_count = 0,
    faults = 0, phase = 0;
int pbmt_kind = 0, pbmt_fetches = 0, pbmt_loads = 0, pbmt_stores = 0;
bool svinval_remapped = 0;
int wrong_path_reads = 0, detached_refills = 0, completions_seen = 0;
int reset_canceled_refills = 0;
int predicted_branches = 0, predicted_conditional = 0, predicted_straddles = 0;
int compressed_retired = 0, straddled_retired = 0, compressed_dual_run = 0;
int zc_pairs = 0;
int instruction_prefetch_reads = 0;
bool iactive = 0, dactive = 0, wactive = 0;
CHIReqFlit irequest, drequest, wrequest;
int idue, ipacket, ddue, dpacket;
int expected_fault_pc, expected_fault_cause;
std::uint64_t expected_fault_value;
int ustate = 0, udue = 0, ureads = 0, uwrites = 0, fences = 0,
    instruction_fences = 0;
int pauses = 0, last_pause_cycle = 0;
CHIReqFlit urequest;
bool reservation_valid = 0;
std::uint64_t reservation_address;
int reservation_width;

std::uint32_t addi(int rd, int rs, int imm) {
  return (((uint128(((imm)&low_mask(12))) & mask128(12)) << 20) |
          ((uint128(((rs)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
          ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(19)) & mask128(7)) << 0));
}
std::uint32_t jal(int rd, int imm) {
  return (((uint128(((imm >> 20) & low_mask(1))) & mask128(1)) << 31) |
          ((uint128(((imm >> 1) & low_mask(10))) & mask128(10)) << 21) |
          ((uint128(((imm >> 11) & low_mask(1))) & mask128(1)) << 20) |
          ((uint128(((imm >> 12) & low_mask(8))) & mask128(8)) << 12) |
          ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(111)) & mask128(7)) << 0));
}
std::uint32_t bne(int rs1, int rs2, int imm) {
  return (((uint128(((imm >> 12) & low_mask(1))) & mask128(1)) << 31) |
          ((uint128(((imm >> 5) & low_mask(6))) & mask128(6)) << 25) |
          ((uint128(((rs2)&low_mask(5))) & mask128(5)) << 20) |
          ((uint128(((rs1)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
          ((uint128(((imm >> 1) & low_mask(4))) & mask128(4)) << 8) |
          ((uint128(((imm >> 11) & low_mask(1))) & mask128(1)) << 7) |
          ((uint128(UINT64_C(99)) & mask128(7)) << 0));
}
std::uint32_t load(int rd, int rs1, int offset, int width) {
  return (((uint128(((offset)&low_mask(12))) & mask128(12)) << 20) |
          ((uint128(((rs1)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(((width)&low_mask(3))) & mask128(3)) << 12) |
          ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(3)) & mask128(7)) << 0));
}
std::uint32_t store(int rs2, int rs1, int offset, int width) {
  return (((uint128(((offset >> 5) & low_mask(7))) & mask128(7)) << 25) |
          ((uint128(((rs2)&low_mask(5))) & mask128(5)) << 20) |
          ((uint128(((rs1)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(((width)&low_mask(3))) & mask128(3)) << 12) |
          ((uint128(((offset)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(35)) & mask128(7)) << 0));
}
std::uint32_t atomic_insn(int operation, int width, int rd, int rs1,
                          int rs2 = 0) {
  return (((uint128(((operation)&low_mask(5))) & mask128(5)) << 27) |
          ((uint128(UINT64_C(3)) & mask128(2)) << 25) |
          ((uint128(((rs2)&low_mask(5))) & mask128(5)) << 20) |
          ((uint128(((rs1)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(((width)&low_mask(3))) & mask128(3)) << 12) |
          ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(47)) & mask128(7)) << 0));
}
void insn(int pc, std::uint32_t word) {
  for (int b = 0; b < 4; b++)
    backing[pc + b] = sv_slice(word, b * 8, 8);
}
void parcel(int pc, std::uint16_t word) {
  for (int b = 0; b < 2; b++)
    backing[pc + b] = sv_slice(word, b * 8, 8);
}
std::uint16_t c_imm(int funct3, int rd, int value) {
  return (((uint128(((funct3)&low_mask(3))) & mask128(3)) << 13) |
          ((uint128(((value >> 5) & low_mask(1))) & mask128(1)) << 12) |
          ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(((value)&low_mask(5))) & mask128(5)) << 2) |
          ((uint128(UINT64_C(1)) & mask128(2)) << 0));
}
std::uint16_t c_zcb_unary(int rd, int operation) {
  return (((uint128(UINT64_C(39)) & mask128(6)) << 10) |
          ((uint128(((rd - 8) & low_mask(3))) & mask128(3)) << 7) |
          ((uint128(((operation)&low_mask(5))) & mask128(5)) << 2) |
          ((uint128(UINT64_C(1)) & mask128(2)) << 0));
}
std::uint16_t c_zcb_memory(int operation, int rd, int base, int offset,
                           std::uint8_t signed_half = 0) {
  return (((uint128(((operation)&low_mask(6))) & mask128(6)) << 10) |
          ((uint128(((base - 8) & low_mask(3))) & mask128(3)) << 7) |
          ((uint128(((operation == UINT64_C(32) || operation == UINT64_C(34)
                          ? offset
                          : int(signed_half)) &
                     low_mask(1))) &
            mask128(1))
           << 6) |
          ((uint128(((offset >> 1) & low_mask(1))) & mask128(1)) << 5) |
          ((uint128(((rd - 8) & low_mask(3))) & mask128(3)) << 2) |
          ((uint128(UINT64_C(0)) & mask128(2)) << 0));
}
// Independent expansion only for encodings authored by this fixture. The
// shared expander's catalog fixtures cover the remaining C instruction forms.
std::uint32_t expand(std::uint16_t c) {
  int rd, rs, value;
  rd = int(sv_slice(c, 7, (11) - (7) + 1));
  rs = 8 + int(sv_slice(c, 7, (9) - (7) + 1));
  value = int(std::int64_t(sign_extend(
      (((uint128(sv_slice(c, 12, 1)) & mask128(1)) << 5) |
       ((uint128(sv_slice(c, 2, (6) - (2) + 1)) & mask128(5)) << 0)),
      6)));
  if (sv_slice(c, 0, (1) - (0) + 1) == 0)
    switch (sv_slice(c, 10, (15) - (10) + 1)) {
    case UINT64_C(32): {
      return load(8 + int(sv_slice(c, 2, (4) - (2) + 1)), rs,
                  int((((uint128(sv_slice(c, 5, 1)) & mask128(1)) << 1) |
                       ((uint128(sv_slice(c, 6, 1)) & mask128(1)) << 0))),
                  4);
    } break;
    case UINT64_C(33): {
      return load(8 + int(sv_slice(c, 2, (4) - (2) + 1)), rs,
                  int((((uint128(sv_slice(c, 5, 1)) & mask128(1)) << 1) |
                       ((uint128(UINT64_C(0)) & mask128(1)) << 0))),
                  sv_slice(c, 6, 1) ? 1 : 5);
    } break;
    case UINT64_C(34): {
      return store(8 + int(sv_slice(c, 2, (4) - (2) + 1)), rs,
                   int((((uint128(sv_slice(c, 5, 1)) & mask128(1)) << 1) |
                        ((uint128(sv_slice(c, 6, 1)) & mask128(1)) << 0))),
                   0);
    } break;
    case UINT64_C(35): {
      return store(8 + int(sv_slice(c, 2, (4) - (2) + 1)), rs,
                   int((((uint128(sv_slice(c, 5, 1)) & mask128(1)) << 1) |
                        ((uint128(UINT64_C(0)) & mask128(1)) << 0))),
                   1);
    } break;
    default: {
      {
      }
    } break;
    }
  if (sv_slice(c, 0, (1) - (0) + 1) == 1 &&
      sv_slice(c, 10, (15) - (10) + 1) == UINT64_C(39)) {
    if (sv_slice(c, 5, (6) - (5) + 1) == 2)
      return (
          ((uint128(UINT64_C(1)) & mask128(7)) << 25) |
          ((uint128(((8 + int(sv_slice(c, 2, (4) - (2) + 1))) & low_mask(5))) &
            mask128(5))
           << 20) |
          ((uint128(((rs)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
          ((uint128(((rs)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(51)) & mask128(7)) << 0));
    switch (sv_slice(c, 2, (6) - (2) + 1)) {
    case UINT64_C(24): {
      return (((uint128(UINT64_C(255)) & mask128(12)) << 20) |
              ((uint128(((rs)&low_mask(5))) & mask128(5)) << 15) |
              ((uint128(UINT64_C(7)) & mask128(3)) << 12) |
              ((uint128(((rs)&low_mask(5))) & mask128(5)) << 7) |
              ((uint128(UINT64_C(19)) & mask128(7)) << 0));
    } break;
    case UINT64_C(25): {
      return (((uint128(UINT64_C(1540)) & mask128(12)) << 20) |
              ((uint128(((rs)&low_mask(5))) & mask128(5)) << 15) |
              ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
              ((uint128(((rs)&low_mask(5))) & mask128(5)) << 7) |
              ((uint128(UINT64_C(19)) & mask128(7)) << 0));
    } break;
    case UINT64_C(26): {
      return (((uint128(UINT64_C(128)) & mask128(12)) << 20) |
              ((uint128(((rs)&low_mask(5))) & mask128(5)) << 15) |
              ((uint128(UINT64_C(4)) & mask128(3)) << 12) |
              ((uint128(((rs)&low_mask(5))) & mask128(5)) << 7) |
              ((uint128(UINT64_C(59)) & mask128(7)) << 0));
    } break;
    case UINT64_C(27): {
      return (((uint128(UINT64_C(1541)) & mask128(12)) << 20) |
              ((uint128(((rs)&low_mask(5))) & mask128(5)) << 15) |
              ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
              ((uint128(((rs)&low_mask(5))) & mask128(5)) << 7) |
              ((uint128(UINT64_C(19)) & mask128(7)) << 0));
    } break;
    case UINT64_C(28): {
      return (((uint128(UINT64_C(4)) & mask128(7)) << 25) |
              ((uint128(UINT64_C(0)) & mask128(5)) << 20) |
              ((uint128(((rs)&low_mask(5))) & mask128(5)) << 15) |
              ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
              ((uint128(((rs)&low_mask(5))) & mask128(5)) << 7) |
              ((uint128(UINT64_C(59)) & mask128(7)) << 0));
    } break;
    case UINT64_C(29): {
      return (((uint128(UINT64_C(4095)) & mask128(12)) << 20) |
              ((uint128(((rs)&low_mask(5))) & mask128(5)) << 15) |
              ((uint128(UINT64_C(4)) & mask128(3)) << 12) |
              ((uint128(((rs)&low_mask(5))) & mask128(5)) << 7) |
              ((uint128(UINT64_C(19)) & mask128(7)) << 0));
    } break;
    default: {
      {
      }
    } break;
    }
  }
  if ((c & UINT64_C(63615)) == UINT64_C(24577) &&
      sv_slice(c, 7, (11) - (7) + 1) < 16 && sv_slice(c, 7, 1))
    return addi(0, 0, 0);
  switch ((((uint128(sv_slice(c, 13, (15) - (13) + 1)) & mask128(3)) << 2) |
           ((uint128(sv_slice(c, 0, (1) - (0) + 1)) & mask128(2)) << 0))) {
  case UINT64_C(1): {
    return addi(rd, rd, value);
  } break;
  case UINT64_C(9): {
    return addi(rd, 0, value);
  } break;
  case UINT64_C(12): {
    return load(
        8 + int(sv_slice(c, 2, (4) - (2) + 1)), rs,
        int((((uint128(sv_slice(c, 5, (6) - (5) + 1)) & mask128(2)) << 6) |
             ((uint128(sv_slice(c, 10, (12) - (10) + 1)) & mask128(3)) << 3) |
             ((uint128(UINT64_C(0)) & mask128(3)) << 0))),
        3);
  } break;
  case UINT64_C(28): {
    return store(
        8 + int(sv_slice(c, 2, (4) - (2) + 1)), rs,
        int((((uint128(sv_slice(c, 5, (6) - (5) + 1)) & mask128(2)) << 6) |
             ((uint128(sv_slice(c, 10, (12) - (10) + 1)) & mask128(3)) << 3) |
             ((uint128(UINT64_C(0)) & mask128(3)) << 0))),
        3);
  } break;
  case UINT64_C(21): {
    return jal(
        0, int(std::int64_t(sign_extend(
               (((uint128(sv_slice(c, 12, 1)) & mask128(1)) << 11) |
                ((uint128(sv_slice(c, 8, 1)) & mask128(1)) << 10) |
                ((uint128(sv_slice(c, 9, (10) - (9) + 1)) & mask128(2)) << 8) |
                ((uint128(sv_slice(c, 6, 1)) & mask128(1)) << 7) |
                ((uint128(sv_slice(c, 7, 1)) & mask128(1)) << 6) |
                ((uint128(sv_slice(c, 2, 1)) & mask128(1)) << 5) |
                ((uint128(sv_slice(c, 11, 1)) & mask128(1)) << 4) |
                ((uint128(sv_slice(c, 3, (5) - (3) + 1)) & mask128(3)) << 1) |
                ((uint128(UINT64_C(0)) & mask128(1)) << 0)),
               12))));
  } break;
  case UINT64_C(25):
  case UINT64_C(29): {
    {
      value = int(std::int64_t(sign_extend(
          (((uint128(sv_slice(c, 12, 1)) & mask128(1)) << 8) |
           ((uint128(sv_slice(c, 5, (6) - (5) + 1)) & mask128(2)) << 6) |
           ((uint128(sv_slice(c, 2, 1)) & mask128(1)) << 5) |
           ((uint128(sv_slice(c, 10, (11) - (10) + 1)) & mask128(2)) << 3) |
           ((uint128(sv_slice(c, 3, (4) - (3) + 1)) & mask128(2)) << 1) |
           ((uint128(UINT64_C(0)) & mask128(1)) << 0)),
          9)));
      return bne(rs, 0, value) &
             (sv_slice(c, 13, 1) ? UINT64_C(4294967295) : ~UINT64_C(4096));
    }
  } break;
  case UINT64_C(18): {
    {
      if (sv_slice(c, 2, (6) - (2) + 1) == 0 && rd != 0)
        return (((uint128(UINT64_C(0)) & mask128(12)) << 20) |
                ((uint128(((rd)&low_mask(5))) & mask128(5)) << 15) |
                ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
                ((uint128(((sv_slice(c, 12, 1) ? 1 : 0) & low_mask(5))) &
                  mask128(5))
                 << 7) |
                ((uint128(UINT64_C(103)) & mask128(7)) << 0));
      if (!sv_slice(c, 12, 1) && sv_slice(c, 2, (6) - (2) + 1) != 0)
        return (((uint128(UINT64_C(0)) & mask128(7)) << 25) |
                ((uint128(sv_slice(c, 2, (6) - (2) + 1)) & mask128(5)) << 20) |
                ((uint128(UINT64_C(0)) & mask128(5)) << 15) |
                ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
                ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
                ((uint128(UINT64_C(51)) & mask128(7)) << 0));
      if (sv_slice(c, 12, 1) && sv_slice(c, 2, (6) - (2) + 1) != 0)
        return (((uint128(UINT64_C(0)) & mask128(7)) << 25) |
                ((uint128(sv_slice(c, 2, (6) - (2) + 1)) & mask128(5)) << 20) |
                ((uint128(((rd)&low_mask(5))) & mask128(5)) << 15) |
                ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
                ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
                ((uint128(UINT64_C(51)) & mask128(7)) << 0));
    }
  } break;
  default: {
    {
    }
  } break;
  }
  fail(1, "unmodeled compressed instruction %h", c);
  return 0;
}
int instruction_pa(int pc) {
  if (pc >= UINT64_C(4194304) && pc < UINT64_C(4198400))
    return UINT64_C(81920) + (pc & UINT64_C(4095));
  if (pc >= UINT64_C(4198400) && pc < UINT64_C(4202496))
    return (phase == 34 && svinval_remapped ? UINT64_C(98304)
                                            : UINT64_C(90112)) +
           (pc & UINT64_C(4095));
  return pc;
}
std::uint32_t instruction_at(int pc) {
  std::uint32_t word{};
  for (int b = 0; b < 4; b++)
    sv_slice(word, b * 8, 8) = backing[instruction_pa(pc + b)];
  return word;
}
int data_pa(std::uint64_t address) {
  if (phase == 34 && address >= UINT64_C(5251072) &&
      address < UINT64_C(5255168))
    return UINT64_C(73728) + int(address & UINT64_C(4095));
  if (phase == 34 && svinval_remapped && address >= UINT64_C(5242880) &&
      address < UINT64_C(5246976))
    return UINT64_C(94208) + int(address & UINT64_C(4095));
  if (phase >= 6 && address >= UINT64_C(5242880) && address < UINT64_C(5246976))
    return (phase == 13 ? UINT64_C(8192) : UINT64_C(86016)) +
           int(address & UINT64_C(4095));
  if (phase >= 23 && address >= UINT64_C(5246976) &&
      address < UINT64_C(5251072))
    return UINT64_C(94208) + int(address & UINT64_C(4095));
  return int(address);
}

// Public CHI transactions alone drive the byte-addressed backing store.
// Instruction and data identities have independent retained transactions.

void retire(retirement_t got, int lane) {
  std::uint32_t word, raw;
  std::uint64_t value, address;
  std::int64_t imm;
  retirement_t expected;
  bool write_rd;
  int rd, rs1, rs2, bytes, length;
  CHECK((phase == 0 || phase >= 4) &&
        got.pfetched.ppc == ((reference_pc)&low_mask(64)) &&
        !got.pfetched.pfault.pvalid);
  raw = instruction_at(reference_pc);
  length = sv_slice(raw, 0, (1) - (0) + 1) == 3 ? 4 : 2;
  if (length == 2) {
    raw = (((uint128(UINT64_C(0)) & mask128(16)) << 16) |
           ((uint128(sv_slice(raw, 0, (15) - (0) + 1)) & mask128(16)) << 0));
    compressed_retired++;
  } else if ((reference_pc & 7) == 6)
    straddled_retired++;
  word = length == 2 ? expand(sv_slice(raw, 0, (15) - (0) + 1)) : raw;
  CHECK(got.pfetched.praw_uinstruction == raw &&
        got.pfetched.psequential_upc ==
            ((reference_pc + length) & low_mask(64)) &&
        !got.pfetched.pcompressed_uillegal);
  CHECK(got.pfetched.pinstruction == word);
  rd = int(sv_slice(word, 7, (11) - (7) + 1));
  rs1 = int(sv_slice(word, 15, (19) - (15) + 1));
  rs2 = int(sv_slice(word, 20, (24) - (20) + 1));
  value = 0;
  write_rd = 0;
  reference_pc += length;
  switch (sv_slice(word, 0, (6) - (0) + 1)) {
  case UINT64_C(19): {
    {
      if (sv_slice(word, 26, (31) - (26) + 1) == UINT64_C(10) &&
          sv_slice(word, 12, (14) - (12) + 1) == 1)
        value = registers[rs1] |
                (UINT64_C(1) << sv_slice(word, 20, (25) - (20) + 1));
      else if (sv_slice(word, 20, (31) - (20) + 1) == UINT64_C(1720) &&
               sv_slice(word, 12, (14) - (12) + 1) == 5)
        for (int b = 0; b < 8; b++)
          sv_slice(value, b * 8, 8) = sv_slice(registers[rs1], (7 - b) * 8, 8);
      else if (sv_slice(word, 20, (31) - (20) + 1) == UINT64_C(1540) &&
               sv_slice(word, 12, (14) - (12) + 1) == 1)
        value = ((std::int64_t(sign_extend(
                     sv_slice(registers[rs1], 0, (7) - (0) + 1), 8))) &
                 low_mask(64));
      else if (sv_slice(word, 20, (31) - (20) + 1) == UINT64_C(1541) &&
               sv_slice(word, 12, (14) - (12) + 1) == 1)
        value = ((std::int64_t(sign_extend(
                     sv_slice(registers[rs1], 0, (15) - (0) + 1), 16))) &
                 low_mask(64));
      else
        switch (sv_slice(word, 12, (14) - (12) + 1)) {
        case 1: {
          value = registers[rs1] << sv_slice(word, 20, (25) - (20) + 1);
        } break;
        case 4: {
          value = registers[rs1] ^
                  ((std::int64_t(
                       sign_extend(sv_slice(word, 20, (31) - (20) + 1), 12))) &
                   low_mask(64));
        } break;
        case 7: {
          value = registers[rs1] &
                  ((std::int64_t(
                       sign_extend(sv_slice(word, 20, (31) - (20) + 1), 12))) &
                   low_mask(64));
        } break;
        default: {
          value = registers[rs1] +
                  ((std::int64_t(
                       sign_extend(sv_slice(word, 20, (31) - (20) + 1), 12))) &
                   low_mask(64));
        } break;
        }
      write_rd = rd != 0;
    }
  } break;
  case UINT64_C(55): {
    {
      value =
          ((std::int64_t(sign_extend(
               (((uint128(sv_slice(word, 12, (31) - (12) + 1)) & mask128(20))
                 << 12) |
                ((uint128(UINT64_C(0)) & mask128(12)) << 0)),
               32))) &
           low_mask(64));
      write_rd = rd != 0;
    }
  } break;
  case UINT64_C(51):
  case UINT64_C(59): {
    {
      if (sv_slice(word, 25, (31) - (25) + 1) == 1)
        switch (sv_slice(word, 12, (14) - (12) + 1)) {
        case 0: {
          value = registers[rs1] * registers[rs2];
        } break;
        case 4: {
          value = registers[rs2] == 0
                      ? UINT64_MAX
                      : std::uint64_t(
                            std::int64_t(sign_extend(registers[rs1], 64)) /
                            std::int64_t(sign_extend(registers[rs2], 64)));
        } break;
        case 5: {
          value = registers[rs2] == 0 ? UINT64_MAX
                                      : registers[rs1] / registers[rs2];
        } break;
        default: {
          fail(1, "unmodeled fetching M instruction");
        } break;
        }
      else if (sv_slice(word, 25, (31) - (25) + 1) == UINT64_C(16) &&
               sv_slice(word, 12, (14) - (12) + 1) == 4)
        value = (registers[rs1] << 2) + registers[rs2];
      else
        value = registers[rs1] + registers[rs2];
      if (sv_slice(word, 25, (31) - (25) + 1) == UINT64_C(4) &&
          sv_slice(word, 0, (6) - (0) + 1) == UINT64_C(59))
        value = sv_slice(word, 12, (14) - (12) + 1) == 4
                    ? (((uint128(UINT64_C(0)) & mask128(48)) << 16) |
                       ((uint128(sv_slice(registers[rs1], 0, (15) - (0) + 1)) &
                         mask128(16))
                        << 0))
                    : (((uint128(UINT64_C(0)) & mask128(32)) << 32) |
                       ((uint128(sv_slice(registers[rs1], 0, (31) - (0) + 1)) &
                         mask128(32))
                        << 0)) +
                          registers[rs2];
      else if (sv_slice(word, 0, (6) - (0) + 1) == UINT64_C(59))
        value = sign_extend(value, 32);
      write_rd = rd != 0;
    }
  } break;
  case UINT64_C(103): {
    {
      value = ((reference_pc)&low_mask(64));
      write_rd = rd != 0;
      reference_pc = int(
          (registers[rs1] + ((std::int64_t(sign_extend(
                                 sv_slice(word, 20, (31) - (20) + 1), 12))) &
                             low_mask(64))) &
          ~UINT64_C(1));
    }
  } break;
  case UINT64_C(111): {
    {
      value = ((reference_pc)&low_mask(64));
      write_rd = rd != 0;
      imm = ((std::int64_t(sign_extend(
                 (((uint128(sv_slice(word, 31, 1)) & mask128(1)) << 20) |
                  ((uint128(sv_slice(word, 12, (19) - (12) + 1)) & mask128(8))
                   << 12) |
                  ((uint128(sv_slice(word, 20, 1)) & mask128(1)) << 11) |
                  ((uint128(sv_slice(word, 21, (30) - (21) + 1)) & mask128(10))
                   << 1) |
                  ((uint128(UINT64_C(0)) & mask128(1)) << 0)),
                 21))) &
             low_mask(64));
      reference_pc = int(got.pfetched.ppc + imm);
    }
  } break;
  case UINT64_C(99): {
    {
      imm = ((std::int64_t(sign_extend(
                 (((uint128(sv_slice(word, 31, 1)) & mask128(1)) << 12) |
                  ((uint128(sv_slice(word, 7, 1)) & mask128(1)) << 11) |
                  ((uint128(sv_slice(word, 25, (30) - (25) + 1)) & mask128(6))
                   << 5) |
                  ((uint128(sv_slice(word, 8, (11) - (8) + 1)) & mask128(4))
                   << 1) |
                  ((uint128(UINT64_C(0)) & mask128(1)) << 0)),
                 13))) &
             low_mask(64));
      if ((registers[rs1] == registers[rs2]) ==
          (sv_slice(word, 12, (14) - (12) + 1) == 0))
        reference_pc = int(got.pfetched.ppc + imm);
    }
  } break;
  case UINT64_C(3): {
    {
      address = registers[rs1] +
                ((std::int64_t(
                     sign_extend(sv_slice(word, 20, (31) - (20) + 1), 12))) &
                 low_mask(64));
      write_rd = rd != 0;
      CHECK(ustate == 0 ||
            (pbmt_kind != 0 && urequest.paddress >= UINT64_C(81920) &&
             urequest.paddress < UINT64_C(86016)));
      bytes = 1 << sv_slice(word, 12, (13) - (12) + 1);
      for (int b = 0; b < bytes; b++)
        sv_slice(value, b * 8, 8) =
            model_bytes[data_pa(address + ((b)&low_mask(64)))];
      if (bytes < 8 && !sv_slice(word, 14, 1) &&
          sv_slice(value, bytes * 8 - 1, 1))
        value |= UINT64_MAX << (bytes * 8);
    }
  } break;
  case UINT64_C(35): {
    {
      imm = ((std::int64_t(sign_extend(
                 (((uint128(sv_slice(word, 25, (31) - (25) + 1)) & mask128(7))
                   << 5) |
                  ((uint128(sv_slice(word, 7, (11) - (7) + 1)) & mask128(5))
                   << 0)),
                 12))) &
             low_mask(64));
      address = registers[rs1] + imm;
      CHECK(ustate == 0 ||
            (pbmt_kind != 0 && urequest.paddress >= UINT64_C(81920) &&
             urequest.paddress < UINT64_C(86016)));
      for (int b = 0; b < (1 << sv_slice(word, 12, (13) - (12) + 1)); b++)
        model_bytes[data_pa(address + ((b)&low_mask(64)))] =
            sv_slice(registers[rs2], b * 8, 8);
    }
  } break;
  case UINT64_C(47): {
    {
      std::uint64_t replacement;
      bool success;
      address = registers[rs1];
      bytes = 1 << sv_slice(word, 12, (13) - (12) + 1);
      for (int b = 0; b < bytes; b++)
        sv_slice(value, b * 8, 8) = model_bytes[int(address) + b];
      if (bytes == 4)
        value = sign_extend(value, 32);
      replacement = registers[rs2];
      switch (sv_slice(word, 27, (31) - (27) + 1)) {
      case 2: {
        {
          reservation_valid = 1;
          reservation_address = address;
          reservation_width = int(sv_slice(word, 12, (14) - (12) + 1));
        }
      } break;
      case 3: {
        {
          success =
              reservation_valid && address == reservation_address &&
              int(sv_slice(word, 12, (14) - (12) + 1)) == reservation_width;
          value = success ? 0 : 1;
          reservation_valid = 0;
        }
      } break;
      case 0: {
        {
          replacement = value + registers[rs2];
          reservation_valid = 0;
        }
      } break;
      case 1: {
        reservation_valid = 0;
      } break;
      default: {
        fail(1, "unmodeled fetching AMO");
      } break;
      }
      if (sv_slice(word, 27, (31) - (27) + 1) != 2 &&
          (sv_slice(word, 27, (31) - (27) + 1) != 3 || success))
        for (int b = 0; b < bytes; b++)
          model_bytes[int(address) + b] = sv_slice(replacement, b * 8, 8);
      write_rd = rd != 0;
      CHECK(got.pdeferred && ustate == 0);
    }
  } break;
  case UINT64_C(15): {
    {
      if (word == UINT64_C(16777231)) {
        CHECK(!got.pwrite && !got.pdeferred);
        pauses++;
        last_pause_cycle = cycles;
      } else {
        CHECK((ustate == 0 ||
               (pbmt_kind != 0 && urequest.paddress >= UINT64_C(81920) &&
                urequest.paddress < UINT64_C(86016))) &&
              !dactive && completions.size() == 0);
        fences++;
        if (sv_slice(word, 12, (14) - (12) + 1) == 1)
          instruction_fences++;
      }
    }
  } break;
  case UINT64_C(115): {
    {
      CHECK(phase >= 5);
      if (phase == 34 && (word & UINT64_C(4261445631)) == UINT64_C(369098867))
        svinval_remapped = 1;
      if (phase >= 6) {
        if (word == UINT64_C(807403635))
          reference_pc =
              phase == 19 ? expected_fault_pc + 4 : UINT64_C(4194304);
        else if (sv_slice(word, 12, (14) - (12) + 1) == 2) {
          write_rd = rd != 0;
          switch (sv_slice(word, 20, (31) - (20) + 1)) {
          case UINT64_C(834): {
            value = ((expected_fault_cause)&low_mask(64));
          } break;
          case UINT64_C(835): {
            value = expected_fault_value;
          } break;
          case UINT64_C(833): {
            value = ((expected_fault_pc)&low_mask(64));
          } break;
          case UINT64_C(778): {
            value = UINT64_C(4611686018427387904);
          } break;
          default: {
            fail(1, "unexpected paged CSR read");
          } break;
          }
        }
      } else
        switch (got.pfetched.ppc) {
        case UINT64_C(772):
        case UINT64_C(908):
        case UINT64_C(784): {
          {
          } // CSRRW x0 and WFI
        } break;
        case UINT64_C(896): {
          {
            value = 11;
            write_rd = 1;
          } // mcause
        } break;
        case UINT64_C(900): {
          {
            value = UINT64_C(776);
            write_rd = 1;
          } // mepc
        } break;
        case UINT64_C(912): {
          reference_pc = UINT64_C(780); // MRET
        } break;
        default: {
          fail(1, "unexpected CSR PC");
        } break;
        }
    }
  } break;
  default: {
    fail(1, "oracle instruction %h", word);
  } break;
  }
  {
    bool is_branch, pushes, pops;
    std::uint64_t predicted_pc;
    std::uint32_t action, prediction;
    is_branch = (sv_slice(word, 0, (6) - (0) + 1) == UINT64_C(99) ||
                 sv_slice(word, 0, (6) - (0) + 1) == UINT64_C(111) ||
                 sv_slice(word, 0, (6) - (0) + 1) == UINT64_C(103));
    pushes = ((sv_slice(word, 0, (6) - (0) + 1) == UINT64_C(111) ||
               sv_slice(word, 0, (6) - (0) + 1) == UINT64_C(103))) &&
             (rd == 1 || rd == 5);
    pops = sv_slice(word, 0, (6) - (0) + 1) == UINT64_C(103) &&
           (rs1 == 1 || rs1 == 5) && (!pushes || rd != rs1);
    action = pushes ? (pops ? 3 : 1) : (pops ? 2 : 0);
    predicted_pc = got.pfetched.pprediction.pvalid
                       ? got.pfetched.pprediction.ptarget
                       : got.pfetched.psequential_upc;
    prediction =
        is_branch ? (predicted_pc == ((reference_pc)&low_mask(64)) ? 1 : 2) : 0;
    rv2wide_fetch_trace_expect(
        lane, got.pfetched.ppc, raw, prediction,
        int(got.pfetched.pspeculated_uras_uaction != ((action)&low_mask(2))));
  }
  CHECK(got.pwrite == write_rd);
  if (write_rd) {
    CHECK(got.prd == ((rd)&low_mask(5)));
    if (!got.pdeferred)
      CHECK(got.pdata == value);
    registers[rd] = value;
  }
  if (got.pdeferred) {
    expected = got;
    expected.pdata = value;
    completions.push_back(expected);
  }
  commits++;
}

void launch(int address, int cause, int fault_pc,
            std::uint64_t fault_value = UINT64_MAX) {
  int before_faults = faults;
  falling();
  CHECK(halted);
  expected_fault_pc = fault_pc;
  expected_fault_cause = cause;
  expected_fault_value =
      fault_value == UINT64_MAX ? ((fault_pc)&low_mask(64)) : fault_value;
  start_in = {.pvalid = UINT64_C(1), .pbits = ((address)&low_mask(64))};
  falling();
  start_in = {};
  until([&] { return faults > before_faults; });
  // The real core resumes at mtvec now. End this scenario with a coordinated
  // reset before executing the handler, including the external CHI epoch.
  falling();
  reset = 1;
  reset_canceled_refills = ireads - acks;
  iactive = 0;
  dactive = 0;
  wactive = 0;
  ustate = 0;
  for (int r = 0; r < 32; r++)
    registers[r] = 0;
  for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
    falling();
  reset = 0;
}

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
      instruction_chi_in.pdat.presponse.pbits.presp_uerr =
          (irequest.paddress == 192 || irequest.paddress == 512) ? UINT64_C(2)
                                                                 : 0;
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
  defer(trace_reset, reset);
  if (!reset) {
    defer(cycles, cycles + 1);
    if (cycles > 60000)
      fail(1, "timeout phase=%0d pc=%h", phase, reference_pc);
    if (retired(0).pvalid && retired(1).pvalid) {
      dual_run++;
      if (dual_run > longest_dual)
        longest_dual = dual_run;
      if (phase == 14 && dual_run > compressed_dual_run)
        compressed_dual_run = dual_run;
      if (phase == 26)
        zc_pairs++;
    } else
      dual_run = 0;
    for (int lane = 0; lane < 2; lane++)
      if (retired(lane).pvalid) {
        if (retired(lane).pbits.pfetched.pprediction.pvalid) {
          predicted_branches++;
          if (sv_slice(retired(lane).pbits.pfetched.pinstruction, 0,
                       (6) - (0) + 1) == UINT64_C(99))
            predicted_conditional++;
          if (phase == 14 && retired(lane).pbits.pfetched.ppc == UINT64_C(1598))
            predicted_straddles++;
        }
        retire(retired(lane).pbits, lane);
      }
    if (completed.pvalid) {
      retirement_t expected;
      int index;
      index = -1;
      CHECK(!completions.empty());
      for (int i = 0; i < completions.size(); i++)
        if (completions[i].pfetched.ppc == completed.pbits.pfetched.ppc)
          index = i;
      CHECK(index >= 0);
      expected = completions[index];
      completions.erase(completions.begin() + index);
      CHECK(same_instruction(completed.pbits.pfetched, expected.pfetched) &&
            completed.pbits.pwrite == expected.pwrite &&
            completed.pbits.prd == expected.prd);
      if (expected.pwrite) {
        if (completed.pbits.pdata != expected.pdata)
          std::cerr << "completion phase " << phase << " pc " << std::hex
                    << expected.pfetched.ppc << " got " << completed.pbits.pdata
                    << " expected " << expected.pdata << std::dec << '\n';
        CHECK(completed.pbits.pdata == expected.pdata);
      }
      completions_seen++;
    }
    if (redirect.pvalid) {
      switch (redirect.pbits.presolution.pdisposition) {
      case 0: {
        {
          branch_count++;
          if (iactive)
            detached_refills++;
        }
      } break;
      case 2: {
        replay_count++;
      } break;
      case 1: {
        {
          CHECK(redirect.pbits.ppc == ((expected_fault_pc)&low_mask(64)) &&
                redirect.pbits.presolution.pcause ==
                    ((expected_fault_cause)&low_mask(64)) &&
                redirect.pbits.presolution.pvalue == expected_fault_value);
          CHECK(completions.size() == 0);
          CHECK(redirect.pbits.ptarget == (phase >= 5 ? UINT64_C(896) : 0) &&
                !halted);
          if (phase == 0)
            CHECK(reference_pc == 4096 && registers[30] == 77);
          if (phase == 4)
            CHECK(reference_pc == 652);
          if (phase >= 5)
            reference_pc = int(redirect.pbits.ptarget);
          if (phase == 25)
            for (int b = 0; b < 3; b++)
              model_bytes[UINT64_C(90109) + b] =
                  sv_slice(registers[2], b * 8, 8);
          faults++;
        }
      } break;
      case 3: {
        CHECK(phase >= 5);
      } break;
      }
    }
    if (instruction_chi_out.preq.pvalid && instruction_chi_in.preq.pready) {
      if (phase == 28 &&
          instruction_chi_out.preq.pbits.paddress == UINT64_C(3072)) {
        CHECK(registers[2] > 0);
        instruction_prefetch_reads++;
      }
      CHECK(instruction_chi_out.preq.pbits.popcode == UINT64_C(3) &&
            (instruction_chi_out.preq.pbits.paddress < 4096 ||
             (instruction_chi_out.preq.pbits.paddress >= UINT64_C(65536) &&
              instruction_chi_out.preq.pbits.paddress < UINT64_C(131072))) &&
            sv_slice(instruction_chi_out.preq.pbits.paddress, 0,
                     (5) - (0) + 1) == 0);
      defer(irequest, instruction_chi_out.preq.pbits);
      defer(iactive, 1);
      defer(ipacket, 0);
      defer(idue, cycles + 15);
      ireads++;
      if (instruction_chi_out.preq.pbits.paddress == 192)
        wrong_path_reads++;
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
    if (instruction_chi_out.prsp.prequester.pvalid &&
        instruction_chi_in.prsp.prequester.pready)
      acks++;
    if (data_chi_out.prequests.pvalid && data_chi_in.prequests.pready) {
      CHECK(ustate == 0 ||
            (pbmt_kind != 0 && urequest.paddress >= UINT64_C(81920) &&
             urequest.paddress < UINT64_C(86016)));
      CHECK(data_chi_out.prequests.pbits.paddress < 4096 ||
            (data_chi_out.prequests.pbits.paddress >= UINT64_C(65536) &&
             data_chi_out.prequests.pbits.paddress < UINT64_C(131072)));
      switch (data_chi_out.prequests.pbits.popcode) {
      case UINT64_C(2):
      case UINT64_C(7): {
        {
          defer(drequest, data_chi_out.prequests.pbits);
          defer(dactive, 1);
          defer(dpacket, 0);
          defer(ddue, cycles + 70);
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
        fail(1, "data opcode");
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
      if (pbmt_kind != 0) {
        CHECK(uncached_chi_out.preq.pbits.ptgt_uid == 1 &&
              (uncached_chi_out.preq.pbits.popcode == UINT64_C(3) ||
               uncached_chi_out.preq.pbits.popcode == UINT64_C(24)));
        CHECK(uncached_chi_out.preq.pbits.paddress >= UINT64_C(81920) &&
              uncached_chi_out.preq.pbits.paddress < UINT64_C(90112) &&
              uncached_chi_out.preq.pbits.psize_uor_unum_ureq == 3);
        CHECK(uncached_chi_out.preq.pbits.pmem_uattr.pdevice ==
              (pbmt_kind == 2));
        if (uncached_chi_out.preq.pbits.paddress < UINT64_C(86016)) {
          CHECK(uncached_chi_out.preq.pbits.popcode == UINT64_C(3));
          pbmt_fetches++;
        } else if (uncached_chi_out.preq.pbits.popcode == UINT64_C(3))
          pbmt_loads++;
        else
          pbmt_stores++;
      } else {
        CHECK((uncached_chi_out.preq.pbits.popcode == UINT64_C(4) ||
               uncached_chi_out.preq.pbits.popcode == UINT64_C(28)));
        CHECK(uncached_chi_out.preq.pbits.paddress >= UINT64_C(8192) &&
              uncached_chi_out.preq.pbits.paddress < UINT64_C(9216));
        CHECK(uncached_chi_out.preq.pbits.psize_uor_unum_ureq <= 3);
        switch (uncached_chi_out.preq.pbits.paddress) {
        case UINT64_C(8200):
        case UINT64_C(8201):
        case UINT64_C(8707): {
          CHECK(uncached_chi_out.preq.pbits.psize_uor_unum_ureq == 0);
        } break;
        case UINT64_C(8202): {
          CHECK(uncached_chi_out.preq.pbits.psize_uor_unum_ureq == 1);
        } break;
        case UINT64_C(8204):
        case UINT64_C(8960): {
          CHECK(uncached_chi_out.preq.pbits.psize_uor_unum_ureq == 2);
        } break;
        case UINT64_C(8208):
        case UINT64_C(8432): {
          CHECK(uncached_chi_out.preq.pbits.psize_uor_unum_ureq == 3);
        } break;
        default: {
          fail(1, "unexpected or wrong-path IO transaction");
        } break;
        }
        CHECK(uncached_chi_out.preq.pbits.pmem_uattr.pdevice ==
              (uncached_chi_out.preq.pbits.paddress < UINT64_C(8960)));
      }
      defer(urequest, uncached_chi_out.preq.pbits);
      if ((uncached_chi_out.preq.pbits.popcode == UINT64_C(3) ||
           uncached_chi_out.preq.pbits.popcode == UINT64_C(4))) {
        defer(ustate, 1);
        ureads++;
      } else {
        defer(ustate, 2);
        uwrites++;
      }
      defer(udue, cycles + 40);
    }
    if (uncached_chi_in.pdat.presponse.pvalid &&
        uncached_chi_out.pdat.presponse.pready)
      defer(ustate, 0);
    if (uncached_chi_in.prsp.presponse.pvalid &&
        uncached_chi_out.prsp.presponse.pready)
      defer(ustate, ustate == 2 ? 3 : 0);
    if (uncached_chi_out.pdat.prequest.pvalid &&
        uncached_chi_in.pdat.prequest.pready) {
      std::uint16_t expected_mask;
      expected_mask =
          (((1 << (1 << urequest.psize_uor_unum_ureq)) - 1) & low_mask(16))
          << sv_slice(urequest.paddress, 0, (3) - (0) + 1);
      CHECK(uncached_chi_out.pdat.prequest.pbits.pbyte_uenable ==
            expected_mask);
      for (int b = 0; b < 16; b++)
        if (sv_slice(expected_mask, b, 1)) {
          int address;
          address = (int(urequest.paddress) & ~15) + b;
          CHECK(sv_slice(uncached_chi_out.pdat.prequest.pbits.pdata, b * 8,
                         8) == model_bytes[address]);
          backing[address] =
              sv_slice(uncached_chi_out.pdat.prequest.pbits.pdata, b * 8, 8);
        }
      // A device command publishes externally written code before its completion.
      // The old instruction line is already resident; FENCE.I must discard it.
      if (phase == 12 && urequest.paddress == UINT64_C(8432))
        insn(UINT64_C(1792), addi(20, 0, 77));
      defer(ustate, 4);
      defer(udue, cycles + 40);
    }
  }
}

void falling_update() { rv2wide_fetch_trace_check(int(trace_reset)); }

void stimulus() {
  reset = 1;
  {
    rv2wide_fetch_trace_bind();
    start_in = {};
    for (int p = 0; p < 4096; p += 4)
      insn(p, addi(0, 0, 0));
    for (int b = 2048; b < 4092; b++)
      backing[b] = ((b)&low_mask(8));
    for (int b = 0; b < 4096; b++)
      model_bytes[b] = backing[b];
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    // Run long enough to warm the ten-bit history as well as the BTB.
    insn(0, addi(31, 0, 20));
    insn(4, addi(1, 0, 2047));
    insn(8, addi(1, 1, 1));
    insn(12, jal(0, 52));
    for (int p = 64; p < 120; p += 4)
      insn(p, addi(2 + (p - 64) / 4, 0, p));
    insn(120, addi(31, 31, -1));
    insn(124, bne(31, 0, -60));
    insn(128, (((uint128(UINT64_C(0)) & mask128(12)) << 20) |
               ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
               ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
               ((uint128(UINT64_C(16)) & mask128(5)) << 7) |
               ((uint128(UINT64_C(3)) & mask128(7)) << 0))); // delayed LD
    insn(132, addi(17, 16, 1)); // hold issue, filling reserved fetch capacity
    insn(136, addi(18, 17, 1));
    insn(140, addi(19, 18, 1));
    insn(144, (((uint128(UINT64_C(0)) & mask128(7)) << 25) |
               ((uint128(UINT64_C(19)) & mask128(5)) << 20) |
               ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
               ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
               ((uint128(UINT64_C(8)) & mask128(5)) << 7) |
               ((uint128(UINT64_C(35)) & mask128(7)) << 0))); // SD
    insn(148, (((uint128(UINT64_C(8)) & mask128(12)) << 20) |
               ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
               ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
               ((uint128(UINT64_C(20)) & mask128(5)) << 7) |
               ((uint128(UINT64_C(3)) & mask128(7)) << 0)));
    insn(184, addi(5, 0, 316));
    insn(188,
         (((uint128(UINT64_C(0)) & mask128(12)) << 20) |
          ((uint128(UINT64_C(5)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(21)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(103)) & mask128(7))
           << 0))); // cold indirect target: discard erroneous younger line 192
    insn(316, addi(22, 21, 1));
    insn(320, jal(0, 4092 - 320));
    insn(4092, addi(30, 0, 77));
    // An older cold load must finish before the younger illegal instruction
    // reports its fault. Fetch cancellation happens before that delayed report.
    insn(640, addi(1, 0, 2047));
    insn(644, addi(1, 1, 65));
    insn(648, (((uint128(UINT64_C(0)) & mask128(12)) << 20) |
               ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
               ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
               ((uint128(UINT64_C(16)) & mask128(5)) << 7) |
               ((uint128(UINT64_C(3)) & mask128(7)) << 0)));
    insn(652, UINT64_C(4294967295));
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index)
      falling();
    reset = 0;
    launch(0, 1, 4096);
    CHECK(longest_dual >= 5 && dreads > 0 && completions_seen > 0 &&
          branch_count >= 3);
    CHECK(predicted_conditional > 0);
    ;
    CHECK(wrong_path_reads > 0 && detached_refills > 0);
    phase = 1;
    launch(512, 1, 512); // accepted CHI error, not illegal-instruction decoding
    phase = 2;
    launch(515, 0, 515); // odd PC faults without issuing an aligned read
    phase = 3;
    launch(4096, 1, 4096); // unmapped restart never reaches CHI
    phase = 4;
    reference_pc = 640;
    launch(640, 2, 652, UINT64_C(4294967295));
    CHECK(faults == 5 && acks + reset_canceled_refills == ireads);
    // Execute a real handler through L1I: program mtvec, take ECALL, read
    // architectural trap state, advance mepc, return, then sleep after a marker.
    phase = 5;
    reference_pc = UINT64_C(768);
    insn(UINT64_C(768), addi(1, 0, UINT64_C(896)));
    insn(UINT64_C(772), (((uint128(UINT64_C(773)) & mask128(12)) << 20) |
                         ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                         ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                         ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                         ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
    insn(UINT64_C(776), UINT64_C(115));
    insn(UINT64_C(780), addi(12, 0, 99));
    insn(UINT64_C(784), UINT64_C(273678451));
    insn(UINT64_C(896), (((uint128(UINT64_C(834)) & mask128(12)) << 20) |
                         ((uint128(UINT64_C(0)) & mask128(5)) << 15) |
                         ((uint128(UINT64_C(2)) & mask128(3)) << 12) |
                         ((uint128(UINT64_C(10)) & mask128(5)) << 7) |
                         ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
    insn(UINT64_C(900), (((uint128(UINT64_C(833)) & mask128(12)) << 20) |
                         ((uint128(UINT64_C(0)) & mask128(5)) << 15) |
                         ((uint128(UINT64_C(2)) & mask128(3)) << 12) |
                         ((uint128(UINT64_C(11)) & mask128(5)) << 7) |
                         ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
    insn(UINT64_C(904), addi(11, 11, 4));
    insn(UINT64_C(908), (((uint128(UINT64_C(833)) & mask128(12)) << 20) |
                         ((uint128(UINT64_C(11)) & mask128(5)) << 15) |
                         ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                         ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                         ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
    insn(UINT64_C(912), UINT64_C(807403635));
    expected_fault_pc = UINT64_C(776);
    expected_fault_cause = 11;
    expected_fault_value = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(768)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(reference_pc == UINT64_C(788) && registers[10] == 11 &&
          registers[11] == UINT64_C(780) && registers[12] == 99 && faults == 6);
    // Real M-mode setup and MRET into translated S-mode. The same three-level
    // tables serve independent I/D TLBs through the coherent data cache.
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    for (int p = UINT64_C(65536); p < UINT64_C(131072); p++) {
      backing[p] = 0;
      model_bytes[p] = 0;
    }
    {
      std::uint64_t pte;
      pte = (UINT64_C(17) << 10) | 1;
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(65536) + b] = sv_slice(pte, b * 8, 8);
      pte = (UINT64_C(18) << 10) | 1;
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(69648) + b] = sv_slice(pte, b * 8, 8);
      pte = (UINT64_C(20) << 10) | UINT64_C(203);
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(73728) + b] = sv_slice(pte, b * 8, 8);
      pte = (UINT64_C(21) << 10) | UINT64_C(199);
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(75776) + b] = sv_slice(pte, b * 8, 8);
      for (int b = 0; b < 16; b++) {
        backing[UINT64_C(86016) + b] = ((b + 1) & low_mask(8));
        model_bytes[UINT64_C(86016) + b] = ((b + 1) & low_mask(8));
      }
    }
    insn(UINT64_C(768), addi(1, 0, UINT64_C(896)));
    insn(UINT64_C(772), (((uint128(UINT64_C(773)) & mask128(12)) << 20) |
                         ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                         ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                         ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                         ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
    insn(UINT64_C(776), addi(1, 0, 8));
    insn(UINT64_C(780), (((uint128(UINT64_C(0)) & mask128(6)) << 26) |
                         ((uint128(UINT64_C(60)) & mask128(6)) << 20) |
                         ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                         ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                         ((uint128(UINT64_C(1)) & mask128(5)) << 7) |
                         ((uint128(UINT64_C(19)) & mask128(7)) << 0)));
    insn(UINT64_C(784), addi(1, 1, 16));
    insn(UINT64_C(788), (((uint128(UINT64_C(384)) & mask128(12)) << 20) |
                         ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                         ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                         ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                         ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
    insn(UINT64_C(792), UINT64_C(301990003)); // SFENCE.VMA
    insn(UINT64_C(796), addi(1, 0, 2047));
    insn(UINT64_C(800), addi(1, 1, 1));
    insn(UINT64_C(804),
         (((uint128(UINT64_C(768)) & mask128(12)) << 20) |
          ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(115)) & mask128(7)) << 0))); // MPP=S
    insn(UINT64_C(808), (((uint128(UINT64_C(1024)) & mask128(20)) << 12) |
                         ((uint128(UINT64_C(1)) & mask128(5)) << 7) |
                         ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
    insn(UINT64_C(812), (((uint128(UINT64_C(833)) & mask128(12)) << 20) |
                         ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                         ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                         ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                         ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
    insn(UINT64_C(816), UINT64_C(807403635));
    insn(UINT64_C(81920), (((uint128(UINT64_C(1280)) & mask128(20)) << 12) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
    insn(UINT64_C(81924), (((uint128(UINT64_C(0)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(5)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(3)) & mask128(7)) << 0)));
    insn(UINT64_C(81928), addi(6, 5, 1));
    insn(UINT64_C(81932), (((uint128(UINT64_C(0)) & mask128(7)) << 25) |
                           ((uint128(UINT64_C(6)) & mask128(5)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(8)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(35)) & mask128(7)) << 0)));
    insn(UINT64_C(81936), (((uint128(UINT64_C(8)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(7)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(3)) & mask128(7)) << 0)));
    insn(UINT64_C(81940), UINT64_C(301990003));
    insn(UINT64_C(81944), (((uint128(UINT64_C(8)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(8)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(3)) & mask128(7)) << 0)));
    insn(UINT64_C(81948), (((uint128(UINT64_C(1281)) & mask128(20)) << 12) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
    insn(UINT64_C(81952), addi(12, 0, 77));
    insn(UINT64_C(81956), (((uint128(UINT64_C(0)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(9)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(3)) & mask128(7)) << 0)));
    insn(UINT64_C(896), (((uint128(UINT64_C(834)) & mask128(12)) << 20) |
                         ((uint128(UINT64_C(0)) & mask128(5)) << 15) |
                         ((uint128(UINT64_C(2)) & mask128(3)) << 12) |
                         ((uint128(UINT64_C(10)) & mask128(5)) << 7) |
                         ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
    insn(UINT64_C(900), (((uint128(UINT64_C(835)) & mask128(12)) << 20) |
                         ((uint128(UINT64_C(0)) & mask128(5)) << 15) |
                         ((uint128(UINT64_C(2)) & mask128(3)) << 12) |
                         ((uint128(UINT64_C(11)) & mask128(5)) << 7) |
                         ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
    insn(UINT64_C(904), (((uint128(UINT64_C(833)) & mask128(12)) << 20) |
                         ((uint128(UINT64_C(0)) & mask128(5)) << 15) |
                         ((uint128(UINT64_C(2)) & mask128(3)) << 12) |
                         ((uint128(UINT64_C(13)) & mask128(5)) << 7) |
                         ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
    insn(UINT64_C(908), UINT64_C(273678451));
    phase = 6;
    reference_pc = UINT64_C(768);
    expected_fault_pc = UINT64_C(4194340);
    expected_fault_cause = 13;
    expected_fault_value = UINT64_C(5246976);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(768)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(registers[5] == UINT64_C(578437695752307201) &&
          registers[7] == registers[6] && registers[8] == registers[6] &&
          registers[12] == 77 && registers[10] == 13 &&
          registers[11] == UINT64_C(5246976) &&
          registers[13] == UINT64_C(4194340) && faults == 7);
    for (int scenario = 7; scenario <= 8; scenario++) {
      falling();
      reset = 1;
      iactive = 0;
      dactive = 0;
      wactive = 0;
      for (int r = 0; r < 32; r++)
        registers[r] = 0;
      for (int b = 0; b < 16; b++) {
        backing[UINT64_C(86016) + b] = ((b + 1) & low_mask(8));
        model_bytes[UINT64_C(86016) + b] = ((b + 1) & low_mask(8));
      }
      if (scenario == 7) {
        insn(UINT64_C(81952), (((uint128(UINT64_C(0)) & mask128(7)) << 25) |
                               ((uint128(UINT64_C(6)) & mask128(5)) << 20) |
                               ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                               ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
                               ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                               ((uint128(UINT64_C(35)) & mask128(7)) << 0)));
        insn(UINT64_C(81956),
             addi(12, 0, 88)); // younger result must not retire
        expected_fault_pc = UINT64_C(4194336);
        expected_fault_cause = 15;
        expected_fault_value = UINT64_C(5246976);
      } else {
        insn(UINT64_C(81952), addi(12, 0, 77));
        insn(UINT64_C(81956), jal(0, UINT64_C(4096) - UINT64_C(36)));
        expected_fault_pc = UINT64_C(4198400);
        expected_fault_cause = 12;
        expected_fault_value = UINT64_C(4198400);
      }
      phase = scenario;
      reference_pc = UINT64_C(768);
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      reset = 0;
      falling();
      start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(768)};
      falling();
      start_in = {};
      until([&] { return sleeping; });
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      CHECK(registers[10] == ((expected_fault_cause)&low_mask(64)) &&
            registers[11] == expected_fault_value &&
            registers[13] == ((expected_fault_pc)&low_mask(64)) &&
            registers[12] == (scenario == 7 ? 0 : 77) &&
            faults == scenario + 1);
    }
    // Exact-width MMIO, ordinary uncached RAM, and cache-hit ordering. A taken
    // branch skips a device store; a byte-only mapping must not become an 8B read.
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    for (int b = UINT64_C(8192); b < UINT64_C(9216); b++) {
      backing[b] = ((b)&low_mask(8));
      model_bytes[b] = ((b)&low_mask(8));
    }
    insn(UINT64_C(1024), (((uint128(UINT64_C(2)) & mask128(20)) << 12) |
                          ((uint128(UINT64_C(1)) & mask128(5)) << 7) |
                          ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
    insn(UINT64_C(1028), addi(2, 0, -2));
    insn(UINT64_C(1032), addi(3, 0, 2047));
    insn(UINT64_C(1036), addi(3, 3, 1));
    insn(UINT64_C(1040), load(4, 3, 0, 3)); // older cold cached read
    for (int width = 0; width < 4; width++) {
      int pc;
      pc = UINT64_C(1044) + width * 16;
      insn(pc, store(2, 1, 8 + (1 << width), width));
      insn(pc + 4, load(5 + width, 1, 8 + (1 << width), width));
      insn(pc + 8, load(9 + width, 3, 0, 3)); // younger warm cache hit
      insn(pc + 12, UINT64_C(267386895));
    }
    insn(UINT64_C(1108), load(13, 1, UINT64_C(515), 4));
    insn(UINT64_C(1112), store(2, 1, UINT64_C(768), 2));
    insn(UINT64_C(1116), load(14, 1, UINT64_C(768), 6));
    insn(UINT64_C(1120), jal(0, 8));
    insn(UINT64_C(1124), store(2, 1, 0, 3));
    insn(UINT64_C(1128), UINT64_C(273678451));
    phase = 9;
    reference_pc = UINT64_C(1024);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(1024)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(ureads == 6 && uwrites == 5 && fences == 4 && registers[13] == 3 &&
          registers[14] == UINT64_C(4294967294));
    for (int r = 5; r <= 8; r++)
      CHECK(registers[r] == UINT64_C(18446744073709551614));
    // Permission and missing-mapping faults have no device-side effects.
    for (int scenario = 10; scenario <= 11; scenario++) {
      falling();
      reset = 1;
      iactive = 0;
      dactive = 0;
      wactive = 0;
      for (int r = 0; r < 32; r++)
        registers[r] = 0;
      insn(UINT64_C(1024), addi(1, 0, UINT64_C(896)));
      insn(UINT64_C(1028), (((uint128(UINT64_C(773)) & mask128(12)) << 20) |
                            ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                            ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                            ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                            ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(1032), (((uint128(UINT64_C(2)) & mask128(20)) << 12) |
                            ((uint128(UINT64_C(1)) & mask128(5)) << 7) |
                            ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
      insn(UINT64_C(1036), scenario == 10 ? store(0, 1, UINT64_C(256), 3)
                                          : load(5, 1, UINT64_C(516), 4));
      expected_fault_pc = UINT64_C(1036);
      expected_fault_cause = scenario == 10 ? 7 : 5;
      expected_fault_value = scenario == 10 ? UINT64_C(8448) : UINT64_C(8708);
      phase = scenario;
      reference_pc = UINT64_C(1024);
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      reset = 0;
      falling();
      start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(1024)};
      falling();
      start_in = {};
      until([&] { return sleeping; });
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      CHECK(ureads == 6 && uwrites == 5 &&
            registers[10] == ((expected_fault_cause)&low_mask(64)) &&
            registers[11] == expected_fault_value);
    }
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    insn(UINT64_C(1024), addi(10, 0, 0));
    insn(UINT64_C(1028), jal(0, UINT64_C(1792) - UINT64_C(1028)));
    insn(UINT64_C(1792), addi(20, 0, 1));
    insn(UINT64_C(1796), bne(10, 0, 12));
    insn(UINT64_C(1800), jal(0, -712));
    insn(UINT64_C(1808), UINT64_C(273678451));
    insn(UINT64_C(1088), (((uint128(UINT64_C(2)) & mask128(20)) << 12) |
                          ((uint128(UINT64_C(1)) & mask128(5)) << 7) |
                          ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
    insn(UINT64_C(1092), addi(2, 0, 1));
    insn(UINT64_C(1096), store(2, 1, UINT64_C(240), 3));
    insn(UINT64_C(1100), addi(10, 0, 1));
    insn(UINT64_C(1104), UINT64_C(4111));
    insn(UINT64_C(1108), jal(0, UINT64_C(1792) - UINT64_C(1108)));
    phase = 12;
    reference_pc = UINT64_C(1024);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(1024)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(registers[20] == 77 && instruction_fences == 1 && uwrites == 6);
    // Route translated S-mode accesses by PA, but preserve the VA on PMA faults.
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    {
      std::uint64_t pte;
      pte = (UINT64_C(2) << 10) | UINT64_C(199);
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(75776) + b] = sv_slice(pte, b * 8, 8);
    }
    insn(UINT64_C(81920), (((uint128(UINT64_C(1280)) & mask128(20)) << 12) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
    insn(UINT64_C(81924), addi(6, 0, -2));
    insn(UINT64_C(81928), store(6, 1, 8, 0));
    insn(UINT64_C(81932), load(7, 1, 8, 0));
    insn(UINT64_C(81936), store(6, 1, UINT64_C(256), 0));
    insn(UINT64_C(81940), addi(12, 0, 88));
    expected_fault_pc = UINT64_C(4194320);
    expected_fault_cause = 7;
    expected_fault_value = UINT64_C(5243136);
    phase = 13;
    reference_pc = UINT64_C(768);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(768)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(registers[7] == UINT64_C(18446744073709551614) &&
          registers[11] == UINT64_C(5243136) && registers[12] == 0 &&
          ureads == 7 && uwrites == 7);
    // Four C instructions per block, all restart offsets, and 32-bit suffixes
    // across both eight-byte blocks and a cold 64-byte line. The warm loop must
    // still retire two per cycle; delayed C.LD fills the buffers behind it.
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    for (int b = 2048; b < 2064; b++) {
      backing[b] = ((b)&low_mask(8));
      model_bytes[b] = ((b)&low_mask(8));
    }
    insn(UINT64_C(1536), addi(31, 0, 20));
    parcel(UINT64_C(1540), c_imm(2, 8, 7));
    parcel(UINT64_C(1542), c_imm(2, 9, 9));
    for (int p = UINT64_C(1544); p < UINT64_C(1592); p += 2)
      parcel(p, c_imm(2, 10 + ((p / 2) & 1), (p / 2) & 31));
    parcel(UINT64_C(1592), c_imm(0, 31, -1));
    insn(UINT64_C(1594), addi(0, 0, 0));
    insn(UINT64_C(1598),
         bne(31, 0,
             -54)); // warm predicted branch crosses the eight-byte boundary
    insn(UINT64_C(1602), addi(20, 20, 1));
    insn(UINT64_C(1606), addi(21, 20, 2));
    insn(UINT64_C(1610), addi(8, 0, 2047));
    insn(UINT64_C(1614), addi(8, 8, 1));
    parcel(UINT64_C(1618), UINT64_C(24580));
    parcel(UINT64_C(1620), c_imm(0, 9, 1));
    parcel(UINT64_C(1622), UINT64_C(58372));
    parcel(UINT64_C(1624), UINT64_C(25608));
    parcel(UINT64_C(1626), UINT64_C(34218));
    insn(UINT64_C(1628), addi(5, 0, UINT64_C(1666)));
    parcel(UINT64_C(1632), UINT64_C(37506));
    insn(UINT64_C(1634), addi(12, 1, 0));
    insn(UINT64_C(1638), addi(14, 0, 14));
    insn(UINT64_C(1642), UINT64_C(273678451));
    parcel(UINT64_C(1666), c_imm(2, 13, 13));
    parcel(UINT64_C(1668), UINT64_C(49169));
    parcel(UINT64_C(1670), UINT64_C(57361));
    parcel(UINT64_C(1672), UINT64_C(32770));
    parcel(UINT64_C(1674), UINT64_C(40977));
    parcel(UINT64_C(1676), UINT64_C(32770));
    parcel(UINT64_C(1678), UINT64_C(32898));
    phase = 14;
    reference_pc = UINT64_C(1536);
    dual_run = 0;
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(1536)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(compressed_retired >= 80 && compressed_dual_run >= 5 &&
          straddled_retired >= 3 && registers[20] == 1 && registers[21] == 3 &&
          registers[9] == registers[10] && registers[10] == registers[11] &&
          registers[12] == UINT64_C(1634) && registers[13] == 13 &&
          reference_pc == UINT64_C(1646));
    CHECK(predicted_straddles > 0);
    // An illegal compressed encoding retains its 16-bit mtval after an older
    // accepted compressed load drains, without executing its canonical zero.
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    insn(UINT64_C(1024), addi(1, 0, UINT64_C(896)));
    insn(UINT64_C(1028), (((uint128(UINT64_C(773)) & mask128(12)) << 20) |
                          ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                          ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                          ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                          ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
    insn(UINT64_C(1032), addi(8, 0, 2047));
    insn(UINT64_C(1036), addi(8, 8, 1));
    parcel(UINT64_C(1040), UINT64_C(24580));
    parcel(UINT64_C(1042), UINT64_C(32770));
    parcel(UINT64_C(1044), c_imm(2, 12, 9));
    expected_fault_pc = UINT64_C(1042);
    expected_fault_cause = 2;
    expected_fault_value = UINT64_C(32770);
    phase = 15;
    reference_pc = UINT64_C(1024);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(1024)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(registers[11] == UINT64_C(32770) && registers[13] == UINT64_C(1042) &&
          registers[12] == 0);
    // A straddling instruction belongs to its first PC, but a continuation-page
    // fault reports the second page. Then install that page and execute it.
    for (int scenario = 16; scenario <= 17; scenario++) {
      falling();
      reset = 1;
      iactive = 0;
      dactive = 0;
      wactive = 0;
      for (int r = 0; r < 32; r++)
        registers[r] = 0;
      {
        std::uint64_t pte;
        std::uint32_t crossing;
        pte = scenario == 17 ? (UINT64_C(22) << 10) | UINT64_C(203) : 0;
        crossing = addi(12, 0, 77);
        for (int b = 0; b < 8; b++)
          backing[UINT64_C(73736) + b] = sv_slice(pte, b * 8, 8);
        insn(UINT64_C(81920), jal(0, UINT64_C(4094)));
        parcel(UINT64_C(86014), sv_slice(crossing, 0, (15) - (0) + 1));
        parcel(UINT64_C(90112), sv_slice(crossing, 16, (31) - (16) + 1));
        parcel(UINT64_C(90114), c_imm(2, 14, 14));
        insn(UINT64_C(90116), UINT64_C(273678451));
      }
      expected_fault_pc = UINT64_C(4198398);
      expected_fault_cause = 12;
      expected_fault_value = UINT64_C(4198400);
      phase = scenario;
      reference_pc = UINT64_C(768);
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      reset = 0;
      falling();
      start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(768)};
      falling();
      start_in = {};
      until([&] { return sleeping; });
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      if (scenario == 16)
        CHECK(registers[11] == UINT64_C(4198400) &&
              registers[13] == UINT64_C(4198398) && registers[12] == 0);
      else
        CHECK(registers[12] == 77 && registers[14] == 14 &&
              reference_pc == UINT64_C(4198408));
    }
    // The same suffix rule applies to a physical-map access fault.
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    insn(UINT64_C(1032), jal(0, UINT64_C(4094) - UINT64_C(1032)));
    parcel(UINT64_C(4094), UINT64_C(1555));
    expected_fault_pc = UINT64_C(4094);
    expected_fault_cause = 1;
    expected_fault_value = UINT64_C(4096);
    phase = 18;
    reference_pc = UINT64_C(1024);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(1024)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(registers[11] == UINT64_C(4096) && registers[13] == UINT64_C(4094) &&
          registers[12] == 0);
    // C.EBREAK traps at a halfword PC; the handler deliberately skips the
    // following C.LI and MRET must preserve bit one in its halfword target.
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    parcel(UINT64_C(1032), UINT64_C(1));
    parcel(UINT64_C(1034), UINT64_C(36866));
    parcel(UINT64_C(1036), c_imm(2, 12, 12));
    insn(UINT64_C(1038), UINT64_C(273678451));
    insn(UINT64_C(908),
         addi(13, 13, 4)); // Return to 0x40e, whose bit one must survive mepc.
    insn(UINT64_C(912), (((uint128(UINT64_C(833)) & mask128(12)) << 20) |
                         ((uint128(UINT64_C(13)) & mask128(5)) << 15) |
                         ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                         ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                         ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
    insn(UINT64_C(916), UINT64_C(807403635));
    expected_fault_pc = UINT64_C(1034);
    expected_fault_cause = 3;
    expected_fault_value = 0;
    phase = 19;
    reference_pc = UINT64_C(1024);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(1024)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(registers[10] == 3 && registers[11] == 0 &&
          registers[13] == UINT64_C(1038) && registers[12] == 0 &&
          reference_pc == UINT64_C(1042));
    // Fetch mixed compressed sources and full-width M operations through the
    // production caches; WFI waits for every accepted deferred write.
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    parcel(UINT64_C(1536), c_imm(2, 8, -17));
    parcel(UINT64_C(1538), c_imm(2, 9, 7));
    insn(UINT64_C(1540), (((uint128(UINT64_C(1)) & mask128(7)) << 25) |
                          ((uint128(UINT64_C(9)) & mask128(5)) << 20) |
                          ((uint128(UINT64_C(8)) & mask128(5)) << 15) |
                          ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
                          ((uint128(UINT64_C(10)) & mask128(5)) << 7) |
                          ((uint128(UINT64_C(51)) & mask128(7)) << 0)));
    parcel(UINT64_C(1544), c_imm(2, 11, 3));
    insn(UINT64_C(1546), (((uint128(UINT64_C(1)) & mask128(7)) << 25) |
                          ((uint128(UINT64_C(9)) & mask128(5)) << 20) |
                          ((uint128(UINT64_C(10)) & mask128(5)) << 15) |
                          ((uint128(UINT64_C(4)) & mask128(3)) << 12) |
                          ((uint128(UINT64_C(12)) & mask128(5)) << 7) |
                          ((uint128(UINT64_C(51)) & mask128(7)) << 0)));
    parcel(UINT64_C(1550), UINT64_C(34482));
    insn(UINT64_C(1552), (((uint128(UINT64_C(1)) & mask128(7)) << 25) |
                          ((uint128(UINT64_C(9)) & mask128(5)) << 20) |
                          ((uint128(UINT64_C(8)) & mask128(5)) << 15) |
                          ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
                          ((uint128(UINT64_C(14)) & mask128(5)) << 7) |
                          ((uint128(UINT64_C(59)) & mask128(7)) << 0)));
    insn(UINT64_C(1556), (((uint128(UINT64_C(1)) & mask128(7)) << 25) |
                          ((uint128(UINT64_C(9)) & mask128(5)) << 20) |
                          ((uint128(UINT64_C(10)) & mask128(5)) << 15) |
                          ((uint128(UINT64_C(5)) & mask128(3)) << 12) |
                          ((uint128(UINT64_C(15)) & mask128(5)) << 7) |
                          ((uint128(UINT64_C(51)) & mask128(7)) << 0)));
    insn(UINT64_C(1560), UINT64_C(273678451));
    phase = 20;
    reference_pc = UINT64_C(1536);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(1536)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(registers[10] == -UINT64_C(119) && registers[12] == -UINT64_C(17) &&
          registers[13] == -UINT64_C(17) && registers[14] == -UINT64_C(119) &&
          completions.size() == 0 && reference_pc == UINT64_C(1564));
    // B results feed the production LSU and compressed consumers without an
    // extra execution stage; retain all bits through store/load forwarding.
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    parcel(UINT64_C(1792), c_imm(2, 8, 17));
    parcel(UINT64_C(1794), c_imm(2, 9, 3));
    insn(UINT64_C(1796),
         (((uint128(UINT64_C(16)) & mask128(7)) << 25) |
          ((uint128(UINT64_C(9)) & mask128(5)) << 20) |
          ((uint128(UINT64_C(8)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(4)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(10)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(51)) & mask128(7)) << 0))); // SH2ADD
    insn(UINT64_C(1800),
         (((uint128(UINT64_C(10)) & mask128(6)) << 26) |
          ((uint128(UINT64_C(63)) & mask128(6)) << 20) |
          ((uint128(UINT64_C(10)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(11)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(19)) & mask128(7)) << 0))); // BSETI
    insn(UINT64_C(1804), (((uint128(UINT64_C(1720)) & mask128(12)) << 20) |
                          ((uint128(UINT64_C(11)) & mask128(5)) << 15) |
                          ((uint128(UINT64_C(5)) & mask128(3)) << 12) |
                          ((uint128(UINT64_C(12)) & mask128(5)) << 7) |
                          ((uint128(UINT64_C(19)) & mask128(7)) << 0))); // REV8
    insn(UINT64_C(1808), addi(1, 0, UINT64_C(1024)));
    insn(UINT64_C(1812), store(12, 1, 0, 3));
    insn(UINT64_C(1816), load(13, 1, 0, 3));
    parcel(UINT64_C(1820), UINT64_C(34614));
    insn(UINT64_C(1822), UINT64_C(273678451)); // C.MV x14,x13; WFI
    phase = 21;
    reference_pc = UINT64_C(1792);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(1792)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(registers[10] == 71 &&
          registers[11] == UINT64_C(9223372036854775879) &&
          registers[12] == UINT64_C(5116089176692883584) &&
          registers[13] == registers[12] && registers[14] == registers[12] &&
          completions.size() == 0 && reference_pc == UINT64_C(1826));
    // Full fetch/MMU/cache composition: old-value W sign extension, SC status,
    // accepted x0 effects, and branch-killed SC with no mutation.
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    reservation_valid = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    for (int b = 0; b < 8; b++) {
      backing[UINT64_C(832) + b] = UINT64_C(255);
      model_bytes[UINT64_C(832) + b] = UINT64_C(255);
    }
    insn(UINT64_C(1792), addi(1, 0, UINT64_C(832)));
    insn(UINT64_C(1796), addi(2, 0, 7));
    insn(UINT64_C(1800), atomic_insn(2, 2, 3, 1));
    insn(UINT64_C(1804), addi(10, 0, 10));
    insn(UINT64_C(1808), atomic_insn(0, 2, 4, 1, 2));
    insn(UINT64_C(1812), atomic_insn(3, 2, 5, 1, 2));
    insn(UINT64_C(1816), atomic_insn(2, 3, 6, 1));
    insn(UINT64_C(1820), atomic_insn(3, 3, 7, 1, 2));
    insn(UINT64_C(1824), addi(8, 7, 1));
    insn(UINT64_C(1828), atomic_insn(1, 2, 0, 1, 0));
    insn(UINT64_C(1832), load(9, 1, 0, 3));
    insn(UINT64_C(1836), atomic_insn(2, 3, 0, 1));
    insn(UINT64_C(1840), jal(0, 12));
    insn(UINT64_C(1844), atomic_insn(3, 3, 0, 1, 2));
    insn(UINT64_C(1848), addi(11, 0, 99));
    insn(UINT64_C(1852), load(12, 1, 0, 3));
    insn(UINT64_C(1856), UINT64_C(273678451));
    phase = 22;
    reference_pc = UINT64_C(1792);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(1792)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(registers[3] == UINT64_MAX && registers[4] == UINT64_MAX &&
          registers[5] == 1 && registers[6] == UINT64_C(18446744069414584326) &&
          registers[7] == 0 && registers[8] == 1 && registers[9] == 0 &&
          registers[10] == 10 && registers[11] == 0 && registers[12] == 0 &&
          completions.size() == 0 && reference_pc == UINT64_C(1860));
    // Independently translated fragments: the second virtual page deliberately
    // maps to a nonadjacent PA. Missing mappings retain the exact fault VA and
    // leave an accepted store prefix visible to the real trap handler.
    for (int scenario = 23; scenario <= 25; scenario++) {
      std::uint64_t pte, prefix;
      falling();
      reset = 1;
      iactive = 0;
      dactive = 0;
      wactive = 0;
      ustate = 0;
      for (int r = 0; r < 32; r++)
        registers[r] = 0;
      for (int b = UINT64_C(65536); b < UINT64_C(131072); b++) {
        backing[b] = 0;
        model_bytes[b] = 0;
      }
      pte = (UINT64_C(17) << 10) | 1;
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(65536) + b] = sv_slice(pte, b * 8, 8);
      pte = (UINT64_C(18) << 10) | 1;
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(69648) + b] = sv_slice(pte, b * 8, 8);
      pte = (UINT64_C(20) << 10) | UINT64_C(203);
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(73728) + b] = sv_slice(pte, b * 8, 8);
      pte = (UINT64_C(21) << 10) | UINT64_C(199);
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(75776) + b] = sv_slice(pte, b * 8, 8);
      pte = scenario == 23 ? (UINT64_C(23) << 10) | UINT64_C(199) : 0;
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(75784) + b] = sv_slice(pte, b * 8, 8);
      for (int b = 0; b < 16; b++) {
        backing[UINT64_C(90096) + b] = ((UINT64_C(128) + b) & low_mask(8));
        model_bytes[UINT64_C(90096) + b] = backing[UINT64_C(90096) + b];
        backing[UINT64_C(94208) + b] = ((UINT64_C(144) + b) & low_mask(8));
        model_bytes[UINT64_C(94208) + b] = backing[UINT64_C(94208) + b];
      }
      insn(UINT64_C(768), addi(1, 0, UINT64_C(896)));
      insn(UINT64_C(772), (((uint128(UINT64_C(773)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(776), addi(1, 0, 8));
      insn(UINT64_C(780), (((uint128(UINT64_C(0)) & mask128(6)) << 26) |
                           ((uint128(UINT64_C(60)) & mask128(6)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(19)) & mask128(7)) << 0)));
      insn(UINT64_C(784), addi(1, 1, 16));
      insn(UINT64_C(788), (((uint128(UINT64_C(384)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(792), UINT64_C(301990003));
      insn(UINT64_C(796), addi(1, 0, 2047));
      insn(UINT64_C(800), addi(1, 1, 1));
      insn(UINT64_C(804), (((uint128(UINT64_C(768)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(808), (((uint128(UINT64_C(1024)) & mask128(20)) << 12) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
      insn(UINT64_C(812), (((uint128(UINT64_C(833)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(816), UINT64_C(807403635));
      insn(UINT64_C(81920), (((uint128(UINT64_C(1281)) & mask128(20)) << 12) |
                             ((uint128(UINT64_C(1)) & mask128(5)) << 7) |
                             ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
      insn(UINT64_C(81924), addi(1, 1, -3));
      insn(UINT64_C(81928), addi(2, 0, UINT64_C(291)));
      insn(UINT64_C(81932),
           scenario == 25 ? store(2, 1, 0, 3) : load(3, 1, 0, 3));
      if (scenario == 23) {
        insn(UINT64_C(81936), load(4, 1, 0, 3));
        insn(UINT64_C(81940), addi(5, 4, 1));
        insn(UINT64_C(81944), store(2, 1, 0, 3));
        insn(UINT64_C(81948), load(6, 1, 0, 3));
        insn(UINT64_C(81952), load(0, 1, 0, 3));
        insn(UINT64_C(81956), UINT64_C(273678451));
      } else
        insn(UINT64_C(81936), addi(4, 0, 99));
      insn(UINT64_C(896), (((uint128(UINT64_C(834)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(0)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(2)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(10)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(900), (((uint128(UINT64_C(835)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(0)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(2)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(11)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(904), (((uint128(UINT64_C(833)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(0)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(2)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(13)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(908), (((uint128(UINT64_C(22)) & mask128(20)) << 12) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
      insn(UINT64_C(912), addi(1, 1, -8));
      insn(UINT64_C(916), load(14, 1, 0, 3));
      insn(UINT64_C(920), UINT64_C(273678451));
      phase = scenario;
      reference_pc = UINT64_C(768);
      expected_fault_pc = UINT64_C(4194316);
      expected_fault_cause = scenario == 25 ? 15 : 13;
      expected_fault_value = UINT64_C(5246976);
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      reset = 0;
      falling();
      start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(768)};
      falling();
      start_in = {};
      until([&] { return sleeping; });
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      if (scenario == 23)
        CHECK(
            registers[3] == UINT64_C(10706061893083041421) &&
            registers[4] == registers[3] && registers[5] == registers[3] + 1 &&
            registers[6] == UINT64_C(291) && reference_pc == UINT64_C(4194344));
      else {
        prefix = scenario == 25 ? UINT64_C(320561520216456)
                                : UINT64_C(10344361028892658056);
        CHECK(registers[3] == 0 && registers[4] == 0 &&
              registers[10] == ((expected_fault_cause)&low_mask(64)) &&
              registers[11] == UINT64_C(5246976) &&
              registers[13] == UINT64_C(4194316) && registers[14] == prefix);
      }
      CHECK(completions.size() == 0);
    }
    // All Zcb forms execute through the normal decoder, including every compact
    // register, dependent M/B results, cold narrow loads, and masked stores.
    {
      int pc;
      falling();
      reset = 1;
      iactive = 0;
      dactive = 0;
      wactive = 0;
      for (int r = 0; r < 32; r++)
        registers[r] = 0;
      for (int b = UINT64_C(3584); b < UINT64_C(3616); b++) {
        backing[b] = ((UINT64_C(128) + b) & low_mask(8));
        model_bytes[b] = backing[b];
      }
      pc = UINT64_C(1536);
      for (int rd = 8; rd < 16; rd++)
        for (int op = UINT64_C(24); op <= UINT64_C(29); op++) {
          insn(pc, addi(rd, 0, -129));
          pc += 4;
          parcel(pc, c_zcb_unary(rd, op));
          pc += 2;
          insn(pc, addi(17, rd, 1));
          pc += 4;
        }
      for (int rd = 8; rd < 16; rd++) {
        int rs;
        rs = 8 + ((rd + 1) & 7);
        insn(pc, addi(rd, 0, -7));
        pc += 4;
        insn(pc, addi(rs, 0, 3));
        pc += 4;
        parcel(pc, (((uint128(UINT64_C(39)) & mask128(6)) << 10) |
                    ((uint128(((rd - 8) & low_mask(3))) & mask128(3)) << 7) |
                    ((uint128(UINT64_C(2)) & mask128(2)) << 5) |
                    ((uint128(((rs - 8) & low_mask(3))) & mask128(3)) << 2) |
                    ((uint128(UINT64_C(1)) & mask128(2)) << 0)));
        pc += 2;
        insn(pc, addi(17, rd, 1));
        pc += 4;
      }
      insn(pc, (((uint128(UINT64_C(1)) & mask128(20)) << 12) |
                ((uint128(UINT64_C(8)) & mask128(5)) << 7) |
                ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
      pc += 4;
      insn(pc, addi(8, 8, -512));
      pc += 4;
      for (int offset = 0; offset < 4; offset++) {
        parcel(pc, c_zcb_memory(UINT64_C(32), 9 + offset, 8, offset));
        pc += 2;
        insn(pc, addi(9 + offset, 9 + offset, 1));
        pc += 4;
        parcel(pc, c_zcb_memory(UINT64_C(34), 9 + offset, 8, offset));
        pc += 2;
        parcel(pc, c_zcb_memory(UINT64_C(32), 13, 8, offset));
        pc += 2;
      }
      for (int offset = 0; offset < 4; offset += 2) {
        parcel(pc, c_zcb_memory(UINT64_C(33), 10, 8, offset));
        pc += 2; // LHU
        parcel(pc, c_zcb_memory(UINT64_C(33), 11, 8, offset, 1));
        pc += 2; // LH
        parcel(pc, c_zcb_memory(UINT64_C(35), 11, 8, offset));
        pc += 2; // SH
        parcel(pc, c_zcb_memory(UINT64_C(33), 12, 8, offset));
        pc += 2;
      }
      // C.MOP preserves its encoded register, including x1, and has no operands.
      for (int index = 1; index < 16; index += 2) {
        insn(pc, addi(index, 0, 50 + index));
        pc += 4;
        parcel(pc, (((uint128(UINT64_C(3)) & mask128(3)) << 13) |
                    ((uint128(UINT64_C(0)) & mask128(1)) << 12) |
                    ((uint128(((index)&low_mask(5))) & mask128(5)) << 7) |
                    ((uint128(UINT64_C(0)) & mask128(5)) << 2) |
                    ((uint128(UINT64_C(1)) & mask128(2)) << 0)));
        pc += 2;
        insn(pc, addi(16, index, 0));
        pc += 4;
      }
      insn(pc, jal(0, 6));
      pc += 4;
      parcel(pc, c_zcb_memory(UINT64_C(34), 9, 8, 0));
      pc += 2; // wrong-path mutation
      parcel(pc, c_zcb_memory(UINT64_C(32), 15, 8, 0));
      pc += 2;
      insn(pc, UINT64_C(273678451));
      pc += 4;
      phase = 26;
      reference_pc = UINT64_C(1536);
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      reset = 0;
      falling();
      start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(1536)};
      falling();
      start_in = {};
      until([&] { return sleeping; });
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      CHECK(reference_pc == pc && zc_pairs > 0 && registers[16] == 65 &&
            registers[15] == UINT64_C(129) && completions.size() == 0);
    }
    // Selected-subset legality must preserve raw 16-bit trap values and drain
    // an older accepted load. The C-only variant rejects these optional forms;
    // the extended variant rejects adjacent reserved encodings instead.
    for (int scenario = 0; scenario < 2; scenario++) {
      std::uint16_t invalid;
      invalid = scenario == 0
                    ? UINT64_C(40057)
                    : UINT64_C(24833); // reserved unary / zero C.ADDI16SP
      falling();
      reset = 1;
      iactive = 0;
      dactive = 0;
      wactive = 0;
      for (int r = 0; r < 32; r++)
        registers[r] = 0;
      for (int b = 0; b < 8; b++) {
        backing[UINT64_C(2048) + b] = ((UINT64_C(160) + b) & low_mask(8));
        model_bytes[UINT64_C(2048) + b] = backing[UINT64_C(2048) + b];
      }
      insn(UINT64_C(896), (((uint128(UINT64_C(834)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(0)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(2)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(10)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(900), (((uint128(UINT64_C(835)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(0)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(2)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(11)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(904), (((uint128(UINT64_C(833)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(0)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(2)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(13)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(908), UINT64_C(273678451));
      insn(UINT64_C(1024), addi(1, 0, UINT64_C(896)));
      insn(UINT64_C(1028), (((uint128(UINT64_C(773)) & mask128(12)) << 20) |
                            ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                            ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                            ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                            ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(1032), addi(8, 0, 2047));
      insn(UINT64_C(1036), addi(8, 8, 1));
      parcel(UINT64_C(1040), UINT64_C(24580));
      parcel(UINT64_C(1042), invalid);
      parcel(UINT64_C(1044), c_imm(2, 12, 9));
      expected_fault_pc = UINT64_C(1042);
      expected_fault_cause = 2;
      expected_fault_value = ((invalid)&low_mask(64));
      phase = 27;
      reference_pc = UINT64_C(1024);
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      reset = 0;
      falling();
      start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(1024)};
      falling();
      start_in = {};
      until([&] { return sleeping; });
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      CHECK(registers[9] == UINT64_C(12080525177006498208) &&
            registers[11] == ((invalid)&low_mask(64)) &&
            registers[13] == UINT64_C(1042) && registers[12] == 0);
    }
    // Repeated best-effort I hints in a resident loop eventually fill a cold
    // target. The later branch uses that line without another CHI request.
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    insn(UINT64_C(1536), UINT64_C(4279));
    insn(UINT64_C(1540), addi(1, 1, -1024));
    insn(UINT64_C(1544), addi(2, 0, 40));
    insn(UINT64_C(1548), (((uint128(UINT64_C(0)) & mask128(7)) << 25) |
                          ((uint128(UINT64_C(0)) & mask128(5)) << 20) |
                          ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                          ((uint128(UINT64_C(6)) & mask128(3)) << 12) |
                          ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                          ((uint128(UINT64_C(19)) & mask128(7)) << 0)));
    insn(UINT64_C(1552), addi(2, 2, -1));
    insn(UINT64_C(1556), bne(2, 0, -8));
    insn(UINT64_C(1560), jal(0, UINT64_C(3072) - UINT64_C(1560)));
    insn(UINT64_C(3072), addi(5, 0, 123));
    insn(UINT64_C(3076), UINT64_C(273678451));
    phase = 28;
    reference_pc = UINT64_C(1536);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(1536)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(instruction_prefetch_reads == 1 && registers[5] == 123);
    // A timed wait keeps its LR reservation: SC still succeeds after the
    // feed-forward pipeline has been flushed and its retained owner retires.
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    insn(UINT64_C(1536), addi(1, 0, 2047));
    insn(UINT64_C(1540), addi(1, 1, 1));
    insn(UINT64_C(1544), addi(2, 0, 7));
    insn(UINT64_C(1548), atomic_insn(2, 3, 3, 1));
    insn(UINT64_C(1552), UINT64_C(30408819));
    insn(UINT64_C(1556), atomic_insn(3, 3, 4, 1, 2));
    insn(UINT64_C(1560), UINT64_C(273678451));
    for (int b = 0; b < 8; b++) {
      backing[UINT64_C(2048) + b] = ((b)&low_mask(8));
      model_bytes[UINT64_C(2048) + b] = ((b)&low_mask(8));
    }
    phase = 29;
    reference_pc = UINT64_C(1536);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(1536)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (32); ++repeat_index)
      falling();
    CHECK(reference_pc == UINT64_C(1552) && sleeping);
    until([&] { return reference_pc == UINT64_C(1564) && sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(registers[4] == 0 && model_bytes[UINT64_C(2048)] == 7);
    // Hints flow through real fetch/decode and the same traced retirement
    // stream; instruction buffering survives each bounded issue cooldown.
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    insn(UINT64_C(1536), addi(1, 0, 7));
    insn(UINT64_C(1540), UINT64_C(16777231));
    insn(UINT64_C(1544), addi(2, 1, 1));
    insn(UINT64_C(1548), UINT64_C(16777231));
    insn(UINT64_C(1552), addi(3, 2, 1));
    insn(UINT64_C(1556), UINT64_C(273678451));
    phase = 30;
    reference_pc = UINT64_C(1536);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(1536)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(pauses == 2 && registers[3] == 9 && cycles - last_pause_cycle >= 16);
    // Compressed NTL aliases retain their raw parcel and canonical hint through
    // fetch, tracing and decode. Each following ordinary load must refill again.
    {
      int pc, before_reads;
      falling();
      reset = 1;
      iactive = 0;
      dactive = 0;
      wactive = 0;
      for (int r = 0; r < 32; r++)
        registers[r] = 0;
      insn(UINT64_C(1536), addi(1, 0, 2047));
      insn(UINT64_C(1540), addi(1, 1, 1));
      pc = UINT64_C(1544);
      for (int hint = 0; hint < 4; hint++) {
        parcel(pc, UINT64_C(36866) | (((hint + 2) & low_mask(16)) << 2));
        pc += 2;
        for (int repeat_index = 0; repeat_index < (2); ++repeat_index) {
          insn(pc, load(3, 1, hint * 64, 3));
          pc += 4;
          insn(pc, addi(4, 3, 1));
          pc += 4;
        }
        for (int b = 0; b < 8; b++) {
          backing[UINT64_C(2048) + hint * 64 + b] = ((b)&low_mask(8));
          model_bytes[UINT64_C(2048) + hint * 64 + b] = ((b)&low_mask(8));
        }
      }
      insn(pc, UINT64_C(273678451));
      phase = 31;
      reference_pc = UINT64_C(1536);
      before_reads = dreads;
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      reset = 0;
      falling();
      start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(1536)};
      falling();
      start_in = {};
      until([&] { return sleeping; });
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      CHECK(reference_pc == pc + 4 && dreads == before_reads + 8);
    }
    // Enable PBMTE through the real CSR bank, then execute both NC and IO
    // mappings over coherent RAM. Fetch and WB data share one uncached identity.
    for (int kind = 1; kind <= 2; kind++) {
      std::uint64_t pte;
      int before_fetches, before_loads, before_stores;
      falling();
      reset = 1;
      iactive = 0;
      dactive = 0;
      wactive = 0;
      ustate = 0;
      for (int r = 0; r < 32; r++)
        registers[r] = 0;
      for (int p = UINT64_C(65536); p < UINT64_C(131072); p++) {
        backing[p] = 0;
        model_bytes[p] = 0;
      }
      pte = (UINT64_C(17) << 10) | 1;
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(65536) + b] = sv_slice(pte, b * 8, 8);
      pte = (UINT64_C(18) << 10) | 1;
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(69648) + b] = sv_slice(pte, b * 8, 8);
      pte =
          (((kind)&low_mask(64)) << 61) | (UINT64_C(20) << 10) | UINT64_C(203);
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(73728) + b] = sv_slice(pte, b * 8, 8);
      pte =
          (((kind)&low_mask(64)) << 61) | (UINT64_C(21) << 10) | UINT64_C(199);
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(75776) + b] = sv_slice(pte, b * 8, 8);
      for (int b = 0; b < 16; b++) {
        backing[UINT64_C(86016) + b] = ((b + 1) & low_mask(8));
        model_bytes[UINT64_C(86016) + b] = ((b + 1) & low_mask(8));
      }
      insn(UINT64_C(768), addi(1, 0, UINT64_C(896)));
      insn(UINT64_C(772), (((uint128(UINT64_C(773)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(776), addi(1, 0, 8));
      insn(UINT64_C(780), (((uint128(UINT64_C(0)) & mask128(6)) << 26) |
                           ((uint128(UINT64_C(60)) & mask128(6)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(19)) & mask128(7)) << 0)));
      insn(UINT64_C(784), addi(1, 1, 16));
      insn(UINT64_C(788), (((uint128(UINT64_C(384)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(792), UINT64_C(301990003));
      insn(UINT64_C(796), addi(1, 0, 2047));
      insn(UINT64_C(800), addi(1, 1, 1));
      insn(UINT64_C(804), (((uint128(UINT64_C(768)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(808), (((uint128(UINT64_C(1024)) & mask128(20)) << 12) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
      insn(UINT64_C(812), (((uint128(UINT64_C(833)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(816), addi(1, 0, 1));
      insn(UINT64_C(820), (((uint128(UINT64_C(0)) & mask128(6)) << 26) |
                           ((uint128(UINT64_C(62)) & mask128(6)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(19)) & mask128(7)) << 0)));
      insn(UINT64_C(824), (((uint128(UINT64_C(778)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(828), (((uint128(UINT64_C(778)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(0)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(2)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(2)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(832), UINT64_C(807403635));
      insn(UINT64_C(81920), (((uint128(UINT64_C(1280)) & mask128(20)) << 12) |
                             ((uint128(UINT64_C(1)) & mask128(5)) << 7) |
                             ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
      insn(UINT64_C(81924), load(5, 1, 0, 3));
      insn(UINT64_C(81928), addi(6, 5, 1));
      insn(UINT64_C(81932), store(6, 1, 8, 3));
      insn(UINT64_C(81936), load(7, 1, 8, 3));
      insn(UINT64_C(81940), jal(0, UINT64_C(44)));
      insn(UINT64_C(81944), store(0, 1, 0, 3)); // Squashed by the branch.
      insn(UINT64_C(81984), load(8, 1, 0, 3));
      insn(UINT64_C(81988), UINT64_C(4111));
      insn(UINT64_C(81992), load(9, 1, 8, 3));
      insn(UINT64_C(81996), UINT64_C(273678451));
      phase = 31 + kind;
      pbmt_kind = kind;
      reference_pc = UINT64_C(768);
      before_fetches = pbmt_fetches;
      before_loads = pbmt_loads;
      before_stores = pbmt_stores;
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      reset = 0;
      falling();
      start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(768)};
      falling();
      start_in = {};
      until([&] { return sleeping; });
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      CHECK(reference_pc == UINT64_C(4194384) &&
            registers[2] == UINT64_C(4611686018427387904) &&
            registers[5] == UINT64_C(578437695752307201) &&
            registers[7] == registers[6] && registers[8] == registers[5] &&
            registers[9] == registers[6]);
      CHECK(pbmt_fetches > before_fetches && pbmt_loads == before_loads + 4 &&
            pbmt_stores == before_stores + 1);
      ;
    }
    // Rewrite warm I/D mappings through a coherent virtual alias of the leaf
    // table. No harness memory mutation occurs while the hart is executing.
    {
      std::uint64_t pte;
      falling();
      reset = 1;
      iactive = 0;
      dactive = 0;
      wactive = 0;
      ustate = 0;
      for (int r = 0; r < 32; r++)
        registers[r] = 0;
      for (int p = UINT64_C(65536); p < UINT64_C(131072); p++) {
        backing[p] = 0;
        model_bytes[p] = 0;
      }
      pte = (UINT64_C(17) << 10) | 1;
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(65536) + b] = sv_slice(pte, b * 8, 8);
      pte = (UINT64_C(18) << 10) | 1;
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(69648) + b] = sv_slice(pte, b * 8, 8);
      pte = (UINT64_C(20) << 10) | UINT64_C(203);
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(73728) + b] = sv_slice(pte, b * 8, 8);
      pte = (UINT64_C(22) << 10) | UINT64_C(203);
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(73736) + b] = sv_slice(pte, b * 8, 8);
      pte = (UINT64_C(21) << 10) | UINT64_C(199);
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(75776) + b] = sv_slice(pte, b * 8, 8);
      pte = (UINT64_C(18) << 10) | UINT64_C(199);
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(75792) + b] = sv_slice(pte, b * 8, 8);
      for (int b = 0; b < 8; b++) {
        backing[UINT64_C(86016) + b] = ((b + 1) & low_mask(8));
        model_bytes[UINT64_C(86016) + b] = ((b + 1) & low_mask(8));
        backing[UINT64_C(94208) + b] = ((b + 17) & low_mask(8));
        model_bytes[UINT64_C(94208) + b] = ((b + 17) & low_mask(8));
      }
      insn(UINT64_C(768), addi(1, 0, 8));
      insn(UINT64_C(772), (((uint128(UINT64_C(0)) & mask128(6)) << 26) |
                           ((uint128(UINT64_C(60)) & mask128(6)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(19)) & mask128(7)) << 0)));
      insn(UINT64_C(776), addi(1, 1, 16));
      insn(UINT64_C(780), (((uint128(UINT64_C(384)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(784), addi(1, 0, 2047));
      insn(UINT64_C(788), addi(1, 1, 1));
      insn(UINT64_C(792), (((uint128(UINT64_C(768)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(796), (((uint128(UINT64_C(1024)) & mask128(20)) << 12) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
      insn(UINT64_C(800), (((uint128(UINT64_C(833)) & mask128(12)) << 20) |
                           ((uint128(UINT64_C(1)) & mask128(5)) << 15) |
                           ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
                           ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                           ((uint128(UINT64_C(115)) & mask128(7)) << 0)));
      insn(UINT64_C(804), UINT64_C(807403635));
      insn(UINT64_C(81920), (((uint128(UINT64_C(1280)) & mask128(20)) << 12) |
                             ((uint128(UINT64_C(1)) & mask128(5)) << 7) |
                             ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
      insn(UINT64_C(81924), load(5, 1, 0, 3));
      insn(UINT64_C(81928), jal(10, UINT64_C(4088)));
      insn(UINT64_C(81932), (((uint128(UINT64_C(1283)) & mask128(20)) << 12) |
                             ((uint128(UINT64_C(2)) & mask128(5)) << 7) |
                             ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
      insn(UINT64_C(81936), addi(2, 2, -2048));
      insn(UINT64_C(81940), (((uint128(UINT64_C(6)) & mask128(20)) << 12) |
                             ((uint128(UINT64_C(3)) & mask128(5)) << 7) |
                             ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
      insn(UINT64_C(81944), addi(3, 3, -825));
      insn(UINT64_C(81948), store(3, 2, 0, 3));
      insn(UINT64_C(81952), (((uint128(UINT64_C(1282)) & mask128(20)) << 12) |
                             ((uint128(UINT64_C(4)) & mask128(5)) << 7) |
                             ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
      insn(UINT64_C(81956), (((uint128(UINT64_C(6)) & mask128(20)) << 12) |
                             ((uint128(UINT64_C(3)) & mask128(5)) << 7) |
                             ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
      insn(UINT64_C(81960), addi(3, 3, UINT64_C(203)));
      insn(UINT64_C(81964), store(3, 4, 8, 3));
      insn(UINT64_C(81968), UINT64_C(402653299)); // SFENCE.W.INVAL
      insn(UINT64_C(81972),
           UINT64_C(
               369131635)); // SINVAL.VMA x1,x0: conservative full invalidation
      insn(UINT64_C(81976), UINT64_C(403701875)); // SFENCE.INVAL.IR
      insn(UINT64_C(81980), load(7, 1, 0, 3));
      insn(UINT64_C(81984), jal(10, UINT64_C(4032)));
      insn(UINT64_C(81988), UINT64_C(273678451));
      insn(UINT64_C(90112), addi(9, 0, 11));
      insn(UINT64_C(90116), (((uint128(UINT64_C(0)) & mask128(12)) << 20) |
                             ((uint128(UINT64_C(10)) & mask128(5)) << 15) |
                             ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
                             ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                             ((uint128(UINT64_C(103)) & mask128(7)) << 0)));
      insn(UINT64_C(98304), addi(9, 9, 22));
      insn(UINT64_C(98308), (((uint128(UINT64_C(0)) & mask128(12)) << 20) |
                             ((uint128(UINT64_C(10)) & mask128(5)) << 15) |
                             ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
                             ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
                             ((uint128(UINT64_C(103)) & mask128(7)) << 0)));
      phase = 34;
      pbmt_kind = 0;
      svinval_remapped = 0;
      reference_pc = UINT64_C(768);
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      reset = 0;
      falling();
      start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(768)};
      falling();
      start_in = {};
      until([&] { return sleeping; });
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      CHECK(reference_pc == UINT64_C(4194376) && svinval_remapped &&
            registers[5] == UINT64_C(578437695752307201) &&
            registers[7] == UINT64_C(1735880461161533969) &&
            registers[9] == 33);
    };
    rv2wide_fetch_trace_finish();
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
