// Checks fetching, compressed execution, memory and trap recovery with prediction disabled.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
using retirement_t = std::remove_cvref_t<decltype(retired_0_out.pbits)>;
using CHIReqFlit =
    std::remove_cvref_t<decltype(instruction_chi_out.preq.pbits)>;
const auto &retired(unsigned i) { return i ? retired_1_out : retired_0_out; }
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
  return (field(((imm)&low_mask(12)), 12, 20) |
          field(((rs)&low_mask(5)), 5, 15) | field(UINT64_C(0), 3, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(19), 7, 0));
}
std::uint32_t jal(int rd, int imm) {
  return (field(((imm >> 20) & low_mask(1)), 1, 31) |
          field(((imm >> 1) & low_mask(10)), 10, 21) |
          field(((imm >> 11) & low_mask(1)), 1, 20) |
          field(((imm >> 12) & low_mask(8)), 8, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(0x6f), 7, 0));
}
std::uint32_t bne(int rs1, int rs2, int imm) {
  return (
      field(((imm >> 12) & low_mask(1)), 1, 31) |
      field(((imm >> 5) & low_mask(6)), 6, 25) |
      field(((rs2)&low_mask(5)), 5, 20) | field(((rs1)&low_mask(5)), 5, 15) |
      field(UINT64_C(1), 3, 12) | field(((imm >> 1) & low_mask(4)), 4, 8) |
      field(((imm >> 11) & low_mask(1)), 1, 7) | field(UINT64_C(0x63), 7, 0));
}
std::uint32_t load(int rd, int rs1, int offset, int width) {
  return (field(((offset)&low_mask(12)), 12, 20) |
          field(((rs1)&low_mask(5)), 5, 15) |
          field(((width)&low_mask(3)), 3, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(3), 7, 0));
}
std::uint32_t store(int rs2, int rs1, int offset, int width) {
  return (field(((offset >> 5) & low_mask(7)), 7, 25) |
          field(((rs2)&low_mask(5)), 5, 20) |
          field(((rs1)&low_mask(5)), 5, 15) |
          field(((width)&low_mask(3)), 3, 12) |
          field(((offset)&low_mask(5)), 5, 7) | field(UINT64_C(0x23), 7, 0));
}
std::uint32_t atomic_insn(int operation, int width, int rd, int rs1,
                          int rs2 = 0) {
  return (field(((operation)&low_mask(5)), 5, 27) | field(UINT64_C(3), 2, 25) |
          field(((rs2)&low_mask(5)), 5, 20) |
          field(((rs1)&low_mask(5)), 5, 15) |
          field(((width)&low_mask(3)), 3, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(0x2f), 7, 0));
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
  return (field(((funct3)&low_mask(3)), 3, 13) |
          field(((value >> 5) & low_mask(1)), 1, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(((value)&low_mask(5)), 5, 2) |
          field(UINT64_C(1), 2, 0));
}
std::uint16_t c_zcb_unary(int rd, int operation) {
  return (field(UINT64_C(0x27), 6, 10) | field(((rd - 8) & low_mask(3)), 3, 7) |
          field(((operation)&low_mask(5)), 5, 2) | field(UINT64_C(1), 2, 0));
}
std::uint16_t c_zcb_memory(int operation, int rd, int base, int offset,
                           std::uint8_t signed_half = 0) {
  return (field(((operation)&low_mask(6)), 6, 10) |
          field(((base - 8) & low_mask(3)), 3, 7) |
          field(((operation == UINT64_C(0x20) || operation == UINT64_C(0x22)
                      ? offset
                      : int(signed_half)) &
                 low_mask(1)),
                1, 6) |
          field(((offset >> 1) & low_mask(1)), 1, 5) |
          field(((rd - 8) & low_mask(3)), 3, 2) | field(UINT64_C(0), 2, 0));
}
// Independent expansion only for encodings authored by this fixture. The
// shared expander's catalog fixtures cover the remaining C instruction forms.
std::uint32_t expand(std::uint16_t c) {
  int rd, rs, value;
  rd = int(sv_slice(c, 7, (11) - (7) + 1));
  rs = 8 + int(sv_slice(c, 7, (9) - (7) + 1));
  value =
      int(std::int64_t(sign_extend((field(sv_slice(c, 12, 1), 1, 5) |
                                    field(sv_slice(c, 2, (6) - (2) + 1), 5, 0)),
                                   6)));
  if (sv_slice(c, 0, (1) - (0) + 1) == 0)
    switch (sv_slice(c, 10, (15) - (10) + 1)) {
    case UINT64_C(0x20): {
      return load(8 + int(sv_slice(c, 2, (4) - (2) + 1)), rs,
                  int((field(sv_slice(c, 5, 1), 1, 1) |
                       field(sv_slice(c, 6, 1), 1, 0))),
                  4);
    } break;
    case UINT64_C(0x21): {
      return load(
          8 + int(sv_slice(c, 2, (4) - (2) + 1)), rs,
          int((field(sv_slice(c, 5, 1), 1, 1) | field(UINT64_C(0), 1, 0))),
          sv_slice(c, 6, 1) ? 1 : 5);
    } break;
    case UINT64_C(0x22): {
      return store(8 + int(sv_slice(c, 2, (4) - (2) + 1)), rs,
                   int((field(sv_slice(c, 5, 1), 1, 1) |
                        field(sv_slice(c, 6, 1), 1, 0))),
                   0);
    } break;
    case UINT64_C(0x23): {
      return store(
          8 + int(sv_slice(c, 2, (4) - (2) + 1)), rs,
          int((field(sv_slice(c, 5, 1), 1, 1) | field(UINT64_C(0), 1, 0))), 1);
    } break;
    default: {
      {
      }
    } break;
    }
  if (sv_slice(c, 0, (1) - (0) + 1) == 1 &&
      sv_slice(c, 10, (15) - (10) + 1) == UINT64_C(0x27)) {
    if (sv_slice(c, 5, (6) - (5) + 1) == 2)
      return (field(UINT64_C(1), 7, 25) |
              field(((8 + int(sv_slice(c, 2, (4) - (2) + 1))) & low_mask(5)), 5,
                    20) |
              field(((rs)&low_mask(5)), 5, 15) | field(UINT64_C(0), 3, 12) |
              field(((rs)&low_mask(5)), 5, 7) | field(UINT64_C(0x33), 7, 0));
    switch (sv_slice(c, 2, (6) - (2) + 1)) {
    case UINT64_C(24): {
      return (field(UINT64_C(0xff), 12, 20) | field(((rs)&low_mask(5)), 5, 15) |
              field(UINT64_C(7), 3, 12) | field(((rs)&low_mask(5)), 5, 7) |
              field(UINT64_C(19), 7, 0));
    } break;
    case UINT64_C(25): {
      return (field(UINT64_C(0x604), 12, 20) |
              field(((rs)&low_mask(5)), 5, 15) | field(UINT64_C(1), 3, 12) |
              field(((rs)&low_mask(5)), 5, 7) | field(UINT64_C(19), 7, 0));
    } break;
    case UINT64_C(26): {
      return (field(UINT64_C(0x80), 12, 20) | field(((rs)&low_mask(5)), 5, 15) |
              field(UINT64_C(4), 3, 12) | field(((rs)&low_mask(5)), 5, 7) |
              field(UINT64_C(0x3b), 7, 0));
    } break;
    case UINT64_C(27): {
      return (field(UINT64_C(0x605), 12, 20) |
              field(((rs)&low_mask(5)), 5, 15) | field(UINT64_C(1), 3, 12) |
              field(((rs)&low_mask(5)), 5, 7) | field(UINT64_C(19), 7, 0));
    } break;
    case UINT64_C(28): {
      return (field(UINT64_C(4), 7, 25) | field(UINT64_C(0), 5, 20) |
              field(((rs)&low_mask(5)), 5, 15) | field(UINT64_C(0), 3, 12) |
              field(((rs)&low_mask(5)), 5, 7) | field(UINT64_C(0x3b), 7, 0));
    } break;
    case UINT64_C(29): {
      return (field(UINT64_C(0xfff), 12, 20) |
              field(((rs)&low_mask(5)), 5, 15) | field(UINT64_C(4), 3, 12) |
              field(((rs)&low_mask(5)), 5, 7) | field(UINT64_C(19), 7, 0));
    } break;
    default: {
      {
      }
    } break;
    }
  }
  if ((c & UINT64_C(0xf87f)) == UINT64_C(0x6001) &&
      sv_slice(c, 7, (11) - (7) + 1) < 16 && sv_slice(c, 7, 1))
    return addi(0, 0, 0);
  switch ((field(sv_slice(c, 13, (15) - (13) + 1), 3, 2) |
           field(sv_slice(c, 0, (1) - (0) + 1), 2, 0))) {
  case UINT64_C(1): {
    return addi(rd, rd, value);
  } break;
  case UINT64_C(9): {
    return addi(rd, 0, value);
  } break;
  case UINT64_C(12): {
    return load(8 + int(sv_slice(c, 2, (4) - (2) + 1)), rs,
                int((field(sv_slice(c, 5, (6) - (5) + 1), 2, 6) |
                     field(sv_slice(c, 10, (12) - (10) + 1), 3, 3) |
                     field(UINT64_C(0), 3, 0))),
                3);
  } break;
  case UINT64_C(28): {
    return store(8 + int(sv_slice(c, 2, (4) - (2) + 1)), rs,
                 int((field(sv_slice(c, 5, (6) - (5) + 1), 2, 6) |
                      field(sv_slice(c, 10, (12) - (10) + 1), 3, 3) |
                      field(UINT64_C(0), 3, 0))),
                 3);
  } break;
  case UINT64_C(21): {
    return jal(
        0,
        int(std::int64_t(sign_extend(
            (field(sv_slice(c, 12, 1), 1, 11) |
             field(sv_slice(c, 8, 1), 1, 10) |
             field(sv_slice(c, 9, (10) - (9) + 1), 2, 8) |
             field(sv_slice(c, 6, 1), 1, 7) | field(sv_slice(c, 7, 1), 1, 6) |
             field(sv_slice(c, 2, 1), 1, 5) | field(sv_slice(c, 11, 1), 1, 4) |
             field(sv_slice(c, 3, (5) - (3) + 1), 3, 1) |
             field(UINT64_C(0), 1, 0)),
            12))));
  } break;
  case UINT64_C(25):
  case UINT64_C(29): {
    {
      value = int(std::int64_t(
          sign_extend((field(sv_slice(c, 12, 1), 1, 8) |
                       field(sv_slice(c, 5, (6) - (5) + 1), 2, 6) |
                       field(sv_slice(c, 2, 1), 1, 5) |
                       field(sv_slice(c, 10, (11) - (10) + 1), 2, 3) |
                       field(sv_slice(c, 3, (4) - (3) + 1), 2, 1) |
                       field(UINT64_C(0), 1, 0)),
                      9)));
      return bne(rs, 0, value) &
             (sv_slice(c, 13, 1) ? UINT64_C(0xffffffff) : ~UINT64_C(0x1000));
    }
  } break;
  case UINT64_C(18): {
    {
      if (sv_slice(c, 2, (6) - (2) + 1) == 0 && rd != 0)
        return (field(UINT64_C(0), 12, 20) | field(((rd)&low_mask(5)), 5, 15) |
                field(UINT64_C(0), 3, 12) |
                field(((sv_slice(c, 12, 1) ? 1 : 0) & low_mask(5)), 5, 7) |
                field(UINT64_C(0x67), 7, 0));
      if (!sv_slice(c, 12, 1) && sv_slice(c, 2, (6) - (2) + 1) != 0)
        return (field(UINT64_C(0), 7, 25) |
                field(sv_slice(c, 2, (6) - (2) + 1), 5, 20) |
                field(UINT64_C(0), 5, 15) | field(UINT64_C(0), 3, 12) |
                field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(0x33), 7, 0));
      if (sv_slice(c, 12, 1) && sv_slice(c, 2, (6) - (2) + 1) != 0)
        return (field(UINT64_C(0), 7, 25) |
                field(sv_slice(c, 2, (6) - (2) + 1), 5, 20) |
                field(((rd)&low_mask(5)), 5, 15) | field(UINT64_C(0), 3, 12) |
                field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(0x33), 7, 0));
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
  if (pc >= UINT64_C(0x400000) && pc < UINT64_C(0x401000))
    return UINT64_C(0x14000) + (pc & UINT64_C(0xfff));
  if (pc >= UINT64_C(0x401000) && pc < UINT64_C(0x402000))
    return (phase == 34 && svinval_remapped ? UINT64_C(0x18000)
                                            : UINT64_C(0x16000)) +
           (pc & UINT64_C(0xfff));
  return pc;
}
std::uint32_t instruction_at(int pc) {
  std::uint32_t word;
  for (int b = 0; b < 4; b++)
    sv_slice(word, b * 8, 8) = backing[instruction_pa(pc + b)];
  return word;
}
int data_pa(std::uint64_t address) {
  if (phase == 34 && address >= UINT64_C(0x502000) &&
      address < UINT64_C(0x503000))
    return UINT64_C(0x12000) + int(address & UINT64_C(0xfff));
  if (phase == 34 && svinval_remapped && address >= UINT64_C(0x500000) &&
      address < UINT64_C(0x501000))
    return UINT64_C(0x17000) + int(address & UINT64_C(0xfff));
  if (phase >= 6 && address >= UINT64_C(0x500000) &&
      address < UINT64_C(0x501000))
    return (phase == 13 ? UINT64_C(0x2000) : UINT64_C(0x15000)) +
           int(address & UINT64_C(0xfff));
  if (phase >= 23 && address >= UINT64_C(0x501000) &&
      address < UINT64_C(0x502000))
    return UINT64_C(0x17000) + int(address & UINT64_C(0xfff));
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
    raw = (field(UINT64_C(0), 16, 16) |
           field(sv_slice(raw, 0, (15) - (0) + 1), 16, 0));
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
      else if (sv_slice(word, 20, (31) - (20) + 1) == UINT64_C(0x6b8) &&
               sv_slice(word, 12, (14) - (12) + 1) == 5)
        for (int b = 0; b < 8; b++)
          sv_slice(value, b * 8, 8) = sv_slice(registers[rs1], (7 - b) * 8, 8);
      else if (sv_slice(word, 20, (31) - (20) + 1) == UINT64_C(0x604) &&
               sv_slice(word, 12, (14) - (12) + 1) == 1)
        value = ((std::int64_t(sign_extend(
                     sv_slice(registers[rs1], 0, (7) - (0) + 1), 8))) &
                 low_mask(64));
      else if (sv_slice(word, 20, (31) - (20) + 1) == UINT64_C(0x605) &&
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
  case UINT64_C(0x37): {
    {
      value = ((std::int64_t(sign_extend(
                   (field(sv_slice(word, 12, (31) - (12) + 1), 20, 12) |
                    field(UINT64_C(0), 12, 0)),
                   32))) &
               low_mask(64));
      write_rd = rd != 0;
    }
  } break;
  case UINT64_C(0x33):
  case UINT64_C(0x3b): {
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
          sv_slice(word, 0, (6) - (0) + 1) == UINT64_C(0x3b))
        value =
            sv_slice(word, 12, (14) - (12) + 1) == 4
                ? (field(UINT64_C(0), 48, 16) |
                   field(sv_slice(registers[rs1], 0, (15) - (0) + 1), 16, 0))
                : (field(UINT64_C(0), 32, 32) |
                   field(sv_slice(registers[rs1], 0, (31) - (0) + 1), 32, 0)) +
                      registers[rs2];
      else if (sv_slice(word, 0, (6) - (0) + 1) == UINT64_C(0x3b))
        value = sign_extend(value, 32);
      write_rd = rd != 0;
    }
  } break;
  case UINT64_C(0x67): {
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
  case UINT64_C(0x6f): {
    {
      value = ((reference_pc)&low_mask(64));
      write_rd = rd != 0;
      imm = ((std::int64_t(sign_extend(
                 (field(sv_slice(word, 31, 1), 1, 20) |
                  field(sv_slice(word, 12, (19) - (12) + 1), 8, 12) |
                  field(sv_slice(word, 20, 1), 1, 11) |
                  field(sv_slice(word, 21, (30) - (21) + 1), 10, 1) |
                  field(UINT64_C(0), 1, 0)),
                 21))) &
             low_mask(64));
      reference_pc = int(got.pfetched.ppc + imm);
    }
  } break;
  case UINT64_C(0x63): {
    {
      imm = ((std::int64_t(
                 sign_extend((field(sv_slice(word, 31, 1), 1, 12) |
                              field(sv_slice(word, 7, 1), 1, 11) |
                              field(sv_slice(word, 25, (30) - (25) + 1), 6, 5) |
                              field(sv_slice(word, 8, (11) - (8) + 1), 4, 1) |
                              field(UINT64_C(0), 1, 0)),
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
            (pbmt_kind != 0 && urequest.paddress >= UINT64_C(0x14000) &&
             urequest.paddress < UINT64_C(0x15000)));
      bytes = 1 << sv_slice(word, 12, (13) - (12) + 1);
      for (int b = 0; b < bytes; b++)
        sv_slice(value, b * 8, 8) =
            model_bytes[data_pa(address + ((b)&low_mask(64)))];
      if (bytes < 8 && !sv_slice(word, 14, 1) &&
          sv_slice(value, bytes * 8 - 1, 1))
        value |= UINT64_MAX << (bytes * 8);
    }
  } break;
  case UINT64_C(0x23): {
    {
      imm = ((std::int64_t(
                 sign_extend((field(sv_slice(word, 25, (31) - (25) + 1), 7, 5) |
                              field(sv_slice(word, 7, (11) - (7) + 1), 5, 0)),
                             12))) &
             low_mask(64));
      address = registers[rs1] + imm;
      CHECK(ustate == 0 ||
            (pbmt_kind != 0 && urequest.paddress >= UINT64_C(0x14000) &&
             urequest.paddress < UINT64_C(0x15000)));
      for (int b = 0; b < (1 << sv_slice(word, 12, (13) - (12) + 1)); b++)
        model_bytes[data_pa(address + ((b)&low_mask(64)))] =
            sv_slice(registers[rs2], b * 8, 8);
    }
  } break;
  case UINT64_C(0x2f): {
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
      if (word == UINT64_C(0x100000f)) {
        CHECK(!got.pwrite && !got.pdeferred);
        pauses++;
        last_pause_cycle = cycles;
      } else {
        CHECK((ustate == 0 ||
               (pbmt_kind != 0 && urequest.paddress >= UINT64_C(0x14000) &&
                urequest.paddress < UINT64_C(0x15000))) &&
              !dactive && completions.size() == 0);
        fences++;
        if (sv_slice(word, 12, (14) - (12) + 1) == 1)
          instruction_fences++;
      }
    }
  } break;
  case UINT64_C(0x73): {
    {
      CHECK(phase >= 5);
      if (phase == 34 && (word & UINT64_C(0xfe007fff)) == UINT64_C(0x16000073))
        svinval_remapped = 1;
      if (phase >= 6) {
        if (word == UINT64_C(0x30200073))
          reference_pc =
              phase == 19 ? expected_fault_pc + 4 : UINT64_C(0x400000);
        else if (sv_slice(word, 12, (14) - (12) + 1) == 2) {
          write_rd = rd != 0;
          switch (sv_slice(word, 20, (31) - (20) + 1)) {
          case UINT64_C(0x342): {
            value = ((expected_fault_cause)&low_mask(64));
          } break;
          case UINT64_C(0x343): {
            value = expected_fault_value;
          } break;
          case UINT64_C(0x341): {
            value = ((expected_fault_pc)&low_mask(64));
          } break;
          case UINT64_C(0x30a): {
            value = UINT64_C(0x4000000000000000);
          } break;
          default: {
            fail(1, "unexpected paged CSR read");
          } break;
          }
        }
      } else
        switch (got.pfetched.ppc) {
        case UINT64_C(0x304):
        case UINT64_C(0x38c):
        case UINT64_C(0x310): {
          {
          } // CSRRW x0 and WFI
        } break;
        case UINT64_C(0x380): {
          {
            value = 11;
            write_rd = 1;
          } // mcause
        } break;
        case UINT64_C(0x384): {
          {
            value = UINT64_C(0x308);
            write_rd = 1;
          } // mepc
        } break;
        case UINT64_C(0x390): {
          reference_pc = UINT64_C(0x30c); // MRET
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

int main() {
  return run_test([] {
    reset = 1;
    instruction_node_id = 2;
    data_node_id = 3;
    uncached_node_id = 4;
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
    insn(128, (field(UINT64_C(0), 12, 20) | field(UINT64_C(1), 5, 15) |
               field(UINT64_C(3), 3, 12) | field(UINT64_C(16), 5, 7) |
               field(UINT64_C(3), 7, 0))); // delayed LD
    insn(132, addi(17, 16, 1)); // hold issue, filling reserved fetch capacity
    insn(136, addi(18, 17, 1));
    insn(140, addi(19, 18, 1));
    insn(144, (field(UINT64_C(0), 7, 25) | field(UINT64_C(19), 5, 20) |
               field(UINT64_C(1), 5, 15) | field(UINT64_C(3), 3, 12) |
               field(UINT64_C(8), 5, 7) | field(UINT64_C(0x23), 7, 0))); // SD
    insn(148, (field(UINT64_C(8), 12, 20) | field(UINT64_C(1), 5, 15) |
               field(UINT64_C(3), 3, 12) | field(UINT64_C(20), 5, 7) |
               field(UINT64_C(3), 7, 0)));
    insn(184, addi(5, 0, 316));
    insn(
        188,
        (field(UINT64_C(0), 12, 20) | field(UINT64_C(5), 5, 15) |
         field(UINT64_C(0), 3, 12) | field(UINT64_C(21), 5, 7) |
         field(UINT64_C(0x67), 7,
               0))); // cold indirect target: discard erroneous younger line 192
    insn(316, addi(22, 21, 1));
    insn(320, jal(0, 4092 - 320));
    insn(4092, addi(30, 0, 77));
    // An older cold load must finish before the younger illegal instruction
    // reports its fault. Fetch cancellation happens before that delayed report.
    insn(640, addi(1, 0, 2047));
    insn(644, addi(1, 1, 65));
    insn(648, (field(UINT64_C(0), 12, 20) | field(UINT64_C(1), 5, 15) |
               field(UINT64_C(3), 3, 12) | field(UINT64_C(16), 5, 7) |
               field(UINT64_C(3), 7, 0)));
    insn(652, UINT64_C(0xffffffff));
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index)
      falling();
    reset = 0;
    launch(0, 1, 4096);
    CHECK(longest_dual >= 5 && dreads > 0 && completions_seen > 0 &&
          branch_count >= 3);
    CHECK(predicted_branches == 0);

    CHECK(wrong_path_reads > 0 && detached_refills > 0);
    phase = 1;
    launch(512, 1, 512); // accepted CHI error, not illegal-instruction decoding
    phase = 2;
    launch(515, 0, 515); // odd PC faults without issuing an aligned read
    phase = 3;
    launch(4096, 1, 4096); // unmapped restart never reaches CHI
    phase = 4;
    reference_pc = 640;
    launch(640, 2, 652, UINT64_C(0xffffffff));
    CHECK(faults == 5 && acks + reset_canceled_refills == ireads);
    // Execute a real handler through L1I: program mtvec, take ECALL, read
    // architectural trap state, advance mepc, return, then sleep after a marker.
    phase = 5;
    reference_pc = UINT64_C(0x300);
    insn(UINT64_C(0x300), addi(1, 0, UINT64_C(0x380)));
    insn(UINT64_C(0x304),
         (field(UINT64_C(0x305), 12, 20) | field(UINT64_C(1), 5, 15) |
          field(UINT64_C(1), 3, 12) | field(UINT64_C(0), 5, 7) |
          field(UINT64_C(0x73), 7, 0)));
    insn(UINT64_C(0x308), UINT64_C(0x73));
    insn(UINT64_C(0x30c), addi(12, 0, 99));
    insn(UINT64_C(0x310), UINT64_C(0x10500073));
    insn(UINT64_C(0x380),
         (field(UINT64_C(0x342), 12, 20) | field(UINT64_C(0), 5, 15) |
          field(UINT64_C(2), 3, 12) | field(UINT64_C(10), 5, 7) |
          field(UINT64_C(0x73), 7, 0)));
    insn(UINT64_C(0x384),
         (field(UINT64_C(0x341), 12, 20) | field(UINT64_C(0), 5, 15) |
          field(UINT64_C(2), 3, 12) | field(UINT64_C(11), 5, 7) |
          field(UINT64_C(0x73), 7, 0)));
    insn(UINT64_C(0x388), addi(11, 11, 4));
    insn(UINT64_C(0x38c),
         (field(UINT64_C(0x341), 12, 20) | field(UINT64_C(11), 5, 15) |
          field(UINT64_C(1), 3, 12) | field(UINT64_C(0), 5, 7) |
          field(UINT64_C(0x73), 7, 0)));
    insn(UINT64_C(0x390), UINT64_C(0x30200073));
    expected_fault_pc = UINT64_C(0x308);
    expected_fault_cause = 11;
    expected_fault_value = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(0x300)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(reference_pc == UINT64_C(0x314) && registers[10] == 11 &&
          registers[11] == UINT64_C(0x30c) && registers[12] == 99 &&
          faults == 6);
    // Real M-mode setup and MRET into translated S-mode. The same three-level
    // tables serve independent I/D TLBs through the coherent data cache.
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    for (int p = UINT64_C(0x10000); p < UINT64_C(0x20000); p++) {
      backing[p] = 0;
      model_bytes[p] = 0;
    }
    {
      std::uint64_t pte;
      pte = (UINT64_C(17) << 10) | 1;
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(0x10000) + b] = sv_slice(pte, b * 8, 8);
      pte = (UINT64_C(18) << 10) | 1;
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(0x11010) + b] = sv_slice(pte, b * 8, 8);
      pte = (UINT64_C(20) << 10) | UINT64_C(0xcb);
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(0x12000) + b] = sv_slice(pte, b * 8, 8);
      pte = (UINT64_C(21) << 10) | UINT64_C(0xc7);
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(0x12800) + b] = sv_slice(pte, b * 8, 8);
      for (int b = 0; b < 16; b++) {
        backing[UINT64_C(0x15000) + b] = ((b + 1) & low_mask(8));
        model_bytes[UINT64_C(0x15000) + b] = ((b + 1) & low_mask(8));
      }
    }
    insn(UINT64_C(0x300), addi(1, 0, UINT64_C(0x380)));
    insn(UINT64_C(0x304),
         (field(UINT64_C(0x305), 12, 20) | field(UINT64_C(1), 5, 15) |
          field(UINT64_C(1), 3, 12) | field(UINT64_C(0), 5, 7) |
          field(UINT64_C(0x73), 7, 0)));
    insn(UINT64_C(0x308), addi(1, 0, 8));
    insn(UINT64_C(0x30c),
         (field(UINT64_C(0), 6, 26) | field(UINT64_C(0x3c), 6, 20) |
          field(UINT64_C(1), 5, 15) | field(UINT64_C(1), 3, 12) |
          field(UINT64_C(1), 5, 7) | field(UINT64_C(19), 7, 0)));
    insn(UINT64_C(0x310), addi(1, 1, 16));
    insn(UINT64_C(0x314),
         (field(UINT64_C(0x180), 12, 20) | field(UINT64_C(1), 5, 15) |
          field(UINT64_C(1), 3, 12) | field(UINT64_C(0), 5, 7) |
          field(UINT64_C(0x73), 7, 0)));
    insn(UINT64_C(0x318), UINT64_C(0x12000073)); // SFENCE.VMA
    insn(UINT64_C(0x31c), addi(1, 0, 2047));
    insn(UINT64_C(0x320), addi(1, 1, 1));
    insn(UINT64_C(0x324),
         (field(UINT64_C(0x300), 12, 20) | field(UINT64_C(1), 5, 15) |
          field(UINT64_C(1), 3, 12) | field(UINT64_C(0), 5, 7) |
          field(UINT64_C(0x73), 7, 0))); // MPP=S
    insn(UINT64_C(0x328),
         (field(UINT64_C(0x400), 20, 12) | field(UINT64_C(1), 5, 7) |
          field(UINT64_C(0x37), 7, 0)));
    insn(UINT64_C(0x32c),
         (field(UINT64_C(0x341), 12, 20) | field(UINT64_C(1), 5, 15) |
          field(UINT64_C(1), 3, 12) | field(UINT64_C(0), 5, 7) |
          field(UINT64_C(0x73), 7, 0)));
    insn(UINT64_C(0x330), UINT64_C(0x30200073));
    insn(UINT64_C(0x14000),
         (field(UINT64_C(0x500), 20, 12) | field(UINT64_C(1), 5, 7) |
          field(UINT64_C(0x37), 7, 0)));
    insn(UINT64_C(0x14004),
         (field(UINT64_C(0), 12, 20) | field(UINT64_C(1), 5, 15) |
          field(UINT64_C(3), 3, 12) | field(UINT64_C(5), 5, 7) |
          field(UINT64_C(3), 7, 0)));
    insn(UINT64_C(0x14008), addi(6, 5, 1));
    insn(UINT64_C(0x1400c),
         (field(UINT64_C(0), 7, 25) | field(UINT64_C(6), 5, 20) |
          field(UINT64_C(1), 5, 15) | field(UINT64_C(3), 3, 12) |
          field(UINT64_C(8), 5, 7) | field(UINT64_C(0x23), 7, 0)));
    insn(UINT64_C(0x14010),
         (field(UINT64_C(8), 12, 20) | field(UINT64_C(1), 5, 15) |
          field(UINT64_C(3), 3, 12) | field(UINT64_C(7), 5, 7) |
          field(UINT64_C(3), 7, 0)));
    insn(UINT64_C(0x14014), UINT64_C(0x12000073));
    insn(UINT64_C(0x14018),
         (field(UINT64_C(8), 12, 20) | field(UINT64_C(1), 5, 15) |
          field(UINT64_C(3), 3, 12) | field(UINT64_C(8), 5, 7) |
          field(UINT64_C(3), 7, 0)));
    insn(UINT64_C(0x1401c),
         (field(UINT64_C(0x501), 20, 12) | field(UINT64_C(1), 5, 7) |
          field(UINT64_C(0x37), 7, 0)));
    insn(UINT64_C(0x14020), addi(12, 0, 77));
    insn(UINT64_C(0x14024),
         (field(UINT64_C(0), 12, 20) | field(UINT64_C(1), 5, 15) |
          field(UINT64_C(3), 3, 12) | field(UINT64_C(9), 5, 7) |
          field(UINT64_C(3), 7, 0)));
    insn(UINT64_C(0x380),
         (field(UINT64_C(0x342), 12, 20) | field(UINT64_C(0), 5, 15) |
          field(UINT64_C(2), 3, 12) | field(UINT64_C(10), 5, 7) |
          field(UINT64_C(0x73), 7, 0)));
    insn(UINT64_C(0x384),
         (field(UINT64_C(0x343), 12, 20) | field(UINT64_C(0), 5, 15) |
          field(UINT64_C(2), 3, 12) | field(UINT64_C(11), 5, 7) |
          field(UINT64_C(0x73), 7, 0)));
    insn(UINT64_C(0x388),
         (field(UINT64_C(0x341), 12, 20) | field(UINT64_C(0), 5, 15) |
          field(UINT64_C(2), 3, 12) | field(UINT64_C(13), 5, 7) |
          field(UINT64_C(0x73), 7, 0)));
    insn(UINT64_C(0x38c), UINT64_C(0x10500073));
    phase = 6;
    reference_pc = UINT64_C(0x300);
    expected_fault_pc = UINT64_C(0x400024);
    expected_fault_cause = 13;
    expected_fault_value = UINT64_C(0x501000);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(0x300)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(registers[5] == UINT64_C(0x807060504030201) &&
          registers[7] == registers[6] && registers[8] == registers[6] &&
          registers[12] == 77 && registers[10] == 13 &&
          registers[11] == UINT64_C(0x501000) &&
          registers[13] == UINT64_C(0x400024) && faults == 7);
    for (int scenario = 7; scenario <= 8; scenario++) {
      falling();
      reset = 1;
      iactive = 0;
      dactive = 0;
      wactive = 0;
      for (int r = 0; r < 32; r++)
        registers[r] = 0;
      for (int b = 0; b < 16; b++) {
        backing[UINT64_C(0x15000) + b] = ((b + 1) & low_mask(8));
        model_bytes[UINT64_C(0x15000) + b] = ((b + 1) & low_mask(8));
      }
      if (scenario == 7) {
        insn(UINT64_C(0x14020),
             (field(UINT64_C(0), 7, 25) | field(UINT64_C(6), 5, 20) |
              field(UINT64_C(1), 5, 15) | field(UINT64_C(3), 3, 12) |
              field(UINT64_C(0), 5, 7) | field(UINT64_C(0x23), 7, 0)));
        insn(UINT64_C(0x14024),
             addi(12, 0, 88)); // younger result must not retire
        expected_fault_pc = UINT64_C(0x400020);
        expected_fault_cause = 15;
        expected_fault_value = UINT64_C(0x501000);
      } else {
        insn(UINT64_C(0x14020), addi(12, 0, 77));
        insn(UINT64_C(0x14024), jal(0, UINT64_C(0x1000) - UINT64_C(0x24)));
        expected_fault_pc = UINT64_C(0x401000);
        expected_fault_cause = 12;
        expected_fault_value = UINT64_C(0x401000);
      }
      phase = scenario;
      reference_pc = UINT64_C(0x300);
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      reset = 0;
      falling();
      start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(0x300)};
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
    for (int b = UINT64_C(0x2000); b < UINT64_C(0x2400); b++) {
      backing[b] = ((b)&low_mask(8));
      model_bytes[b] = ((b)&low_mask(8));
    }
    insn(UINT64_C(0x400),
         (field(UINT64_C(2), 20, 12) | field(UINT64_C(1), 5, 7) |
          field(UINT64_C(0x37), 7, 0)));
    insn(UINT64_C(0x404), addi(2, 0, -2));
    insn(UINT64_C(0x408), addi(3, 0, 2047));
    insn(UINT64_C(0x40c), addi(3, 3, 1));
    insn(UINT64_C(0x410), load(4, 3, 0, 3)); // older cold cached read
    for (int width = 0; width < 4; width++) {
      int pc;
      pc = UINT64_C(0x414) + width * 16;
      insn(pc, store(2, 1, 8 + (1 << width), width));
      insn(pc + 4, load(5 + width, 1, 8 + (1 << width), width));
      insn(pc + 8, load(9 + width, 3, 0, 3)); // younger warm cache hit
      insn(pc + 12, UINT64_C(0xff0000f));
    }
    insn(UINT64_C(0x454), load(13, 1, UINT64_C(0x203), 4));
    insn(UINT64_C(0x458), store(2, 1, UINT64_C(0x300), 2));
    insn(UINT64_C(0x45c), load(14, 1, UINT64_C(0x300), 6));
    insn(UINT64_C(0x460), jal(0, 8));
    insn(UINT64_C(0x464), store(2, 1, 0, 3));
    insn(UINT64_C(0x468), UINT64_C(0x10500073));
    phase = 9;
    reference_pc = UINT64_C(0x400);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(0x400)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(ureads == 6 && uwrites == 5 && fences == 4 && registers[13] == 3 &&
          registers[14] == UINT64_C(0xfffffffe));
    for (int r = 5; r <= 8; r++)
      CHECK(registers[r] == UINT64_C(0xfffffffffffffffe));
    // Permission and missing-mapping faults have no device-side effects.
    for (int scenario = 10; scenario <= 11; scenario++) {
      falling();
      reset = 1;
      iactive = 0;
      dactive = 0;
      wactive = 0;
      for (int r = 0; r < 32; r++)
        registers[r] = 0;
      insn(UINT64_C(0x400), addi(1, 0, UINT64_C(0x380)));
      insn(UINT64_C(0x404),
           (field(UINT64_C(0x305), 12, 20) | field(UINT64_C(1), 5, 15) |
            field(UINT64_C(1), 3, 12) | field(UINT64_C(0), 5, 7) |
            field(UINT64_C(0x73), 7, 0)));
      insn(UINT64_C(0x408),
           (field(UINT64_C(2), 20, 12) | field(UINT64_C(1), 5, 7) |
            field(UINT64_C(0x37), 7, 0)));
      insn(UINT64_C(0x40c), scenario == 10 ? store(0, 1, UINT64_C(0x100), 3)
                                           : load(5, 1, UINT64_C(0x204), 4));
      expected_fault_pc = UINT64_C(0x40c);
      expected_fault_cause = scenario == 10 ? 7 : 5;
      expected_fault_value =
          scenario == 10 ? UINT64_C(0x2100) : UINT64_C(0x2204);
      phase = scenario;
      reference_pc = UINT64_C(0x400);
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      reset = 0;
      falling();
      start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(0x400)};
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
    insn(UINT64_C(0x400), addi(10, 0, 0));
    insn(UINT64_C(0x404), jal(0, UINT64_C(0x700) - UINT64_C(0x404)));
    insn(UINT64_C(0x700), addi(20, 0, 1));
    insn(UINT64_C(0x704), bne(10, 0, 12));
    insn(UINT64_C(0x708), jal(0, 1088 - 1800));
    insn(UINT64_C(0x710), UINT64_C(0x10500073));
    insn(UINT64_C(0x440),
         (field(UINT64_C(2), 20, 12) | field(UINT64_C(1), 5, 7) |
          field(UINT64_C(0x37), 7, 0)));
    insn(UINT64_C(0x444), addi(2, 0, 1));
    insn(UINT64_C(0x448), store(2, 1, UINT64_C(0xf0), 3));
    insn(UINT64_C(0x44c), addi(10, 0, 1));
    insn(UINT64_C(0x450), UINT64_C(0x100f));
    insn(UINT64_C(0x454), jal(0, UINT64_C(0x700) - UINT64_C(0x454)));
    phase = 12;
    reference_pc = UINT64_C(0x400);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(0x400)};
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
      pte = (UINT64_C(2) << 10) | UINT64_C(0xc7);
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(0x12800) + b] = sv_slice(pte, b * 8, 8);
    }
    insn(UINT64_C(0x14000),
         (field(UINT64_C(0x500), 20, 12) | field(UINT64_C(1), 5, 7) |
          field(UINT64_C(0x37), 7, 0)));
    insn(UINT64_C(0x14004), addi(6, 0, -2));
    insn(UINT64_C(0x14008), store(6, 1, 8, 0));
    insn(UINT64_C(0x1400c), load(7, 1, 8, 0));
    insn(UINT64_C(0x14010), store(6, 1, UINT64_C(0x100), 0));
    insn(UINT64_C(0x14014), addi(12, 0, 88));
    expected_fault_pc = UINT64_C(0x400010);
    expected_fault_cause = 7;
    expected_fault_value = UINT64_C(0x500100);
    phase = 13;
    reference_pc = UINT64_C(0x300);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(0x300)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(registers[7] == UINT64_C(0xfffffffffffffffe) &&
          registers[11] == UINT64_C(0x500100) && registers[12] == 0 &&
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
    insn(UINT64_C(0x600), addi(31, 0, 20));
    parcel(UINT64_C(0x604), c_imm(2, 8, 7));
    parcel(UINT64_C(0x606), c_imm(2, 9, 9));
    for (int p = UINT64_C(0x608); p < UINT64_C(0x638); p += 2)
      parcel(p, c_imm(2, 10 + ((p / 2) & 1), (p / 2) & 31));
    parcel(UINT64_C(0x638), c_imm(0, 31, -1));
    insn(UINT64_C(0x63a), addi(0, 0, 0));
    insn(
        UINT64_C(0x63e),
        bne(31, 0,
            1544 -
                1598)); // warm predicted branch crosses the eight-byte boundary
    insn(UINT64_C(0x642), addi(20, 20, 1));
    insn(UINT64_C(0x646), addi(21, 20, 2));
    insn(UINT64_C(0x64a), addi(8, 0, 2047));
    insn(UINT64_C(0x64e), addi(8, 8, 1));
    parcel(UINT64_C(0x652), UINT64_C(0x6004));
    parcel(UINT64_C(0x654), c_imm(0, 9, 1));
    parcel(UINT64_C(0x656), UINT64_C(0xe404));
    parcel(UINT64_C(0x658), UINT64_C(0x6408));
    parcel(UINT64_C(0x65a), UINT64_C(0x85aa));
    insn(UINT64_C(0x65c), addi(5, 0, UINT64_C(0x682)));
    parcel(UINT64_C(0x660), UINT64_C(0x9282));
    insn(UINT64_C(0x662), addi(12, 1, 0));
    insn(UINT64_C(0x666), addi(14, 0, 14));
    insn(UINT64_C(0x66a), UINT64_C(0x10500073));
    parcel(UINT64_C(0x682), c_imm(2, 13, 13));
    parcel(UINT64_C(0x684), UINT64_C(0xc011));
    parcel(UINT64_C(0x686), UINT64_C(0xe011));
    parcel(UINT64_C(0x688), UINT64_C(0x8002));
    parcel(UINT64_C(0x68a), UINT64_C(0xa011));
    parcel(UINT64_C(0x68c), UINT64_C(0x8002));
    parcel(UINT64_C(0x68e), UINT64_C(0x8082));
    phase = 14;
    reference_pc = UINT64_C(0x600);
    dual_run = 0;
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(0x600)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(compressed_retired >= 80 && compressed_dual_run >= 5 &&
          straddled_retired >= 3 && registers[20] == 1 && registers[21] == 3 &&
          registers[9] == registers[10] && registers[10] == registers[11] &&
          registers[12] == UINT64_C(0x662) && registers[13] == 13 &&
          reference_pc == UINT64_C(0x66e));
    // An illegal compressed encoding retains its 16-bit mtval after an older
    // accepted compressed load drains, without executing its canonical zero.
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    insn(UINT64_C(0x400), addi(1, 0, UINT64_C(0x380)));
    insn(UINT64_C(0x404),
         (field(UINT64_C(0x305), 12, 20) | field(UINT64_C(1), 5, 15) |
          field(UINT64_C(1), 3, 12) | field(UINT64_C(0), 5, 7) |
          field(UINT64_C(0x73), 7, 0)));
    insn(UINT64_C(0x408), addi(8, 0, 2047));
    insn(UINT64_C(0x40c), addi(8, 8, 1));
    parcel(UINT64_C(0x410), UINT64_C(0x6004));
    parcel(UINT64_C(0x412), UINT64_C(0x8002));
    parcel(UINT64_C(0x414), c_imm(2, 12, 9));
    expected_fault_pc = UINT64_C(0x412);
    expected_fault_cause = 2;
    expected_fault_value = UINT64_C(0x8002);
    phase = 15;
    reference_pc = UINT64_C(0x400);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(0x400)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(registers[11] == UINT64_C(0x8002) &&
          registers[13] == UINT64_C(0x412) && registers[12] == 0);
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
        pte = scenario == 17 ? (UINT64_C(22) << 10) | UINT64_C(0xcb) : 0;
        crossing = addi(12, 0, 77);
        for (int b = 0; b < 8; b++)
          backing[UINT64_C(0x12008) + b] = sv_slice(pte, b * 8, 8);
        insn(UINT64_C(0x14000), jal(0, UINT64_C(0xffe)));
        parcel(UINT64_C(0x14ffe), sv_slice(crossing, 0, (15) - (0) + 1));
        parcel(UINT64_C(0x16000), sv_slice(crossing, 16, (31) - (16) + 1));
        parcel(UINT64_C(0x16002), c_imm(2, 14, 14));
        insn(UINT64_C(0x16004), UINT64_C(0x10500073));
      }
      expected_fault_pc = UINT64_C(0x400ffe);
      expected_fault_cause = 12;
      expected_fault_value = UINT64_C(0x401000);
      phase = scenario;
      reference_pc = UINT64_C(0x300);
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      reset = 0;
      falling();
      start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(0x300)};
      falling();
      start_in = {};
      until([&] { return sleeping; });
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      if (scenario == 16)
        CHECK(registers[11] == UINT64_C(0x401000) &&
              registers[13] == UINT64_C(0x400ffe) && registers[12] == 0);
      else
        CHECK(registers[12] == 77 && registers[14] == 14 &&
              reference_pc == UINT64_C(0x401008));
    }
    // The same suffix rule applies to a physical-map access fault.
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    insn(UINT64_C(0x408), jal(0, UINT64_C(0xffe) - UINT64_C(0x408)));
    parcel(UINT64_C(0xffe), UINT64_C(0x613));
    expected_fault_pc = UINT64_C(0xffe);
    expected_fault_cause = 1;
    expected_fault_value = UINT64_C(0x1000);
    phase = 18;
    reference_pc = UINT64_C(0x400);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(0x400)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(registers[11] == UINT64_C(0x1000) &&
          registers[13] == UINT64_C(0xffe) && registers[12] == 0);
    // C.EBREAK traps at a halfword PC; the handler deliberately skips the
    // following C.LI and MRET must preserve bit one in its halfword target.
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    parcel(UINT64_C(0x408), UINT64_C(1));
    parcel(UINT64_C(0x40a), UINT64_C(0x9002));
    parcel(UINT64_C(0x40c), c_imm(2, 12, 12));
    insn(UINT64_C(0x40e), UINT64_C(0x10500073));
    insn(UINT64_C(0x38c),
         addi(13, 13, 4)); // Return to 0x40e, whose bit one must survive mepc.
    insn(UINT64_C(0x390),
         (field(UINT64_C(0x341), 12, 20) | field(UINT64_C(13), 5, 15) |
          field(UINT64_C(1), 3, 12) | field(UINT64_C(0), 5, 7) |
          field(UINT64_C(0x73), 7, 0)));
    insn(UINT64_C(0x394), UINT64_C(0x30200073));
    expected_fault_pc = UINT64_C(0x40a);
    expected_fault_cause = 3;
    expected_fault_value = 0;
    phase = 19;
    reference_pc = UINT64_C(0x400);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(0x400)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(registers[10] == 3 && registers[11] == 0 &&
          registers[13] == UINT64_C(0x40e) && registers[12] == 0 &&
          reference_pc == UINT64_C(0x412));
    // Fetch mixed compressed sources and full-width M operations through the
    // production caches; WFI waits for every accepted deferred write.
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    parcel(UINT64_C(0x600), c_imm(2, 8, -17));
    parcel(UINT64_C(0x602), c_imm(2, 9, 7));
    insn(UINT64_C(0x604),
         (field(UINT64_C(1), 7, 25) | field(UINT64_C(9), 5, 20) |
          field(UINT64_C(8), 5, 15) | field(UINT64_C(0), 3, 12) |
          field(UINT64_C(10), 5, 7) | field(UINT64_C(0x33), 7, 0)));
    parcel(UINT64_C(0x608), c_imm(2, 11, 3));
    insn(UINT64_C(0x60a),
         (field(UINT64_C(1), 7, 25) | field(UINT64_C(9), 5, 20) |
          field(UINT64_C(10), 5, 15) | field(UINT64_C(4), 3, 12) |
          field(UINT64_C(12), 5, 7) | field(UINT64_C(0x33), 7, 0)));
    parcel(UINT64_C(0x60e), UINT64_C(0x86b2));
    insn(UINT64_C(0x610),
         (field(UINT64_C(1), 7, 25) | field(UINT64_C(9), 5, 20) |
          field(UINT64_C(8), 5, 15) | field(UINT64_C(0), 3, 12) |
          field(UINT64_C(14), 5, 7) | field(UINT64_C(0x3b), 7, 0)));
    insn(UINT64_C(0x614),
         (field(UINT64_C(1), 7, 25) | field(UINT64_C(9), 5, 20) |
          field(UINT64_C(10), 5, 15) | field(UINT64_C(5), 3, 12) |
          field(UINT64_C(15), 5, 7) | field(UINT64_C(0x33), 7, 0)));
    insn(UINT64_C(0x618), UINT64_C(0x10500073));
    phase = 20;
    reference_pc = UINT64_C(0x600);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(0x600)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(registers[10] == -UINT64_C(0x77) && registers[12] == -UINT64_C(17) &&
          registers[13] == -UINT64_C(17) && registers[14] == -UINT64_C(0x77) &&
          completions.size() == 0 && reference_pc == UINT64_C(0x61c));
    // B results feed the production LSU and compressed consumers without an
    // extra execution stage; retain all bits through store/load forwarding.
    falling();
    reset = 1;
    iactive = 0;
    dactive = 0;
    wactive = 0;
    for (int r = 0; r < 32; r++)
      registers[r] = 0;
    parcel(UINT64_C(0x700), c_imm(2, 8, 17));
    parcel(UINT64_C(0x702), c_imm(2, 9, 3));
    insn(UINT64_C(0x704),
         (field(UINT64_C(16), 7, 25) | field(UINT64_C(9), 5, 20) |
          field(UINT64_C(8), 5, 15) | field(UINT64_C(4), 3, 12) |
          field(UINT64_C(10), 5, 7) | field(UINT64_C(0x33), 7, 0))); // SH2ADD
    insn(UINT64_C(0x708),
         (field(UINT64_C(10), 6, 26) | field(UINT64_C(0x3f), 6, 20) |
          field(UINT64_C(10), 5, 15) | field(UINT64_C(1), 3, 12) |
          field(UINT64_C(11), 5, 7) | field(UINT64_C(19), 7, 0))); // BSETI
    insn(UINT64_C(0x70c),
         (field(UINT64_C(0x6b8), 12, 20) | field(UINT64_C(11), 5, 15) |
          field(UINT64_C(5), 3, 12) | field(UINT64_C(12), 5, 7) |
          field(UINT64_C(19), 7, 0))); // REV8
    insn(UINT64_C(0x710), addi(1, 0, UINT64_C(0x400)));
    insn(UINT64_C(0x714), store(12, 1, 0, 3));
    insn(UINT64_C(0x718), load(13, 1, 0, 3));
    parcel(UINT64_C(0x71c), UINT64_C(0x8736));
    insn(UINT64_C(0x71e), UINT64_C(0x10500073)); // C.MV x14,x13; WFI
    phase = 21;
    reference_pc = UINT64_C(0x700);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(0x700)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(registers[10] == 71 &&
          registers[11] == UINT64_C(0x8000000000000047) &&
          registers[12] == UINT64_C(0x4700000000000080) &&
          registers[13] == registers[12] && registers[14] == registers[12] &&
          completions.size() == 0 && reference_pc == UINT64_C(0x722));
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
      backing[UINT64_C(0x340) + b] = UINT64_C(0xff);
      model_bytes[UINT64_C(0x340) + b] = UINT64_C(0xff);
    }
    insn(UINT64_C(0x700), addi(1, 0, UINT64_C(0x340)));
    insn(UINT64_C(0x704), addi(2, 0, 7));
    insn(UINT64_C(0x708), atomic_insn(2, 2, 3, 1));
    insn(UINT64_C(0x70c), addi(10, 0, 10));
    insn(UINT64_C(0x710), atomic_insn(0, 2, 4, 1, 2));
    insn(UINT64_C(0x714), atomic_insn(3, 2, 5, 1, 2));
    insn(UINT64_C(0x718), atomic_insn(2, 3, 6, 1));
    insn(UINT64_C(0x71c), atomic_insn(3, 3, 7, 1, 2));
    insn(UINT64_C(0x720), addi(8, 7, 1));
    insn(UINT64_C(0x724), atomic_insn(1, 2, 0, 1, 0));
    insn(UINT64_C(0x728), load(9, 1, 0, 3));
    insn(UINT64_C(0x72c), atomic_insn(2, 3, 0, 1));
    insn(UINT64_C(0x730), jal(0, 12));
    insn(UINT64_C(0x734), atomic_insn(3, 3, 0, 1, 2));
    insn(UINT64_C(0x738), addi(11, 0, 99));
    insn(UINT64_C(0x73c), load(12, 1, 0, 3));
    insn(UINT64_C(0x740), UINT64_C(0x10500073));
    phase = 22;
    reference_pc = UINT64_C(0x700);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    falling();
    start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(0x700)};
    falling();
    start_in = {};
    until([&] { return sleeping; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(registers[3] == UINT64_MAX && registers[4] == UINT64_MAX &&
          registers[5] == 1 && registers[6] == UINT64_C(0xffffffff00000006) &&
          registers[7] == 0 && registers[8] == 1 && registers[9] == 0 &&
          registers[10] == 10 && registers[11] == 0 && registers[12] == 0 &&
          completions.size() == 0 && reference_pc == UINT64_C(0x744));
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
      for (int b = UINT64_C(0x10000); b < UINT64_C(0x20000); b++) {
        backing[b] = 0;
        model_bytes[b] = 0;
      }
      pte = (UINT64_C(17) << 10) | 1;
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(0x10000) + b] = sv_slice(pte, b * 8, 8);
      pte = (UINT64_C(18) << 10) | 1;
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(0x11010) + b] = sv_slice(pte, b * 8, 8);
      pte = (UINT64_C(20) << 10) | UINT64_C(0xcb);
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(0x12000) + b] = sv_slice(pte, b * 8, 8);
      pte = (UINT64_C(21) << 10) | UINT64_C(0xc7);
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(0x12800) + b] = sv_slice(pte, b * 8, 8);
      pte = scenario == 23 ? (UINT64_C(23) << 10) | UINT64_C(0xc7) : 0;
      for (int b = 0; b < 8; b++)
        backing[UINT64_C(0x12808) + b] = sv_slice(pte, b * 8, 8);
      for (int b = 0; b < 16; b++) {
        backing[UINT64_C(0x15ff0) + b] = ((UINT64_C(0x80) + b) & low_mask(8));
        model_bytes[UINT64_C(0x15ff0) + b] = backing[UINT64_C(0x15ff0) + b];
        backing[UINT64_C(0x17000) + b] = ((UINT64_C(0x90) + b) & low_mask(8));
        model_bytes[UINT64_C(0x17000) + b] = backing[UINT64_C(0x17000) + b];
      }
      insn(UINT64_C(0x300), addi(1, 0, UINT64_C(0x380)));
      insn(UINT64_C(0x304),
           (field(UINT64_C(0x305), 12, 20) | field(UINT64_C(1), 5, 15) |
            field(UINT64_C(1), 3, 12) | field(UINT64_C(0), 5, 7) |
            field(UINT64_C(0x73), 7, 0)));
      insn(UINT64_C(0x308), addi(1, 0, 8));
      insn(UINT64_C(0x30c),
           (field(UINT64_C(0), 6, 26) | field(UINT64_C(0x3c), 6, 20) |
            field(UINT64_C(1), 5, 15) | field(UINT64_C(1), 3, 12) |
            field(UINT64_C(1), 5, 7) | field(UINT64_C(19), 7, 0)));
      insn(UINT64_C(0x310), addi(1, 1, 16));
      insn(UINT64_C(0x314),
           (field(UINT64_C(0x180), 12, 20) | field(UINT64_C(1), 5, 15) |
            field(UINT64_C(1), 3, 12) | field(UINT64_C(0), 5, 7) |
            field(UINT64_C(0x73), 7, 0)));
      insn(UINT64_C(0x318), UINT64_C(0x12000073));
      insn(UINT64_C(0x31c), addi(1, 0, 2047));
      insn(UINT64_C(0x320), addi(1, 1, 1));
      insn(UINT64_C(0x324),
           (field(UINT64_C(0x300), 12, 20) | field(UINT64_C(1), 5, 15) |
            field(UINT64_C(1), 3, 12) | field(UINT64_C(0), 5, 7) |
            field(UINT64_C(0x73), 7, 0)));
      insn(UINT64_C(0x328),
           (field(UINT64_C(0x400), 20, 12) | field(UINT64_C(1), 5, 7) |
            field(UINT64_C(0x37), 7, 0)));
      insn(UINT64_C(0x32c),
           (field(UINT64_C(0x341), 12, 20) | field(UINT64_C(1), 5, 15) |
            field(UINT64_C(1), 3, 12) | field(UINT64_C(0), 5, 7) |
            field(UINT64_C(0x73), 7, 0)));
      insn(UINT64_C(0x330), UINT64_C(0x30200073));
      insn(UINT64_C(0x14000),
           (field(UINT64_C(0x501), 20, 12) | field(UINT64_C(1), 5, 7) |
            field(UINT64_C(0x37), 7, 0)));
      insn(UINT64_C(0x14004), addi(1, 1, -3));
      insn(UINT64_C(0x14008), addi(2, 0, UINT64_C(0x123)));
      insn(UINT64_C(0x1400c),
           scenario == 25 ? store(2, 1, 0, 3) : load(3, 1, 0, 3));
      if (scenario == 23) {
        insn(UINT64_C(0x14010), load(4, 1, 0, 3));
        insn(UINT64_C(0x14014), addi(5, 4, 1));
        insn(UINT64_C(0x14018), store(2, 1, 0, 3));
        insn(UINT64_C(0x1401c), load(6, 1, 0, 3));
        insn(UINT64_C(0x14020), load(0, 1, 0, 3));
        insn(UINT64_C(0x14024), UINT64_C(0x10500073));
      } else
        insn(UINT64_C(0x14010), addi(4, 0, 99));
      insn(UINT64_C(0x380),
           (field(UINT64_C(0x342), 12, 20) | field(UINT64_C(0), 5, 15) |
            field(UINT64_C(2), 3, 12) | field(UINT64_C(10), 5, 7) |
            field(UINT64_C(0x73), 7, 0)));
      insn(UINT64_C(0x384),
           (field(UINT64_C(0x343), 12, 20) | field(UINT64_C(0), 5, 15) |
            field(UINT64_C(2), 3, 12) | field(UINT64_C(11), 5, 7) |
            field(UINT64_C(0x73), 7, 0)));
      insn(UINT64_C(0x388),
           (field(UINT64_C(0x341), 12, 20) | field(UINT64_C(0), 5, 15) |
            field(UINT64_C(2), 3, 12) | field(UINT64_C(13), 5, 7) |
            field(UINT64_C(0x73), 7, 0)));
      insn(UINT64_C(0x38c),
           (field(UINT64_C(22), 20, 12) | field(UINT64_C(1), 5, 7) |
            field(UINT64_C(0x37), 7, 0)));
      insn(UINT64_C(0x390), addi(1, 1, -8));
      insn(UINT64_C(0x394), load(14, 1, 0, 3));
      insn(UINT64_C(0x398), UINT64_C(0x10500073));
      phase = scenario;
      reference_pc = UINT64_C(0x300);
      expected_fault_pc = UINT64_C(0x40000c);
      expected_fault_cause = scenario == 25 ? 15 : 13;
      expected_fault_value = UINT64_C(0x501000);
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      reset = 0;
      falling();
      start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(0x300)};
      falling();
      start_in = {};
      until([&] { return sleeping; });
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      if (scenario == 23)
        CHECK(registers[3] == UINT64_C(0x94939291908f8e8d) &&
              registers[4] == registers[3] &&
              registers[5] == registers[3] + 1 &&
              registers[6] == UINT64_C(0x123) &&
              reference_pc == UINT64_C(0x400028));
      else {
        prefix = scenario == 25 ? UINT64_C(0x1238c8b8a8988)
                                : UINT64_C(0x8f8e8d8c8b8a8988);
        CHECK(registers[3] == 0 && registers[4] == 0 &&
              registers[10] == ((expected_fault_cause)&low_mask(64)) &&
              registers[11] == UINT64_C(0x501000) &&
              registers[13] == UINT64_C(0x40000c) && registers[14] == prefix);
      }
      CHECK(completions.size() == 0);
    }
    // Selected-subset legality must preserve raw 16-bit trap values and drain
    // an older accepted load. The C-only variant rejects these optional forms;
    // the extended variant rejects adjacent reserved encodings instead.
    for (int scenario = 0; scenario < 2; scenario++) {
      std::uint16_t invalid;
      invalid = scenario == 0 ? UINT64_C(0x9c75)
                              : UINT64_C(0x6081); // C.NOT / C.MOP.1 disabled
      falling();
      reset = 1;
      iactive = 0;
      dactive = 0;
      wactive = 0;
      for (int r = 0; r < 32; r++)
        registers[r] = 0;
      for (int b = 0; b < 8; b++) {
        backing[UINT64_C(0x800) + b] = ((UINT64_C(0xa0) + b) & low_mask(8));
        model_bytes[UINT64_C(0x800) + b] = backing[UINT64_C(0x800) + b];
      }
      insn(UINT64_C(0x380),
           (field(UINT64_C(0x342), 12, 20) | field(UINT64_C(0), 5, 15) |
            field(UINT64_C(2), 3, 12) | field(UINT64_C(10), 5, 7) |
            field(UINT64_C(0x73), 7, 0)));
      insn(UINT64_C(0x384),
           (field(UINT64_C(0x343), 12, 20) | field(UINT64_C(0), 5, 15) |
            field(UINT64_C(2), 3, 12) | field(UINT64_C(11), 5, 7) |
            field(UINT64_C(0x73), 7, 0)));
      insn(UINT64_C(0x388),
           (field(UINT64_C(0x341), 12, 20) | field(UINT64_C(0), 5, 15) |
            field(UINT64_C(2), 3, 12) | field(UINT64_C(13), 5, 7) |
            field(UINT64_C(0x73), 7, 0)));
      insn(UINT64_C(0x38c), UINT64_C(0x10500073));
      insn(UINT64_C(0x400), addi(1, 0, UINT64_C(0x380)));
      insn(UINT64_C(0x404),
           (field(UINT64_C(0x305), 12, 20) | field(UINT64_C(1), 5, 15) |
            field(UINT64_C(1), 3, 12) | field(UINT64_C(0), 5, 7) |
            field(UINT64_C(0x73), 7, 0)));
      insn(UINT64_C(0x408), addi(8, 0, 2047));
      insn(UINT64_C(0x40c), addi(8, 8, 1));
      parcel(UINT64_C(0x410), UINT64_C(0x6004));
      parcel(UINT64_C(0x412), invalid);
      parcel(UINT64_C(0x414), c_imm(2, 12, 9));
      expected_fault_pc = UINT64_C(0x412);
      expected_fault_cause = 2;
      expected_fault_value = ((invalid)&low_mask(64));
      phase = 27;
      reference_pc = UINT64_C(0x400);
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      reset = 0;
      falling();
      start_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(0x400)};
      falling();
      start_in = {};
      until([&] { return sleeping; });
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      CHECK(registers[9] == UINT64_C(0xa7a6a5a4a3a2a1a0) &&
            registers[11] == ((invalid)&low_mask(64)) &&
            registers[13] == UINT64_C(0x412) && registers[12] == 0);
    }
  });
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
        sv_slice(uncached_chi_in.pdat.presponse.pbits.pdata.words[b / 4],
                 (b % 4) * 8, 8) = backing[(int(urequest.paddress) & ~15) + b];
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
        sv_slice(instruction_chi_in.pdat.presponse.pbits.pdata.words[b / 4],
                 (b % 4) * 8, 8) =
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
        sv_slice(data_chi_in.presponse_udata.pbits.pdata.words[b / 4],
                 (b % 4) * 8, 8) =
            backing[int(drequest.paddress) + 16 * dpacket + b];
    }
  }
}

void observe() {
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
                       (6) - (0) + 1) == UINT64_C(0x63))
            predicted_conditional++;
          if (phase == 14 &&
              retired(lane).pbits.pfetched.ppc == UINT64_C(0x63e))
            predicted_straddles++;
        }
        retire(retired(lane).pbits, lane);
      }
    if (completed_out.pvalid) {
      retirement_t expected;
      int index;
      index = -1;
      CHECK(completions.size() > 0);
      for (std::size_t i = 0; i < completions.size(); ++i)
        if (completions[i].pfetched.ppc == completed_out.pbits.pfetched.ppc)
          index = i;
      CHECK(index >= 0);
      expected = completions[index];
      completions.erase(completions.begin() + index);
      CHECK(same_instruction(completed_out.pbits.pfetched, expected.pfetched) &&
            completed_out.pbits.pwrite == expected.pwrite &&
            completed_out.pbits.prd == expected.prd);
      if (expected.pwrite) {
        CHECK(completed_out.pbits.pdata == expected.pdata);
      }
      completions_seen++;
    }
    if (redirect_out.pvalid) {
      switch (redirect_out.pbits.presolution.pdisposition) {
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
          CHECK(redirect_out.pbits.ppc == ((expected_fault_pc)&low_mask(64)) &&
                redirect_out.pbits.presolution.pcause ==
                    ((expected_fault_cause)&low_mask(64)) &&
                redirect_out.pbits.presolution.pvalue == expected_fault_value);
          CHECK(completions.size() == 0);
          CHECK(redirect_out.pbits.ptarget ==
                    (phase >= 5 ? UINT64_C(0x380) : 0) &&
                !halted);
          if (phase == 0)
            CHECK(reference_pc == 4096 && registers[30] == 77);
          if (phase == 4)
            CHECK(reference_pc == 652);
          if (phase >= 5)
            reference_pc = int(redirect_out.pbits.ptarget);
          if (phase == 25)
            for (int b = 0; b < 3; b++)
              model_bytes[UINT64_C(0x15ffd) + b] =
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
          instruction_chi_out.preq.pbits.paddress == UINT64_C(0xc00)) {
        CHECK(registers[2] > 0);
        instruction_prefetch_reads++;
      }
      CHECK(instruction_chi_out.preq.pbits.popcode == UINT64_C(3) &&
            (instruction_chi_out.preq.pbits.paddress < 4096 ||
             (instruction_chi_out.preq.pbits.paddress >= UINT64_C(0x10000) &&
              instruction_chi_out.preq.pbits.paddress < UINT64_C(0x20000))) &&
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
            (pbmt_kind != 0 && urequest.paddress >= UINT64_C(0x14000) &&
             urequest.paddress < UINT64_C(0x15000)));
      CHECK(data_chi_out.prequests.pbits.paddress < 4096 ||
            (data_chi_out.prequests.pbits.paddress >= UINT64_C(0x10000) &&
             data_chi_out.prequests.pbits.paddress < UINT64_C(0x20000)));
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
              sv_slice(data_chi_out.prequest_udata.pbits.pdata.words[b / 4],
                       (b % 4) * 8, 8);
    if (uncached_chi_out.preq.pvalid && uncached_chi_in.preq.pready) {
      CHECK(!dactive && !wactive);
      if (pbmt_kind != 0) {
        CHECK(uncached_chi_out.preq.pbits.ptgt_uid == 1 &&
              (uncached_chi_out.preq.pbits.popcode == UINT64_C(3) ||
               uncached_chi_out.preq.pbits.popcode == UINT64_C(24)));
        CHECK(uncached_chi_out.preq.pbits.paddress >= UINT64_C(0x14000) &&
              uncached_chi_out.preq.pbits.paddress < UINT64_C(0x16000) &&
              uncached_chi_out.preq.pbits.psize_uor_unum_ureq == 3);
        CHECK(uncached_chi_out.preq.pbits.pmem_uattr.pdevice ==
              (pbmt_kind == 2));
        if (uncached_chi_out.preq.pbits.paddress < UINT64_C(0x15000)) {
          CHECK(uncached_chi_out.preq.pbits.popcode == UINT64_C(3));
          pbmt_fetches++;
        } else if (uncached_chi_out.preq.pbits.popcode == UINT64_C(3))
          pbmt_loads++;
        else
          pbmt_stores++;
      } else {
        CHECK((uncached_chi_out.preq.pbits.popcode == UINT64_C(4) ||
               uncached_chi_out.preq.pbits.popcode == UINT64_C(28)));
        CHECK(uncached_chi_out.preq.pbits.paddress >= UINT64_C(0x2000) &&
              uncached_chi_out.preq.pbits.paddress < UINT64_C(0x2400));
        CHECK(uncached_chi_out.preq.pbits.psize_uor_unum_ureq <= 3);
        switch (uncached_chi_out.preq.pbits.paddress) {
        case UINT64_C(0x2008):
        case UINT64_C(0x2009):
        case UINT64_C(0x2203): {
          CHECK(uncached_chi_out.preq.pbits.psize_uor_unum_ureq == 0);
        } break;
        case UINT64_C(0x200a): {
          CHECK(uncached_chi_out.preq.pbits.psize_uor_unum_ureq == 1);
        } break;
        case UINT64_C(0x200c):
        case UINT64_C(0x2300): {
          CHECK(uncached_chi_out.preq.pbits.psize_uor_unum_ureq == 2);
        } break;
        case UINT64_C(0x2010):
        case UINT64_C(0x20f0): {
          CHECK(uncached_chi_out.preq.pbits.psize_uor_unum_ureq == 3);
        } break;
        default: {
          fail(1, "unexpected or wrong-path IO transaction");
        } break;
        }
        CHECK(uncached_chi_out.preq.pbits.pmem_uattr.pdevice ==
              (uncached_chi_out.preq.pbits.paddress < UINT64_C(0x2300)));
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
          CHECK(
              sv_slice(uncached_chi_out.pdat.prequest.pbits.pdata.words[b / 4],
                       (b % 4) * 8, 8) == model_bytes[address]);
          backing[address] =
              sv_slice(uncached_chi_out.pdat.prequest.pbits.pdata.words[b / 4],
                       (b % 4) * 8, 8);
        }
      // A device command publishes externally written code before its completion.
      // The old instruction line is already resident; FENCE.I must discard it.
      if (phase == 12 && urequest.paddress == UINT64_C(0x20f0))
        insn(UINT64_C(0x700), addi(20, 0, 77));
      defer(ustate, 4);
      defer(udue, cycles + 40);
    }
  }
}

void falling_update() {}
