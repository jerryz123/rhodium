// Checks the production cache against an independent byte-addressed CHI and architectural oracle.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
using retirement_t = std::remove_cvref_t<decltype(retired_0_out.pbits)>;
using CHIReqFlit = std::remove_cvref_t<decltype(chi_out.prequests.pbits)>;
const auto &retired(unsigned i) { return i ? retired_1_out : retired_0_out; }
std::uint32_t program_words[1024];
std::uint8_t backing[4096], reference_bytes[4096];
std::uint64_t registers[32];
Queue<retirement_t> completion_queue;
int program_size, send_pc, reference_pc, cycles, commits, dual_commits, replays,
    branches;
int reads, writes, completions, hits_during_miss, alu_during_miss;
bool fault_seen, read_active, write_active;
CHIReqFlit pending_read, pending_write;
int read_due, read_packet;
bool reservation_valid = 0, probe_pending = 0, probe_accepted = 0,
     probe_complete = 0;
std::uint64_t reservation_address;
int reservation_width, probe_lr_pc, probe_sc_pc,
    atomic_commits = 0, atomic_dual = 0, sc_success = 0, sc_failure = 0;
int atomic_ops[9] = {1, 0, 4, 12, 8, 16, 20, 24, 28};
int split_resumes = 0;
int zero_commits = 0;
bool maintenance_active = 0, maintenance_snooped = 0, maintenance_data = 0;
CHIReqFlit pending_maintenance;
int maintenance_due, maintenance_requests = 0, maintenance_responses = 0,
                     maintenance_commits = 0;
int after_maintenance_reads, after_maintenance_misses;
int clean_resident_pc = -1;
bool check_maintenance_hit = 0;
bool check_maintenance_completion = 0;
std::uint64_t maintenance_load_pc;
int prefetch_reads[2] = {0, 0}, prefetch_commits = 0;
int waits = 0;
int ntl_reads[4] = {}, ntl_loads[4] = {};

std::uint32_t addi(int rd, int rs1, int imm) {
  return (field(((imm)&low_mask(12)), 12, 20) |
          field(((rs1)&low_mask(5)), 5, 15) | field(UINT64_C(0), 3, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(19), 7, 0));
}
std::uint32_t load(int rd, int rs1, int imm, int width) {
  return (field(((imm)&low_mask(12)), 12, 20) |
          field(((rs1)&low_mask(5)), 5, 15) |
          field(((width)&low_mask(3)), 3, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(3), 7, 0));
}
std::uint32_t store_insn(int rs2, int rs1, int imm, int width) {
  return (field(((imm >> 5) & low_mask(7)), 7, 25) |
          field(((rs2)&low_mask(5)), 5, 20) |
          field(((rs1)&low_mask(5)), 5, 15) |
          field(((width)&low_mask(3)), 3, 12) |
          field(((imm)&low_mask(5)), 5, 7) | field(UINT64_C(0x23), 7, 0));
}
std::uint32_t jal(int rd, int imm) {
  return (field(((imm >> 20) & low_mask(1)), 1, 31) |
          field(((imm >> 1) & low_mask(10)), 10, 21) |
          field(((imm >> 11) & low_mask(1)), 1, 20) |
          field(((imm >> 12) & low_mask(8)), 8, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(0x6f), 7, 0));
}
std::uint32_t atomic_insn(int op, int width, int rd, int rs1, int rs2,
                          int order_bits = 3) {
  return (field(((op)&low_mask(5)), 5, 27) |
          field(((order_bits)&low_mask(2)), 2, 25) |
          field(((rs2)&low_mask(5)), 5, 20) |
          field(((rs1)&low_mask(5)), 5, 15) |
          field(((width)&low_mask(3)), 3, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(0x2f), 7, 0));
}
void emit(std::uint32_t instruction) {
  program_words[program_size++] = instruction;
}

// A byte-addressed backing memory is independent of cache tags, ownership, and replacement.

void check_retirement(retirement_t got) {
  std::uint32_t insn;
  std::uint64_t value, address;
  std::int64_t imm;
  int rd, rs1, rs2, width, bytes_count;
  bool writes_rd;
  retirement_t expected;
  CHECK(got.pfetched.ppc == ((reference_pc)&low_mask(64)));
  insn = program_words[reference_pc / 4];
  CHECK(got.pfetched.pinstruction == insn);
  rd = int(sv_slice(insn, 7, (11) - (7) + 1));
  rs1 = int(sv_slice(insn, 15, (19) - (15) + 1));
  rs2 = int(sv_slice(insn, 20, (24) - (20) + 1));
  width = int(sv_slice(insn, 12, (14) - (12) + 1));
  value = 0;
  writes_rd = 0;
  reference_pc += 4;
  switch (sv_slice(insn, 0, (6) - (0) + 1)) {
  case UINT64_C(15): {
    {
      CHECK((sv_slice(insn, 20, (31) - (20) + 1) == 0 ||
             sv_slice(insn, 20, (31) - (20) + 1) == 1 ||
             sv_slice(insn, 20, (31) - (20) + 1) == 2 ||
             sv_slice(insn, 20, (31) - (20) + 1) == 4) &&
            width == 2 && rd == 0);
      address = registers[rs1] & ~UINT64_C(0x3f);
      if (sv_slice(insn, 20, (31) - (20) + 1) == 4) {
        for (int b = 0; b < 64; b++)
          reference_bytes[int(address) + b] = 0;
        if (reservation_valid && address / 64 == reservation_address / 64)
          reservation_valid = 0;
        CHECK(got.pdeferred && !got.pwrite);
        zero_commits++;
      } else {
        CHECK(!got.pdeferred && !got.pwrite && !maintenance_active);
        if (address < 4096) {
          CHECK(maintenance_responses == maintenance_commits + 1);
          for (int b = 0; b < 64; b++)
            CHECK(backing[int(address) + b] ==
                  reference_bytes[int(address) + b]);
          after_maintenance_reads = reads;
          after_maintenance_misses =
              got.pfetched.ppc == ((clean_resident_pc)&low_mask(64)) ? 0 : 1;
          check_maintenance_hit = 1;
        }
        maintenance_commits++;
      }
    }
  } break;
  case UINT64_C(19): {
    {
      value = registers[rs1] + ((std::int64_t(sign_extend(
                                    sv_slice(insn, 20, (31) - (20) + 1), 12))) &
                                low_mask(64));
      writes_rd = rd != 0;
      if (width == 6 && rd == 0) {
        CHECK(!got.pdeferred && !got.pwrite);
        prefetch_commits++;
      }
    }
  } break;
  case UINT64_C(0x37): {
    {
      value = sign_extend(insn & 0xfffff000, 32);
      writes_rd = rd != 0;
    }
  } break;
  case UINT64_C(0x33): {
    {
      CHECK((insn == UINT64_C(0x200033) || insn == UINT64_C(0x300033) ||
             insn == UINT64_C(0x400033) || insn == UINT64_C(0x500033)) &&
            !got.pwrite && !got.pdeferred);
    }
  } break;
  case UINT64_C(3): {
    {
      address = registers[rs1] +
                ((std::int64_t(
                     sign_extend(sv_slice(insn, 20, (31) - (20) + 1), 12))) &
                 low_mask(64));
      bytes_count = 1 << (width & 3);
      for (int b = 0; b < bytes_count; b++)
        sv_slice(value, b * 8, 8) = reference_bytes[int(address) + b];
      if (width < 4 && bytes_count < 8 &&
          sv_slice(value, bytes_count * 8 - 1, 1))
        value |= ~UINT64_C(0) << (bytes_count * 8);
      writes_rd = rd != 0;
      if (address >= 1792 && address < 2048) {
        int index = int((address - 1792) / 64);
        ntl_loads[index]++;
        if (ntl_loads[index] == 3)
          CHECK(!got.pdeferred);
      }
      if (read_active && !got.pdeferred)
        hits_during_miss++;
      if (check_maintenance_hit) {
        if (got.pdeferred) {
          check_maintenance_completion = 1;
          maintenance_load_pc = got.pfetched.ppc;
        } else
          CHECK(reads == after_maintenance_reads + after_maintenance_misses);
        check_maintenance_hit = 0;
      }
    }
  } break;
  case UINT64_C(0x23): {
    {
      imm = ((std::int64_t(
                 sign_extend((field(sv_slice(insn, 25, (31) - (25) + 1), 7, 5) |
                              field(sv_slice(insn, 7, (11) - (7) + 1), 5, 0)),
                             12))) &
             low_mask(64));
      address = registers[rs1] + imm;
      for (int b = 0; b < (1 << width); b++)
        reference_bytes[int(address) + b] = sv_slice(registers[rs2], b * 8, 8);
      if (reservation_valid && address / 64 == reservation_address / 64)
        reservation_valid = 0;
    }
  } break;
  case UINT64_C(0x2f): {
    {
      std::uint64_t replacement, operand;
      bool successful;
      address = registers[rs1];
      bytes_count = 1 << width;
      for (int b = 0; b < bytes_count; b++)
        sv_slice(value, b * 8, 8) = reference_bytes[int(address) + b];
      if (bytes_count == 4)
        value = sign_extend(value, 32);
      operand =
          bytes_count == 4 ? sign_extend(registers[rs2], 32) : registers[rs2];
      replacement = value;
      switch (sv_slice(insn, 27, (31) - (27) + 1)) {
      case 2: {
        {
          reservation_valid = 1;
          reservation_address = address;
          reservation_width = width;
        }
      } break;
      case 3: {
        {
          successful = reservation_valid && reservation_address == address &&
                       reservation_width == width;
          value = successful ? 0 : 1;
          replacement = operand;
          reservation_valid = 0;
          if (successful)
            sc_success++;
          else
            sc_failure++;
        }
      } break;
      case 1: {
        replacement = operand;
      } break;
      case 0: {
        replacement = value + operand;
      } break;
      case 4: {
        replacement = value ^ operand;
      } break;
      case 12: {
        replacement = value & operand;
      } break;
      case 8: {
        replacement = value | operand;
      } break;
      case 16: {
        replacement = std::int64_t(sign_extend(value, 64)) <
                              std::int64_t(sign_extend(operand, 64))
                          ? value
                          : operand;
      } break;
      case 20: {
        replacement = std::int64_t(sign_extend(value, 64)) >
                              std::int64_t(sign_extend(operand, 64))
                          ? value
                          : operand;
      } break;
      case 24: {
        replacement = value < operand ? value : operand;
      } break;
      case 28: {
        replacement = value > operand ? value : operand;
      } break;
      default: {
        fail(1, "unknown AMO");
      } break;
      }
      if (sv_slice(insn, 27, (31) - (27) + 1) != 2 &&
          (sv_slice(insn, 27, (31) - (27) + 1) != 3 || successful)) {
        for (int b = 0; b < bytes_count; b++)
          reference_bytes[int(address) + b] = sv_slice(replacement, b * 8, 8);
        if (sv_slice(insn, 27, (31) - (27) + 1) != 3 && reservation_valid &&
            address / 64 == reservation_address / 64)
          reservation_valid = 0;
      }
      writes_rd = rd != 0;
      atomic_commits++;
      CHECK(got.pdeferred);
    }
  } break;
  case UINT64_C(0x73): {
    {
      CHECK(insn == UINT64_C(0xd00073) && probe_accepted &&
            !reservation_valid && !got.pdeferred && !got.pwrite);
      waits++;
    }
  } break;
  case UINT64_C(0x6f): {
    {
      value = ((reference_pc)&low_mask(64));
      writes_rd = rd != 0;
      imm = ((std::int64_t(sign_extend(
                 (field(sv_slice(insn, 31, 1), 1, 20) |
                  field(sv_slice(insn, 12, (19) - (12) + 1), 8, 12) |
                  field(sv_slice(insn, 20, 1), 1, 11) |
                  field(sv_slice(insn, 21, (30) - (21) + 1), 10, 1) |
                  field(UINT64_C(0), 1, 0)),
                 21))) &
             low_mask(64));
      reference_pc = int(got.pfetched.ppc + imm);
    }
  } break;
  default: {
    fail(1, "unmodeled instruction %h", insn);
  } break;
  }
  if (read_active && sv_slice(insn, 0, (6) - (0) + 1) == UINT64_C(19))
    alu_during_miss++;
  CHECK(got.pwrite == writes_rd);
  if (writes_rd) {
    CHECK(got.prd == ((rd)&low_mask(5)));
    if (!got.pdeferred)
      CHECK(got.pdata == value);
    registers[rd] = value;
  }
  if (got.pdeferred) {
    expected = got;
    expected.pdata = value;
    completion_queue.push_back(expected);
  }
  commits++;
}

int main() {
  return run_test([] {
    reset = 1;
    node_id = 3;
    program_size = 0;
    send_pc = 0;
    reference_pc = 0;
    cycles = 0;
    for (int i = 0; i < 4096; i++) {
      backing[i] = ((i)&low_mask(8));
      reference_bytes[i] = ((i)&low_mask(8));
    }
    for (int i = 0; i < 32; i++)
      registers[i] = 0;
    emit(addi(1, 0, 0));
    emit(addi(2, 0, 85));
    emit(store_insn(2, 1, 24, 3));
    emit(addi(4, 0, 7));
    emit(load(3, 1, 24, 3));
    emit(addi(5, 4, 2));
    emit(addi(6, 3, 1));
    emit(store_insn(6, 1, 25, 0));
    emit(load(7, 1, 25, 4));
    emit(addi(2, 0, -128));
    emit(store_insn(2, 1, 31, 0));
    emit(load(12, 1, 31, 0));
    emit(load(13, 1, 31, 4));
    emit(load(14, 1, 24, 1));
    emit(load(15, 1, 28, 2));
    emit(load(16, 1, 28, 6));
    emit(load(8, 1, 128, 3));
    emit(addi(9, 0, 99));
    emit(load(10, 1, 24, 3));
    emit(addi(17, 0, 111));
    emit(load(11, 1, 192, 3));
    emit(jal(0, 12));
    emit(store_insn(2, 1, 24, 3));
    emit(addi(3, 0, 999));
    emit(addi(18, 11, 1));
    emit(store_insn(18, 1, 256, 3));
    emit(load(19, 1, 256, 3));
    emit(store_insn(19, 1, 512, 3));
    emit(load(20, 1, 512, 3));
    emit(load(21, 1, 24, 3));
    // Exercise every AMO in both widths, all aq/rl settings, and both word lanes.
    emit(addi(24, 0, 768));
    emit(addi(25, 0, -17));
    for (int width = 2; width <= 3; width++) {
      for (int variant = 0; variant < 2; variant++)
        for (int op = 0; op < 9; op++) {
          emit(addi(25, 0, variant == 0 ? -17 : 17));
          emit(addi(2, 0, variant == 0 ? 7 : -128));
          emit(store_insn(25, 24, 0, width));
          emit(atomic_insn(atomic_ops[op], width, 26, 24, 2, op % 4));
          emit(addi(29, 0, 9));
          emit(load(28, 24, 0, width));
          emit(addi(27, 26, 1));
        }
      emit(atomic_insn(2, width, 26, 24, 0, 0));
      emit(addi(29, 0, 3));
      emit(atomic_insn(3, width, 26, 24, 2, 1));
      emit(addi(27, 26, 1));
      emit(atomic_insn(3, width, 26, 24, 2, 2));
      emit(addi(27, 26, 1));
    }
    emit(addi(24, 24, 4));
    emit(atomic_insn(1, 2, 0, 24, 25));
    emit(load(26, 24, 0, 2));
    emit(atomic_insn(2, 2, 26, 24, 0));
    emit(atomic_insn(3, 2, 0, 24, 2));
    emit(load(26, 24, 0, 2));
    // A different word and a different width cannot satisfy an exact reservation.
    emit(atomic_insn(2, 2, 26, 24, 0));
    emit(addi(24, 24, 4));
    emit(atomic_insn(3, 2, 26, 24, 2));
    emit(addi(27, 26, 1));
    emit(atomic_insn(2, 2, 26, 24, 0));
    emit(atomic_insn(3, 3, 26, 24, 2));
    // An intervening same-line store invalidates LR even if it writes another byte.
    emit(atomic_insn(2, 3, 26, 24, 0));
    emit(store_insn(2, 24, 9, 0));
    emit(atomic_insn(3, 3, 26, 24, 2));
    emit(addi(27, 26, 1));
    emit(addi(24, 0, 768));
    probe_lr_pc = program_size * 4;
    emit(atomic_insn(2, 3, 26, 24, 0));
    emit(UINT64_C(0xd00073));
    probe_sc_pc = program_size * 4;
    emit(atomic_insn(3, 3, 26, 24, 2));
    emit(addi(27, 26, 1));
    emit(load(26, 24, 0, 3));
    // Exhaustive intra-word offsets, both extension modes, cross-word and
    // cross-line fragments, x0 loads, and preservation of neighboring bytes.
    for (int width = 1; width <= 3; width++)
      for (int offset = 1; offset <= 7; offset++)
        if (offset % (1 << width) != 0) {
          emit(store_insn(2, 1, 56 + offset, width));
          emit(load(3, 1, 56 + offset, width));
          emit(addi(4, 3, 1));
          if (width < 3)
            emit(load(5, 1, 56 + offset, width + 4));
          emit(load(0, 1, 56 + offset, width));
          emit(load(6, 1, 56, 3));
          emit(load(7, 1, 64, 3));
        }
    // Zero cold and owned-hit blocks, using deliberately unaligned rs1 values.
    // Observe every byte plus neighboring blocks through normal load retirement.
    for (int scenario = 0; scenario < 2; scenario++) {
      emit(addi(24, 0, scenario == 0 ? 1027 : 1151));
      if (scenario == 1)
        emit(store_insn(2, 1, 1088, 3));
      emit(UINT64_C(0x40200f) | (24 << 15));
      emit(addi(29, 0, 9));
      for (int word = 0; word < 10; word++)
        emit(load(26, 1, (scenario == 0 ? 1016 : 1080) + word * 8, 3));
    }
    // Kill a CBO before WB; its entire target must remain unchanged.
    emit(addi(24, 0, 1215));
    emit(jal(0, 8));
    emit(UINT64_C(0x40200f) | (24 << 15));
    for (int word = 0; word < 8; word++)
      emit(load(26, 1, 1152 + word * 8, 3));
    // Dirty lines publish data through Home-initiated self snoops. The shared
    // RN drops dirty copies even on CLEAN; CLEAN retains an already-clean copy.
    for (int operation = 0; operation < 3; operation++) {
      emit(addi(24, 0, 1280 + 64 * operation));
      emit(addi(2, 0, 91 + operation));
      emit(store_insn(2, 24, 0, 3));
      emit(addi(24, 24, 63));
      emit((((operation)&low_mask(32)) << 20) | UINT64_C(0x200f) | (24 << 15));
      emit(load(26, 1, 1280 + 64 * operation, 3));
      emit(addi(29, 0, 9));
      if (operation == 1) {
        clean_resident_pc = program_size * 4;
        emit(UINT64_C(0x10200f) | (24 << 15));
        emit(load(26, 1, 1344, 3));
      }
    }
    // Static device and noncacheable RAM blocks complete locally: neither
    // CHI port is permitted to send a synthetic data read/write.
    for (int region = 0; region < 2; region++) {
      emit(UINT64_C(0x22b7)); // LUI x5,2
      emit(addi(24, 5, region == 0 ? 63 : 831));
      for (int operation = 0; operation < 3; operation++)
        emit((((operation)&low_mask(32)) << 20) | UINT64_C(0x200f) |
             (24 << 15));
    }
    // Retiring best-effort hints fetch real lines. Independent scalar work
    // continues while they refill; the following demand must use the line.
    for (int p = 0; p < 2; p++) {
      emit(addi(24, 0, 1664 + p * 64));
      emit(addi(28, 0, p + 1));
      emit((field(UINT64_C(0), 7, 25) |
            field(((p == 0 ? 1 : 3) & low_mask(5)), 5, 20) |
            field(UINT64_C(24), 5, 15) | field(UINT64_C(6), 3, 12) |
            field(UINT64_C(0), 5, 7) | field(UINT64_C(19), 7, 0)));
      for (int repeat_index = 0; repeat_index < (140); ++repeat_index)
        emit(addi(29, 29, 1));
      emit(addi(28, 0, 0));
      emit(load(26, 24, 0, 3));
      if (p == 1)
        emit(store_insn(2, 24, 0, 3));
    }
    // Each hinted cold load bypasses allocation. The ordinary second load
    // refills again, and its dependent third access must hit that installation.
    for (int hint = 0; hint < 4; hint++) {
      emit(UINT64_C(0x33) | (((hint + 2) & low_mask(32)) << 20));
      for (int access_index = 0; access_index < 3; access_index++) {
        emit(load(26, 1, 1792 + hint * 64, 3));
        emit(addi(27, 26, 1));
      }
    }
    // Establish a fresh miss immediately before a fault: accepted work must drain first.
    emit(load(22, 1, 704, 3));
    emit(addi(23, 0, 2047));
    emit(addi(23, 23, 2047));
    emit(addi(23, 23, -1));
    emit(load(0, 23, 0, 3));
    emit(store_insn(2, 1, 24, 3));
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index)
      falling();
    reset = 0;
    until([&] { return fault_seen; });
    for (int repeat_index = 0; repeat_index < (8); ++repeat_index)
      falling();
    CHECK(reads >= 6 && writes > 0 && completions >= 6 && dual_commits > 0 &&
          branches == 2 && replays > 0 && zero_commits == 2);
    CHECK(hits_during_miss > 0 && alu_during_miss > 0);
    CHECK(atomic_commits == 53 && atomic_dual > 0 && sc_success == 3 &&
          sc_failure == 6 && probe_complete);
    CHECK(split_resumes == 72 && waits == 1 && maintenance_commits == 10 &&
          maintenance_requests == 4 && maintenance_responses == 4 &&
          !check_maintenance_completion && !check_maintenance_hit);
    CHECK(prefetch_commits == 2 && prefetch_reads[0] == 1 &&
          prefetch_reads[1] == 1);

    for (int hint = 0; hint < 4; hint++)
      CHECK(ntl_reads[hint] == 2 && ntl_loads[hint] == 3);
  });
}

void drive() {
  management_pbmt = maintenance_commits % 3;
  {
    chi_in = {};
    chi_in.prequests.pready =
        !read_active && !write_active && !maintenance_active && cycles % 5 != 0;
    chi_in.prequester_uresponses.pready = cycles % 4 != 0;
    chi_in.prequest_udata.pready = cycles % 3 != 0;
    chi_in.psnoops.pvalid = probe_pending && !probe_accepted;
    chi_in.psnoops.pbits.popcode = UINT64_C(9);
    chi_in.psnoops.pbits.paddress = ((768 >> 3) & low_mask(41));
    chi_in.psnoops.pbits.psrc_uid = UINT64_C(1);
    chi_in.psnoops.pbits.ptxn_uid = UINT64_C(0x100);
    if (maintenance_active && !maintenance_snooped) {
      chi_in.psnoops.pvalid = 1;
      chi_in.psnoops.pbits.popcode =
          ((pending_maintenance.popcode) & low_mask(5));
      chi_in.psnoops.pbits.paddress =
          ((pending_maintenance.paddress >> 3) & low_mask(41));
      chi_in.psnoops.pbits.ptxn_uid = UINT64_C(0x101);
    }
    if (write_active) {
      chi_in.presponses.pvalid = 1;
      chi_in.presponses.pbits.popcode = UINT64_C(5);
      chi_in.presponses.pbits.psrc_uid = UINT64_C(1);
      chi_in.presponses.pbits.ptgt_uid = UINT64_C(3);
      chi_in.presponses.pbits.ptxn_uid = pending_write.ptxn_uid;
      chi_in.presponses.pbits.pdbid_uor_ugroup_uid = UINT64_C(9);
    }
    if (maintenance_active && maintenance_data && cycles >= maintenance_due) {
      chi_in.presponses.pvalid = 1;
      chi_in.presponses.pbits.popcode = UINT64_C(4);
      chi_in.presponses.pbits.psrc_uid = UINT64_C(1);
      chi_in.presponses.pbits.ptgt_uid = UINT64_C(3);
      chi_in.presponses.pbits.ptxn_uid = pending_maintenance.ptxn_uid;
    }
    if (read_active && cycles >= read_due) {
      chi_in.presponse_udata.pvalid = 1;
      chi_in.presponse_udata.pbits.popcode = UINT64_C(4);
      chi_in.presponse_udata.pbits.psrc_uid = UINT64_C(1);
      chi_in.presponse_udata.pbits.ptgt_uid = UINT64_C(3);
      chi_in.presponse_udata.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
          UINT64_C(1);
      chi_in.presponse_udata.pbits.ptxn_uid = pending_read.ptxn_uid;
      chi_in.presponse_udata.pbits.pdbid_uor_umecid = UINT64_C(5);
      chi_in.presponse_udata.pbits.presp =
          pending_read.popcode == UINT64_C(7) ? UINT64_C(2) : UINT64_C(1);
      chi_in.presponse_udata.pbits.pdata_uid = ((read_packet)&low_mask(2));
      chi_in.presponse_udata.pbits.pbyte_uenable = UINT16_MAX;
      for (int b = 0; b < 16; b++)
        sv_slice(chi_in.presponse_udata.pbits.pdata.words[b / 4], (b % 4) * 8,
                 8) =
            backing[int(pending_read.paddress) + 16 * read_packet + b];
    }
  }
}

void observe() {
  if (!reset) {
    CHECK(!uncached_activity);
    defer(cycles, cycles + 1);
    if (cycles > 10000)
      fail(1,
           "cache/core timeout pc=%h reference=%h reads=%0d completions=%0d "
           "atomic=%0d probe=%b/%b/%b LR=%h SC=%h",
           send_pc, reference_pc, reads, completions, atomic_commits,
           probe_pending, probe_accepted, probe_complete, probe_lr_pc,
           probe_sc_pc);
    if (instructions_in.pvalid && instructions_out.pready)
      send_pc += 4 * int(instructions_in.pbits.pcount);
    if (retired(0).pvalid && retired(1).pvalid)
      dual_commits++;
    if (retired(0).pvalid && retired(1).pvalid &&
        (sv_slice(retired(0).pbits.pfetched.pinstruction, 0, (6) - (0) + 1) ==
             UINT64_C(0x2f) ||
         sv_slice(retired(1).pbits.pfetched.pinstruction, 0, (6) - (0) + 1) ==
             UINT64_C(0x2f)))
      atomic_dual++;
    for (int slot = 0; slot < 2; slot++)
      if (retired(slot).pvalid)
        check_retirement(retired(slot).pbits);
    if (completed_out.pvalid) {
      retirement_t expected;
      CHECK(completion_queue.size() > 0);
      expected = completion_queue.take();
      CHECK(same_instruction(completed_out.pbits.pfetched, expected.pfetched) &&
            completed_out.pbits.prd == expected.prd &&
            completed_out.pbits.pwrite == expected.pwrite);
      if (expected.pwrite) {
        CHECK(completed_out.pbits.pdata == expected.pdata);
      }
      if (check_maintenance_completion &&
          completed_out.pbits.pfetched.ppc == maintenance_load_pc) {
        CHECK(reads == after_maintenance_reads + after_maintenance_misses);
        check_maintenance_completion = 0;
      }
      completions++;
      if (completed_out.pbits.pfetched.ppc == ((probe_lr_pc)&low_mask(64)))
        defer(probe_pending, 1);
    }
    if (redirect_out.pvalid) {
      switch (redirect_out.pbits.presolution.pdisposition) {
      case 0: {
        {
          send_pc = int(redirect_out.pbits.ptarget);
          branches++;
        }
      } break;
      case 2: {
        {
          send_pc = int(redirect_out.pbits.ppc);
          replays++;
        }
      } break;
      case 3: {
        {
          CHECK(redirect_out.pbits.ptarget == ((reference_pc)&low_mask(64)));
          send_pc = int(redirect_out.pbits.ptarget);
          split_resumes++;
        }
      } break;
      case 1: {
        {
          CHECK(redirect_out.pbits.ppc == ((reference_pc)&low_mask(64)) &&
                redirect_out.pbits.presolution.pcause == 5 &&
                redirect_out.pbits.presolution.pvalue == 4096);
          CHECK(completion_queue.size() == 0);
          fault_seen = 1;
        }
      } break;
      default: {
        fail(1, "bad disposition");
      } break;
      }
    }
    if (chi_out.prequests.pvalid && chi_in.prequests.pready) {

      CHECK(chi_out.prequests.pbits.paddress < 4096);
      switch (chi_out.prequests.pbits.popcode) {
      case UINT64_C(2):
      case UINT64_C(7): {
        {
          if (chi_out.prequests.pbits.paddress >= 1792 &&
              chi_out.prequests.pbits.paddress < 2048)
            ntl_reads[int((chi_out.prequests.pbits.paddress - 1792) / 64)]++;
          for (int p = 0; p < 2; p++)
            if (chi_out.prequests.pbits.paddress ==
                ((1664 + p * 64) & low_mask(44))) {
              CHECK(chi_out.prequests.pbits.popcode ==
                    (p == 0 ? UINT64_C(2) : UINT64_C(7)));
              CHECK(registers[28] == ((p + 1) & low_mask(64)));
              prefetch_reads[p]++;
            }
          defer(pending_read, chi_out.prequests.pbits);
          defer(read_due, cycles + 35);
          defer(read_packet, 0);
          defer(read_active, 1);
          reads++;
        }
      } break;
      case UINT64_C(27): {
        {
          defer(pending_write, chi_out.prequests.pbits);
          defer(write_active, 1);
          writes++;
        }
      } break;
      case UINT64_C(8):
      case UINT64_C(9): {
        {
          CHECK(chi_out.prequests.pbits.pexcl_usnoop_ume_ucah &&
                sv_slice(chi_out.prequests.pbits.paddress, 0, (5) - (0) + 1) ==
                    0);
          defer(pending_maintenance, chi_out.prequests.pbits);
          defer(maintenance_active, 1);
          defer(maintenance_snooped, 0);
          defer(maintenance_data, 0);
          defer(maintenance_due, cycles + 40);
          maintenance_requests++;
        }
      } break;
      default: {
        fail(1, "unexpected request opcode %h",
             chi_out.prequests.pbits.popcode);
      } break;
      }
    }
    if (chi_in.presponse_udata.pvalid && chi_out.presponse_udata.pready) {

      if (read_packet == 3)
        defer(read_active, 0);
      else {
        defer(read_packet, read_packet + 1);
        defer(read_due, cycles + 2);
      }
    }
    if (chi_in.psnoops.pvalid && chi_out.psnoops.pready) {
      if (chi_in.psnoops.pbits.ptxn_uid == UINT64_C(0x101))
        defer(maintenance_snooped, 1);
      else
        defer(probe_accepted, 1);
      reservation_valid = 0;
    }
    if (chi_out.prequester_uresponses.pvalid &&
        chi_in.prequester_uresponses.pready) {

      if (chi_out.prequester_uresponses.pbits.popcode == UINT64_C(1) &&
          chi_out.prequester_uresponses.pbits.ptxn_uid == UINT64_C(0x100))
        defer(probe_complete, 1);
      if (chi_out.prequester_uresponses.pbits.popcode == UINT64_C(1) &&
          chi_out.prequester_uresponses.pbits.ptxn_uid == UINT64_C(0x101))
        defer(maintenance_data, 1);
    }
    if (chi_in.presponses.pvalid && chi_out.presponses.pready) {
      if (maintenance_active) {
        defer(maintenance_active, 0);
        maintenance_responses++;
      } else
        defer(write_active, 0);
    }
    if (chi_out.prequest_udata.pvalid && chi_in.prequest_udata.pready) {

      CHECK((chi_out.prequest_udata.pbits.popcode == UINT64_C(1) ||
             chi_out.prequest_udata.pbits.popcode == UINT64_C(2)));
      for (int b = 0; b < 16; b++)
        if (sv_slice(chi_out.prequest_udata.pbits.pbyte_uenable, b, 1))
          backing[(chi_out.prequest_udata.pbits.popcode == 1
                       ? (maintenance_active ? int(pending_maintenance.paddress)
                                             : 768)
                       : int(pending_write.paddress)) +
                  16 * int(chi_out.prequest_udata.pbits.pdata_uid) + b] =
              sv_slice(chi_out.prequest_udata.pbits.pdata.words[b / 4],
                       (b % 4) * 8, 8);
      if (chi_out.prequest_udata.pbits.popcode == 1 &&
          chi_out.prequest_udata.pbits.pdata_uid == 3) {
        if (maintenance_active)
          defer(maintenance_data, 1);
        else
          defer(probe_complete, 1);
      }
    }
  }
}

void falling_update() {
  {
    instructions_in = {};
    if (!reset && !fault_seen && send_pc / 4 < program_size &&
        (send_pc < probe_sc_pc || probe_complete)) {
      instructions_in.pvalid = 1;
      instructions_in.pbits.pcount = send_pc / 4 + 1 < program_size ? 2 : 1;
      if (send_pc + 4 == probe_sc_pc && !probe_complete)
        instructions_in.pbits.pcount = 1;
      instructions_in.pbits.pentries[0] = {
          .ppc = ((send_pc)&low_mask(64)),
          .pinstruction = program_words[send_pc / 4],
          .praw_uinstruction = program_words[send_pc / 4],
          .psequential_upc = ((send_pc + 4) & low_mask(64)),
          .pcompressed_uillegal = UINT64_C(0),
          .pfault = {},
          .pprediction = {},
          .pspeculated_uras_uaction = UINT64_C(0),
          .pdirection = {}};
      instructions_in.pbits.pentries[1] = {
          .ppc = ((send_pc + 4) & low_mask(64)),
          .pinstruction = program_words[send_pc / 4 + 1],
          .praw_uinstruction = program_words[send_pc / 4 + 1],
          .psequential_upc = ((send_pc + 8) & low_mask(64)),
          .pcompressed_uillegal = UINT64_C(0),
          .pfault = {},
          .pprediction = {},
          .pspeculated_uras_uaction = UINT64_C(0),
          .pdirection = {}};
    }
  }
}
