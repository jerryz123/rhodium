// Checks the complete dual-issue architectural oracle, memory ownership, and precise recovery.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
using instruction_t =
    std::remove_cvref_t<decltype(instructions_in.pbits.pentries[0])>;
using prediction_t = std::remove_cvref_t<
    decltype(instructions_in.pbits.pentries[0].pprediction)>;
using direction_t =
    std::remove_cvref_t<decltype(instructions_in.pbits.pentries[0].pdirection)>;
using retirement_t = std::remove_cvref_t<decltype(retired_0_out.pbits)>;
using redirect_t = std::remove_cvref_t<decltype(redirect_out.pbits)>;
using resolution_t = std::remove_cvref_t<decltype(resolution_0_in.pbits)>;
using split_result_t = std::remove_cvref_t<decltype(split_in.presponse.pbits)>;
using memory_req_t = std::remove_cvref_t<decltype(memory_out.prequest.pbits)>;
using lookup_flow_t = std::remove_cvref_t<decltype(pipeline_in.presponse)>;
using prefetch_t = std::remove_cvref_t<decltype(prefetch_out.pbits)>;
auto &resolution(unsigned i) { return i ? resolution_1_in : resolution_0_in; }
const auto &memory_stage(unsigned i) {
  return i ? memory_stage_1_out : memory_stage_0_out;
}
const auto &retired(unsigned i) { return i ? retired_1_out : retired_0_out; }

void set_interrupts(std::uint64_t value) {
  interrupts = {std::uint8_t(value >> 5 & 1), std::uint8_t(value >> 4 & 1),
                std::uint8_t(value >> 3 & 1), std::uint8_t(value >> 2 & 1),
                std::uint8_t(value >> 1 & 1), std::uint8_t(value & 1)};
}
bool split_active = 0, hold_split = 0, split_fault = 0;
int split_due = 0, split_requests = 0;
std::uint8_t expected_split_locality = 0;
split_result_t split_reply;

constexpr std::uint64_t timer_compare = UINT64_C(0x10000007b);

bool reservation_valid = 0;
std::uint64_t trap_target = 0;
int mem_branch_redirects = 0, wb_overrides = 0;
int minimum_instruction_capacity = 8;
bool inject_enable;
std::uint64_t inject_pc;
resolution_t inject_result;
Queue<retirement_t> expected;
Queue<redirect_t> expected_redirects;
std::uint64_t model[32];
int cycles = 0, commits = 0, dual_commits = 0, single_issues = 0, stops = 0;
int dual_run = 0, longest_dual_run = 0;
int pause_commits = 0, pause_dual = 0, pause_cycle = 0;
bool saw_repacked = 0;

lookup_flow_t lookup_response;
memory_req_t lookup_request, store_candidate;
Queue<memory_req_t> expected_requests;
Queue<retirement_t> expected_completions, response_owners;
std::uint64_t response_data[16];
int response_due[16], response_read = 0, response_write = 0, response_count = 0;
std::uint8_t memory_bytes[4096], model_bytes[4096];
bool block_requests = 0, block_stores = 0, hold_responses = 0;
bool inject_memory_fault = 0, store_candidate_valid = 0;
std::uint64_t fault_address;
int configured_delay = 8, lookup_mode = 0;
int requests = 0, responses = 0, canceled = 0, stores = 0, stall_cycles = 0,
    hits = 0, lookups = 0;
int overlap_retirements = 0, max_outstanding = 0, shared_writes = 0,
    reserved_slots = 0;

int translation_invalidations = 0;

int branch_updates = 0;
int invalidations = 0;
int multiply_mem_cycle = -1, dependent_mem_cycle = -1;
std::map<std::uint64_t, int> multiply_authorized_cycle;
int multiply_stream_cycle = -1, multiply_stream_count = 0;
int conditional_dual = 0, mop_dual = 0;
int waw_dual = 0, waw_deferred = 0;
int address_pairs = 0;
int auipc_addi_pairs = 0;
int guest_address_pairs = 0;
bool response_management[16];
int store_data_pairs = 0;
int branch_pairs = 0;
int dual_branches = 0, ras_resolutions = 0, max_training_pending = 0;
Queue<std::uint64_t> training_pending;

std::map<std::uint64_t, bool> expected_branch_taken;

std::map<std::uint64_t, prefetch_t> expected_prefetch;
int prefetch_count = 0, prefetch_pairs = 0;

std::uint8_t prefetch_encoding(std::uint32_t word) {
  return sv_slice(word, 0, (6) - (0) + 1) == UINT64_C(19) &&
         sv_slice(word, 12, (14) - (12) + 1) == 6 &&
         sv_slice(word, 7, (11) - (7) + 1) == 0 &&
         (sv_slice(word, 20, (24) - (20) + 1) == 0 ||
          sv_slice(word, 20, (24) - (20) + 1) == 1 ||
          sv_slice(word, 20, (24) - (20) + 1) == 3);
}
std::uint32_t prefetch_insn(int operation, int rs1, int offset = 0) {
  return (field(((offset >> 5) & low_mask(7)), 7, 25) |
          field(((operation)&low_mask(5)), 5, 20) |
          field(((rs1)&low_mask(5)), 5, 15) | field(UINT64_C(6), 3, 12) |
          field(UINT64_C(0), 5, 7) | field(UINT64_C(19), 7, 0));
}

std::uint8_t mop_encoding(std::uint32_t word) {
  return (word & UINT64_C(0xb3c0707f)) == UINT64_C(0x81c04073) ||
         (word & UINT64_C(0xb200707f)) == UINT64_C(0x82004073);
}
std::uint8_t conditional_encoding(std::uint32_t word) {
  return sv_slice(word, 0, (6) - (0) + 1) == UINT64_C(0x33) &&
         sv_slice(word, 25, (31) - (25) + 1) == UINT64_C(7) &&
         (sv_slice(word, 12, (14) - (12) + 1) == UINT64_C(5) ||
          sv_slice(word, 12, (14) - (12) + 1) == UINT64_C(7));
}
std::uint8_t serializing_encoding(std::uint32_t word) {
  return sv_slice(word, 0, (6) - (0) + 1) == UINT64_C(0x73) &&
         !mop_encoding(word) &&
         !(sv_slice(word, 28, (31) - (28) + 1) == UINT64_C(6) &&
           sv_slice(word, 12, (14) - (12) + 1) == 4);
}

std::map<std::uint64_t, direction_t> offered_direction;
std::map<std::uint64_t, std::uint16_t> corrected_history;
int direction_updates = 0, history_recoveries = 0;
direction_t direction_metadata(std::uint64_t pc, std::uint32_t word,
                               std::uint8_t taken = 0) {
  return {sv_slice(word, 0, (6) - (0) + 1) == UINT64_C(0x63),
          std::uint16_t(((pc >> 1) ^ 725) & 4095),
          std::uint16_t(((pc >> 1) ^ 341) & 1023), taken};
}
std::uint8_t branch_encoding(std::uint32_t word) {
  return (sv_slice(word, 0, (6) - (0) + 1) == UINT64_C(0x63) ||
          sv_slice(word, 0, (6) - (0) + 1) == UINT64_C(0x6f) ||
          sv_slice(word, 0, (6) - (0) + 1) == UINT64_C(0x67));
}
std::uint8_t ras_action(std::uint32_t word) {
  bool rd_link, rs1_link;
  rd_link = (sv_slice(word, 7, (11) - (7) + 1) == 1 ||
             sv_slice(word, 7, (11) - (7) + 1) == 5);
  rs1_link = (sv_slice(word, 15, (19) - (15) + 1) == 1 ||
              sv_slice(word, 15, (19) - (15) + 1) == 5);
  if (sv_slice(word, 0, (6) - (0) + 1) == UINT64_C(0x6f))
    return rd_link ? 1 : 0;
  if (sv_slice(word, 0, (6) - (0) + 1) != UINT64_C(0x67))
    return 0;
  if (!rd_link)
    return rs1_link ? 2 : 0;
  return rs1_link && sv_slice(word, 7, (11) - (7) + 1) !=
                         sv_slice(word, 15, (19) - (15) + 1)
             ? 3
             : 1;
}

std::uint32_t imm(int rd, int rs1, int value, int f3 = 0,
                  int op = UINT64_C(19)) {
  return (field(((value)&low_mask(12)), 12, 20) |
          field(((rs1)&low_mask(5)), 5, 15) | field(((f3)&low_mask(3)), 3, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(((op)&low_mask(7)), 7, 0));
}
std::uint32_t regop(int rd, int rs1, int rs2, int f3 = 0, int f7 = 0,
                    int op = UINT64_C(0x33)) {
  return (field(((f7)&low_mask(7)), 7, 25) | field(((rs2)&low_mask(5)), 5, 20) |
          field(((rs1)&low_mask(5)), 5, 15) | field(((f3)&low_mask(3)), 3, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(((op)&low_mask(7)), 7, 0));
}
std::uint32_t branch(int rs1, int rs2, int offset, int f3 = 0) {
  std::uint16_t b;
  b = ((offset)&low_mask(13));
  return (field(sv_slice(b, 12, 1), 1, 31) |
          field(sv_slice(b, 5, (10) - (5) + 1), 6, 25) |
          field(((rs2)&low_mask(5)), 5, 20) |
          field(((rs1)&low_mask(5)), 5, 15) | field(((f3)&low_mask(3)), 3, 12) |
          field(sv_slice(b, 1, (4) - (1) + 1), 4, 8) |
          field(sv_slice(b, 11, 1), 1, 7) | field(UINT64_C(0x63), 7, 0));
}
std::uint32_t jump(int rd, int offset) {
  std::uint32_t j;
  j = ((offset)&low_mask(21));
  return (field(sv_slice(j, 20, 1), 1, 31) |
          field(sv_slice(j, 1, (10) - (1) + 1), 10, 21) |
          field(sv_slice(j, 11, 1), 1, 20) |
          field(sv_slice(j, 12, (19) - (12) + 1), 8, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(0x6f), 7, 0));
}
std::uint32_t store(int rs2, int rs1, int offset, int width) {
  std::uint16_t s;
  s = ((offset)&low_mask(12));
  return (
      field(sv_slice(s, 5, (11) - (5) + 1), 7, 25) |
      field(((rs2)&low_mask(5)), 5, 20) | field(((rs1)&low_mask(5)), 5, 15) |
      field(((width)&low_mask(3)), 3, 12) |
      field(sv_slice(s, 0, (4) - (0) + 1), 5, 7) | field(UINT64_C(0x23), 7, 0));
}

void expect_memory(std::uint64_t address, std::uint8_t write_access, int bytes,
                   std::uint64_t value) {
  memory_req_t item;
  item = {};
  item.paddress = address;
  item.paccess = write_access ? 2 : 1;
  item.pwidth = ((std::countr_zero(unsigned(bytes))) & low_mask(2));
  for (int b = 0; b < bytes; b++) {
    sv_slice(item.pmask, int(sv_slice(address, 0, 3)) + b, 1) = 1;
    sv_slice(item.pdata, (int(sv_slice(address, 0, 3)) + b) * 8, 8) =
        sv_slice(value, b * 8, 8);
  }
  expected_requests.push_back(item);
}

void check_request(memory_req_t actual) {
  memory_req_t want;
  CHECK(expected_requests.size() > 0);
  want = expected_requests.take();
  CHECK(actual.paddress == want.paddress && actual.paccess == want.paccess &&
        actual.pwidth == want.pwidth && actual.plocality == want.plocality &&
        (want.paccess >= 6 || actual.pmask == want.pmask));
  if (want.paccess == 6)
    for (int b = 0; b < 64; b++)
      defer(memory_bytes[int(actual.paddress & ~UINT64_C(0x3f)) + b], 0);
  if (want.paccess == 2) {
    for (int b = 0; b < 8; b++) {
      if (sv_slice(want.pmask, b, 1)) {
        CHECK(sv_slice(actual.pdata, b * 8, 8) ==
              sv_slice(want.pdata, b * 8, 8));
        defer(
            memory_bytes[int(sv_slice(actual.paddress, 3, (11) - (3) + 1)) * 8 +
                         b],
            std::uint8_t(sv_slice(actual.pdata, b * 8, 8)));
      }
    }
    stores++;
  }
}

// Independent architectural B oracle: bit loops rather than the RTL's shared
// shifter/count/adder implementation. Results include each instruction's own
// word projection; ADD.UW, SLLI.UW, and ZEXT.H are not sign-extended word ops.
std::uint8_t bitmanip_value(std::uint32_t word, std::uint64_t a,
                            std::uint64_t b, std::uint64_t &value) {
  int op = int(sv_slice(word, 0, (6) - (0) + 1)),
      f3 = int(sv_slice(word, 12, (14) - (12) + 1)),
      f7 = int(sv_slice(word, 25, (31) - (25) + 1));
  int n = (op == UINT64_C(0x3b) || op == UINT64_C(27)) ? 32 : 64;
  int shift = int(sv_slice(b, 0, (5) - (0) + 1));
  bool right;
  value = 0;
  if (op == UINT64_C(19) || op == UINT64_C(27)) {
    if (f3 == 1 && sv_slice(word, 20, (31) - (20) + 1) >= UINT64_C(0x600) &&
        sv_slice(word, 20, (31) - (20) + 1) <= UINT64_C(0x602)) {
      switch (sv_slice(word, 20, (21) - (20) + 1)) {
      case 0: {
        {
          value = ((n)&low_mask(64));
          for (int i = 0; i < n; i++)
            if (sv_slice(a, i, 1))
              value = ((n)&low_mask(64)) - UINT64_C(1) - ((i)&low_mask(64));
        }
      } break;
      case 1: {
        {
          value = ((n)&low_mask(64));
          for (int i = n - 1; i >= 0; i--)
            if (sv_slice(a, i, 1))
              value = ((i)&low_mask(64));
        }
      } break;
      case 2: {
        for (int i = 0; i < n; i++)
          value += ((sv_slice(a, i, 1)) & low_mask(64));
      } break;
      }
      return 1;
    }
    if (op == UINT64_C(19) && f3 == 1 &&
        sv_slice(word, 20, (31) - (20) + 1) == UINT64_C(0x604)) {
      value = sign_extend(a, 8);
      return 1;
    }
    if (op == UINT64_C(19) && f3 == 1 &&
        sv_slice(word, 20, (31) - (20) + 1) == UINT64_C(0x605)) {
      value = sign_extend(a, 16);
      return 1;
    }
    if (op == UINT64_C(19) && f3 == 5 &&
        sv_slice(word, 20, (31) - (20) + 1) == UINT64_C(0x287)) {
      for (int i = 0; i < 8; i++)
        sv_slice(value, i * 8, 8) =
            sv_slice(a, i * 8, 8) != 0 ? UINT64_C(0xff) : 0;
      return 1;
    }
    if (op == UINT64_C(19) && f3 == 5 &&
        sv_slice(word, 20, (31) - (20) + 1) == UINT64_C(0x6b8)) {
      for (int i = 0; i < 8; i++)
        sv_slice(value, i * 8, 8) = sv_slice(a, (7 - i) * 8, 8);
      return 1;
    }
    shift = int(sv_slice(word, 20, (25) - (20) + 1));
    if (op == UINT64_C(27) && f3 == 1 &&
        sv_slice(word, 26, (31) - (26) + 1) == 2) {
      value = (field(UINT64_C(0), 32, 32) |
               field(sv_slice(a, 0, (31) - (0) + 1), 32, 0))
              << shift;
      return 1;
    }
    if ((op == UINT64_C(19) &&
         sv_slice(word, 26, (31) - (26) + 1) == UINT64_C(24) && f3 == 5) ||
        (op == UINT64_C(27) && f7 == UINT64_C(0x30) && f3 == 5))
      right = 1;
    else if (op == UINT64_C(19) &&
             sv_slice(word, 26, (31) - (26) + 1) == UINT64_C(18) && f3 == 1) {
      value = a & ~(UINT64_C(1) << shift);
      return 1;
    } else if (op == UINT64_C(19) &&
               sv_slice(word, 26, (31) - (26) + 1) == UINT64_C(18) && f3 == 5) {
      value = ((sv_slice(a, shift, 1)) & low_mask(64));
      return 1;
    } else if (op == UINT64_C(19) &&
               sv_slice(word, 26, (31) - (26) + 1) == UINT64_C(26) && f3 == 1) {
      value = a ^ (UINT64_C(1) << shift);
      return 1;
    } else if (op == UINT64_C(19) &&
               sv_slice(word, 26, (31) - (26) + 1) == UINT64_C(10) && f3 == 1) {
      value = a | (UINT64_C(1) << shift);
      return 1;
    } else
      return 0;
  } else if (op == UINT64_C(0x33) || op == UINT64_C(0x3b)) {
    if (f7 == UINT64_C(16) && (f3 == 2 || f3 == 4 || f3 == 6)) {
      value = ((op == UINT64_C(0x3b)
                    ? (field(UINT64_C(0), 32, 32) |
                       field(sv_slice(a, 0, (31) - (0) + 1), 32, 0))
                    : a)
               << (f3 / 2)) +
              b;
      return 1;
    }
    if (op == UINT64_C(0x3b) && f7 == 4 && f3 == 0) {
      value = (field(UINT64_C(0), 32, 32) |
               field(sv_slice(a, 0, (31) - (0) + 1), 32, 0)) +
              b;
      return 1;
    }
    if (op == UINT64_C(0x3b) && f7 == 4 && f3 == 4 &&
        sv_slice(word, 20, (24) - (20) + 1) == 0) {
      value = (field(UINT64_C(0), 48, 16) |
               field(sv_slice(a, 0, (15) - (0) + 1), 16, 0));
      return 1;
    }
    if (op == UINT64_C(0x33) && f7 == UINT64_C(0x20))
      switch (f3) {
      case 4: {
        {
          value = ~(a ^ b);
          return 1;
        }
      } break;
      case 6: {
        {
          value = a | ~b;
          return 1;
        }
      } break;
      case 7: {
        {
          value = a & ~b;
          return 1;
        }
      } break;
      default: {
        return 0;
      } break;
      }
    if (op == UINT64_C(0x33) && f7 == 5)
      switch (f3) {
      case 4: {
        {
          value = std::int64_t(a) < std::int64_t(b) ? a : b;
          return 1;
        }
      } break;
      case 5: {
        {
          value = a < b ? a : b;
          return 1;
        }
      } break;
      case 6: {
        {
          value = std::int64_t(a) > std::int64_t(b) ? a : b;
          return 1;
        }
      } break;
      case 7: {
        {
          value = a > b ? a : b;
          return 1;
        }
      } break;
      default: {
        return 0;
      } break;
      }
    if (f7 == UINT64_C(0x30) && (f3 == 1 || f3 == 5))
      right = f3 == 5;
    else if (op == UINT64_C(0x33) && f7 == UINT64_C(0x24) && f3 == 1) {
      value = a & ~(UINT64_C(1) << shift);
      return 1;
    } else if (op == UINT64_C(0x33) && f7 == UINT64_C(0x24) && f3 == 5) {
      value = ((sv_slice(a, shift, 1)) & low_mask(64));
      return 1;
    } else if (op == UINT64_C(0x33) && f7 == UINT64_C(0x34) && f3 == 1) {
      value = a ^ (UINT64_C(1) << shift);
      return 1;
    } else if (op == UINT64_C(0x33) && f7 == UINT64_C(20) && f3 == 1) {
      value = a | (UINT64_C(1) << shift);
      return 1;
    } else
      return 0;
  } else
    return 0;
  shift %= n;
  for (int i = 0; i < n; i++)
    sv_slice(value, i, 1) =
        sv_slice(a, right ? (i + shift) % n : (i + n - shift) % n, 1);
  if (n == 32)
    value = sign_extend(value, 32);
  return 1;
}

void expect_instruction(std::uint64_t pc, std::uint32_t word) {
  retirement_t item;
  std::uint64_t a, b, value, immediate;
  std::int32_t narrow;
  int op, f3, f7;
  bool writes;
  a = model[sv_slice(word, 15, (19) - (15) + 1)];
  b = model[sv_slice(word, 20, (24) - (20) + 1)];
  immediate = sign_extend(word >> 20, 12);
  op = int(sv_slice(word, 0, (6) - (0) + 1));
  f3 = int(sv_slice(word, 12, (14) - (12) + 1));
  f7 = int(sv_slice(word, 25, (31) - (25) + 1));
  writes = 1;
  value = 0;
  if (prefetch_encoding(word))
    expected_prefetch[pc] = {
        a + sign_extend(word >> 25, 7) * 32,
        std::uint8_t((word >> 20 & 31) == 3 ? 3 : ((word >> 20 & 31) + 1) & 3)};
  if (!bitmanip_value(word, a, b, value)) {
    switch (op) {
    case UINT64_C(15): {
      {
        memory_req_t request;
        if (word == UINT64_C(0x100000f))
          writes = 0;
        else {
          CHECK((sv_slice(word, 20, (31) - (20) + 1) == 0 ||
                 sv_slice(word, 20, (31) - (20) + 1) == 1 ||
                 sv_slice(word, 20, (31) - (20) + 1) == 2 ||
                 sv_slice(word, 20, (31) - (20) + 1) == 4) &&
                f3 == 2 && sv_slice(word, 7, (11) - (7) + 1) == 0);
          request = {};
          request.paddress = a;
          request.paccess =
              sv_slice(word, 20, (31) - (20) + 1) == 4
                  ? 6
                  : ((7 + sv_slice(word, 20, (31) - (20) + 1)) & low_mask(4));
          request.pwidth = 3;
          expected_requests.push_back(request);
          writes = 0;
          if (sv_slice(word, 20, (31) - (20) + 1) == 4)
            for (int i = 0; i < 64; i++)
              model_bytes[int(a & ~UINT64_C(0x3f)) + i] = 0;
        }
      }
    } break;
    case UINT64_C(0x73): {
      {
        CHECK(mop_encoding(word));
        value = 0;
      }
    } break;
    case UINT64_C(0x37): {
      value = sign_extend(word & 0xfffff000, 32);
    } break;
    case UINT64_C(23): {
      value = pc + sign_extend(word & 0xfffff000, 32);
    } break;
    case UINT64_C(0x6f):
    case UINT64_C(0x67): {
      value = pc + 4;
    } break;
    case UINT64_C(0x63): {
      writes = 0;
    } break;
    case UINT64_C(3):
    case UINT64_C(0x23): {
      {
        std::uint64_t address;
        int bytes;
        if (op == UINT64_C(0x23))
          immediate = sign_extend(((word >> 25) << 5) | ((word >> 7) & 31), 12);
        address = a + immediate;
        bytes = 1 << (f3 & 3);
        CHECK(address + ((bytes)&low_mask(64)) <= 4096);
        if (address % ((bytes)&low_mask(64)) == 0 &&
            (lookup_mode != 1 || op == UINT64_C(0x23)))
          expect_memory(address, op == UINT64_C(0x23), bytes, b);
        if (op == UINT64_C(0x23)) {
          writes = 0;
          for (int i = 0; i < bytes; i++)
            model_bytes[int(address) + i] = sv_slice(b, i * 8, 8);
        } else {
          for (int i = 0; i < bytes; i++)
            sv_slice(value, i * 8, 8) = model_bytes[int(address) + i];
          if ((f3 & 4) == 0 && bytes < 8 && sv_slice(value, bytes * 8 - 1, 1))
            value |= UINT64_MAX << (bytes * 8);
        }
      }
    } break;
    case UINT64_C(0x2f): {
      {
        memory_req_t request;
        int bytes = 1 << f3;
        CHECK(sv_slice(word, 27, (31) - (27) + 1) == 2);
        request = {};
        request.paddress = a;
        request.paccess = 3;
        request.pwidth = ((f3)&low_mask(2));
        for (int i = 0; i < bytes; i++) {
          sv_slice(request.pmask, int(sv_slice(a, 0, 3)) + i, 1) = 1;
          sv_slice(value, i * 8, 8) = model_bytes[int(a) + i];
        }
        if (bytes == 4)
          value = sign_extend(value, 32);
        expected_requests.push_back(request);
      }
    } break;
    case UINT64_C(19):
    case UINT64_C(27): {
      {
        switch (f3) {
        case 0: {
          value = a + immediate;
        } break;
        case 1: {
          value = a << (op == UINT64_C(27)
                            ? int(sv_slice(word, 20, (24) - (20) + 1))
                            : int(sv_slice(word, 20, (25) - (20) + 1)));
        } break;
        case 2: {
          value = ((std::int64_t(a) < std::int64_t(immediate)) & low_mask(64));
        } break;
        case 3: {
          value = ((a < immediate) & low_mask(64));
        } break;
        case 4: {
          value = a ^ immediate;
        } break;
        case 5: {
          {
            if (op == UINT64_C(27)) {
              narrow = sv_slice(a, 0, (31) - (0) + 1);
              value = sv_slice(word, 30, 1)
                          ? ((narrow >> sv_slice(word, 20, (24) - (20) + 1)) &
                             low_mask(64))
                          : ((sv_slice(a, 0, (31) - (0) + 1) >>
                              sv_slice(word, 20, (24) - (20) + 1)) &
                             low_mask(64));
            } else
              value = sv_slice(word, 30, 1)
                          ? std::uint64_t(std::int64_t(a) >>
                                          sv_slice(word, 20, (25) - (20) + 1))
                          : a >> sv_slice(word, 20, (25) - (20) + 1);
          }
        } break;
        case 6: {
          value = a | immediate;
        } break;
        case 7: {
          value = a & immediate;
        } break;
        }
      }
    } break;
    case UINT64_C(0x33):
    case UINT64_C(0x3b): {
      {
        if (conditional_encoding(word))
          value = ((f3 == 5 && b == 0) || (f3 == 7 && b != 0)) ? 0 : a;
        else if (f7 == 1) {
          signed128 left_wide, right_wide, product;
          if (op == UINT64_C(0x3b)) {
            a = (f3 == 5 || f3 == 7)
                    ? (field(UINT64_C(0), 32, 32) |
                       field(sv_slice(a, 0, (31) - (0) + 1), 32, 0))
                    : sign_extend(a, 32);
            b = (f3 == 5 || f3 == 7)
                    ? (field(UINT64_C(0), 32, 32) |
                       field(sv_slice(b, 0, (31) - (0) + 1), 32, 0))
                    : sign_extend(b, 32);
          }
          left_wide =
              f3 == 3
                  ? signed128((field(UINT64_C(0), 64, 64) | field(a, 64, 0)))
                  : signed128(std::int64_t(a));
          right_wide =
              (f3 == 2 || f3 == 3)
                  ? signed128((field(UINT64_C(0), 64, 64) | field(b, 64, 0)))
                  : signed128(std::int64_t(b));
          product = uint128(left_wide) * uint128(right_wide);
          switch (f3) {
          case 0: {
            value = sv_slice(product, 0, (63) - (0) + 1);
          } break;
          case 1:
          case 2:
          case 3: {
            value = sv_slice(product, 64, (127) - (64) + 1);
          } break;
          case 4: {
            value =
                b == 0
                    ? UINT64_MAX
                    : (a == UINT64_C(0x8000000000000000) && b == UINT64_MAX
                           ? a
                           : std::uint64_t(std::int64_t(a) / std::int64_t(b)));
          } break;
          case 5: {
            value = b == 0 ? UINT64_MAX : a / b;
          } break;
          case 6: {
            value =
                b == 0
                    ? a
                    : (a == UINT64_C(0x8000000000000000) && b == UINT64_MAX
                           ? 0
                           : std::uint64_t(std::int64_t(a) % std::int64_t(b)));
          } break;
          case 7: {
            value = b == 0 ? a : a % b;
          } break;
          }
        } else
          switch (f3) {
          case 0: {
            value = f7 == 32 ? a - b : a + b;
          } break;
          case 1: {
            value = a << (op == UINT64_C(0x3b)
                              ? int(sv_slice(b, 0, (4) - (0) + 1))
                              : int(sv_slice(b, 0, (5) - (0) + 1)));
          } break;
          case 2: {
            value = ((std::int64_t(a) < std::int64_t(b)) & low_mask(64));
          } break;
          case 3: {
            value = ((a < b) & low_mask(64));
          } break;
          case 4: {
            value = a ^ b;
          } break;
          case 5: {
            {
              if (op == UINT64_C(0x3b)) {
                narrow = sv_slice(a, 0, (31) - (0) + 1);
                value = f7 == 32 ? ((narrow >> sv_slice(b, 0, (4) - (0) + 1)) &
                                    low_mask(64))
                                 : ((sv_slice(a, 0, (31) - (0) + 1) >>
                                     sv_slice(b, 0, (4) - (0) + 1)) &
                                    low_mask(64));
              } else
                value = f7 == 32 ? std::uint64_t(std::int64_t(a) >>
                                                 sv_slice(b, 0, (5) - (0) + 1))
                                 : a >> sv_slice(b, 0, (5) - (0) + 1);
            }
          } break;
          case 6: {
            value = a | b;
          } break;
          case 7: {
            value = a & b;
          } break;
          }
      }
    } break;
    default: {
      fail(1, "oracle unsupported instruction %h", word);
    } break;
    }
    if (op == UINT64_C(27) || op == UINT64_C(0x3b))
      value = sign_extend(value, 32);
  }
  item = {};
  item.pfetched.ppc = pc;
  item.pfetched.pinstruction = word;
  item.pfetched.praw_uinstruction = word;
  item.pfetched.psequential_upc = pc + 4;
  item.pfetched.pdirection = direction_metadata(pc, word);
  item.prd = sv_slice(word, 7, (11) - (7) + 1);
  item.pwrite = writes && sv_slice(word, 7, (11) - (7) + 1) != 0;
  item.pdata = value;
  if (item.pwrite)
    model[item.prd] = value;
  expected.push_back(item);
}

std::uint32_t m_insn(int rd, int rs1, int rs2, int funct3,
                     std::uint8_t word = 0) {
  return (field(UINT64_C(1), 7, 25) | field(((rs2)&low_mask(5)), 5, 20) |
          field(((rs1)&low_mask(5)), 5, 15) |
          field(((funct3)&low_mask(3)), 3, 12) |
          field(((rd)&low_mask(5)), 5, 7) |
          field(word ? UINT64_C(0x3b) : UINT64_C(0x33), 7, 0));
}
std::uint32_t atomic_insn(int operation, int width, int rd, int rs1,
                          int rs2 = 0) {
  return (field(((operation)&low_mask(5)), 5, 27) | field(UINT64_C(3), 2, 25) |
          field(((rs2)&low_mask(5)), 5, 20) |
          field(((rs1)&low_mask(5)), 5, 15) |
          field(((width)&low_mask(3)), 3, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(0x2f), 7, 0));
}
std::uint32_t mop_insn(std::uint8_t two_sources, int index, int rd, int rs1,
                       int rs2 = 0) {
  std::uint32_t word;
  if (two_sources)
    word = UINT64_C(0x82004073) | (((index & 4) & low_mask(32)) << 28) |
           (((index & 2) & low_mask(32)) << 26) |
           (((index & 1) & low_mask(32)) << 26) | (((rs2)&low_mask(32)) << 20);
  else
    word = UINT64_C(0x81c04073) | (((index & 16) & low_mask(32)) << 26) |
           (((index & 8) & low_mask(32)) << 24) |
           (((index & 4) & low_mask(32)) << 24) |
           (((index & 3) & low_mask(32)) << 20);
  return word | (((rs1)&low_mask(32)) << 15) | (((rd)&low_mask(32)) << 7);
}
std::uint32_t b_insn(int operation, int rd, int rs1, int rs2, int shift = 0) {
  std::uint32_t word;
  bool binary_source = 1;
  switch (operation) {
  case 0: {
    word = UINT64_C(0x20002033); // SH1ADD
  } break;
  case 1: {
    word = UINT64_C(0x20004033); // SH2ADD
  } break;
  case 2: {
    word = UINT64_C(0x20006033); // SH3ADD
  } break;
  case 3: {
    word = UINT64_C(0x800003b); // ADD.UW
  } break;
  case 4: {
    word = UINT64_C(0x2000203b); // SH1ADD.UW
  } break;
  case 5: {
    word = UINT64_C(0x2000403b); // SH2ADD.UW
  } break;
  case 6: {
    word = UINT64_C(0x2000603b); // SH3ADD.UW
  } break;
  case 7: {
    {
      word = UINT64_C(0x800101b) | (((shift & 63) & low_mask(32)) << 20);
      binary_source = 0;
    } // SLLI.UW
  } break;
  case 8: {
    word = UINT64_C(0x40007033); // ANDN
  } break;
  case 9: {
    word = UINT64_C(0x40006033); // ORN
  } break;
  case 10: {
    word = UINT64_C(0x40004033); // XNOR
  } break;
  case 11: {
    {
      word = UINT64_C(0x60001013);
      binary_source = 0;
    } // CLZ
  } break;
  case 12: {
    {
      word = UINT64_C(0x6000101b);
      binary_source = 0;
    } // CLZW
  } break;
  case 13: {
    {
      word = UINT64_C(0x60101013);
      binary_source = 0;
    } // CTZ
  } break;
  case 14: {
    {
      word = UINT64_C(0x6010101b);
      binary_source = 0;
    } // CTZW
  } break;
  case 15: {
    {
      word = UINT64_C(0x60201013);
      binary_source = 0;
    } // CPOP
  } break;
  case 16: {
    {
      word = UINT64_C(0x6020101b);
      binary_source = 0;
    } // CPOPW
  } break;
  case 17: {
    word = UINT64_C(0xa004033); // MIN
  } break;
  case 18: {
    word = UINT64_C(0xa005033); // MINU
  } break;
  case 19: {
    word = UINT64_C(0xa006033); // MAX
  } break;
  case 20: {
    word = UINT64_C(0xa007033); // MAXU
  } break;
  case 21: {
    {
      word = UINT64_C(0x28705013);
      binary_source = 0;
    } // ORC.B
  } break;
  case 22: {
    {
      word = UINT64_C(0x6b805013);
      binary_source = 0;
    } // REV8
  } break;
  case 23: {
    word = UINT64_C(0x60001033); // ROL
  } break;
  case 24: {
    word = UINT64_C(0x6000103b); // ROLW
  } break;
  case 25: {
    word = UINT64_C(0x60005033); // ROR
  } break;
  case 26: {
    word = UINT64_C(0x6000503b); // RORW
  } break;
  case 27: {
    {
      word = UINT64_C(0x60005013) | (((shift & 63) & low_mask(32)) << 20);
      binary_source = 0;
    } // RORI
  } break;
  case 28: {
    {
      word = UINT64_C(0x6000501b) | (((shift & 31) & low_mask(32)) << 20);
      binary_source = 0;
    } // RORIW
  } break;
  case 29: {
    {
      word = UINT64_C(0x60401013);
      binary_source = 0;
    } // SEXT.B
  } break;
  case 30: {
    {
      word = UINT64_C(0x60501013);
      binary_source = 0;
    } // SEXT.H
  } break;
  case 31: {
    {
      word = UINT64_C(0x800403b);
      binary_source = 0;
    } // ZEXT.H
  } break;
  case 32: {
    word = UINT64_C(0x48001033); // BCLR
  } break;
  case 33: {
    {
      word = UINT64_C(0x48001013) | (((shift & 63) & low_mask(32)) << 20);
      binary_source = 0;
    } // BCLRI
  } break;
  case 34: {
    word = UINT64_C(0x48005033); // BEXT
  } break;
  case 35: {
    {
      word = UINT64_C(0x48005013) | (((shift & 63) & low_mask(32)) << 20);
      binary_source = 0;
    } // BEXTI
  } break;
  case 36: {
    word = UINT64_C(0x68001033); // BINV
  } break;
  case 37: {
    {
      word = UINT64_C(0x68001013) | (((shift & 63) & low_mask(32)) << 20);
      binary_source = 0;
    } // BINVI
  } break;
  case 38: {
    word = UINT64_C(0x28001033); // BSET
  } break;
  case 39: {
    {
      word = UINT64_C(0x28001013) | (((shift & 63) & low_mask(32)) << 20);
      binary_source = 0;
    } // BSETI
  } break;
  default: {
    fail(1, "unknown authored B operation");
  } break;
  }
  return word | (((rs1)&low_mask(32)) << 15) | (((rd)&low_mask(32)) << 7) |
         (binary_source ? ((rs2)&low_mask(32)) << 20 : 0);
}
void send(std::uint64_t pc, std::uint32_t first, std::uint32_t second,
          int count = 2, std::uint8_t keep0 = 1, std::uint8_t keep1 = 1,
          int fetch_fault_lane = -1, prediction_t second_prediction = {},
          prediction_t first_prediction = {});
// Construct arbitrary architectural operands through real instructions_in.
void constant64(std::uint64_t &pc, int rd, std::uint64_t value) {
  send(pc, imm(rd, 0, 0), 0, 1);
  pc += 4;
  for (int byte_index = 7; byte_index >= 0; byte_index--) {
    send(pc, imm(rd, rd, 8, 1),
         imm(rd, rd, int(sv_slice(value, byte_index * 8, 8))));
    pc += 8;
  }
}

void tick() {
  rising();
  settle();
}
void send(std::uint64_t pc, std::uint32_t first, std::uint32_t second,
          int count, std::uint8_t keep0, std::uint8_t keep1,
          int fetch_fault_lane, prediction_t second_prediction,
          prediction_t first_prediction) {
  int expected_index = expected.size();
  if (keep0)
    expect_instruction(pc, first);
  if (count == 2 && keep1)
    expect_instruction(pc + 4, second);
  instructions_in.pvalid = 1;
  instructions_in.pbits.pcount = ((count)&low_mask(2));
  instructions_in.pbits.pentries[0] = {
      .ppc = pc,
      .pinstruction = first,
      .praw_uinstruction = first,
      .psequential_upc = pc + 4,
      .pcompressed_uillegal = 0,
      .pfault = {},
      .pprediction = first_prediction,
  };
  instructions_in.pbits.pentries[1] = {
      .ppc = pc + 4,
      .pinstruction = second,
      .praw_uinstruction = second,
      .psequential_upc = pc + 8,
      .pcompressed_uillegal = 0,
      .pfault = {},
      .pprediction = second_prediction,
  };
  for (int lane = 0; lane < count; lane++) {
    instructions_in.pbits.pentries[lane].pdirection = direction_metadata(
        instructions_in.pbits.pentries[lane].ppc,
        instructions_in.pbits.pentries[lane].pinstruction,
        instructions_in.pbits.pentries[lane].pprediction.pvalid);
    instructions_in.pbits.pentries[lane].pspeculated_uras_uaction =
        instructions_in.pbits.pentries[lane].pprediction.pras_uaction;
    offered_direction[instructions_in.pbits.pentries[lane].ppc] =
        instructions_in.pbits.pentries[lane].pdirection;
    corrected_history.erase(instructions_in.pbits.pentries[lane].ppc);
  }
  if (keep0) {
    expected[expected_index].pfetched.pprediction = first_prediction;
    expected[expected_index].pfetched.pspeculated_uras_uaction =
        first_prediction.pras_uaction;
    expected[expected_index].pfetched.pdirection =
        instructions_in.pbits.pentries[0].pdirection;
  }
  if (count == 2 && keep1) {
    expected_index += keep0 ? 1 : 0;
    expected[expected_index].pfetched.pprediction = second_prediction;
    expected[expected_index].pfetched.pspeculated_uras_uaction =
        second_prediction.pras_uaction;
    expected[expected_index].pfetched.pdirection =
        instructions_in.pbits.pentries[1].pdirection;
  }
  if (fetch_fault_lane >= 0)
    instructions_in.pbits.pentries[fetch_fault_lane].pfault = {
        1, 1, pc + 4 * std::uint64_t(fetch_fault_lane), {}};
  settle();
  while (!instructions_out.pready)
    tick();
  tick();
  instructions_in.pvalid = 0;
}
void drain() {
  instructions_in.pvalid = 0;
  do
    tick();
  while (expected.size() != 0 || expected_redirects.size() != 0 ||
         expected_requests.size() != 0 || expected_completions.size() != 0 ||
         response_count != 0 || split_active || training_pending.size() != 0);
  for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
    tick();
  CHECK(expected.size() == 0 && expected_redirects.size() == 0);
}
void send_predicted(std::uint64_t pc, std::uint32_t first,
                    std::uint64_t predicted_target, std::uint64_t successor_pc,
                    std::uint8_t keep_successor = 1, std::uint8_t action = 0) {
  instruction_t first_token, second_token;
  int index = expected.size();
  expect_instruction(pc, first);
  expected[index].pfetched.pprediction = {UINT64_C(1), pc, predicted_target,
                                          UINT64_C(0), action};
  expected[index].pfetched.pspeculated_uras_uaction = action;
  expected[index].pfetched.pdirection = direction_metadata(pc, first, 1);
  first_token = expected[index].pfetched;
  second_token = {};
  second_token.ppc = successor_pc;
  second_token.pinstruction = imm(12, 0, 42);
  second_token.praw_uinstruction = second_token.pinstruction;
  second_token.psequential_upc = successor_pc + 4;
  second_token.pdirection =
      direction_metadata(successor_pc, second_token.pinstruction);
  offered_direction[pc] = first_token.pdirection;
  offered_direction[successor_pc] = second_token.pdirection;
  if (keep_successor)
    expect_instruction(successor_pc, second_token.pinstruction);
  instructions_in = {UINT64_C(1), {UINT64_C(2), {first_token, second_token}}};
  settle();
  while (!instructions_out.pready)
    tick();
  tick();
  instructions_in.pvalid = 0;
}
void stop_at(std::uint64_t pc, std::uint64_t target, int disposition = 0,
             std::uint64_t cause = 0, std::uint64_t value = 0) {
  redirect_t item;
  item = {.ppc = pc,
          .ptarget = disposition == 1 ? trap_target : target,
          .presolution = {.pguest = {},
                          .pdisposition =
                              static_cast<uint8_t>(((disposition)&low_mask(2))),
                          .pcause = cause,
                          .pvalue = value}};
  expected_redirects.push_back(item);
}
void reset_core() {
  canceled += response_count;
  reset = 1;
  instructions_in = {};
  inject_enable = 0;
  inject_pc = 0;
  inject_result = {};
  set_interrupts(0);
  time_counter = 123;
  trap_target = 0;
  reservation_valid = 0;
  block_requests = 0;
  block_stores = 0;
  inject_memory_fault = 0;
  hold_responses = 0;
  lookup_mode = 0;
  hold_split = 0;
  split_fault = 0;
  expected_split_locality = 0;
  expected.clear();
  expected_redirects.clear();
  expected_requests.clear();
  expected_completions.clear();
  response_owners.clear();
  multiply_authorized_cycle.clear();
  expected_branch_taken.clear();
  training_pending.clear();
  for (int i = 0; i < 32; i++)
    model[i] = 0;
  for (int i = 0; i < 4096; i++) {
    memory_bytes[i] = ((i ^ UINT64_C(0x98)) & low_mask(8));
    model_bytes[i] = memory_bytes[i];
  }
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
    tick();
  reset = 0;
}

std::uint32_t csr(int f3, int rd, int rs1, int address) {
  return (field(((address)&low_mask(12)), 12, 20) |
          field(((rs1)&low_mask(5)), 5, 15) | field(((f3)&low_mask(3)), 3, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(0x73), 7, 0));
}
void expect_system(std::uint64_t pc, std::uint32_t word,
                   std::uint64_t value = 0) {
  retirement_t item;
  item = {};
  item.pfetched.ppc = pc;
  item.pfetched.pinstruction = word;
  item.pfetched.praw_uinstruction = word;
  item.pfetched.psequential_upc = pc + 4;
  item.pfetched.pdirection = direction_metadata(pc, word);
  item.prd = sv_slice(word, 7, (11) - (7) + 1);
  item.pwrite = sv_slice(word, 12, (14) - (12) + 1) != 0 && item.prd != 0;
  item.pdata = value;
  if (item.pwrite)
    model[item.prd] = value;
  expected.push_back(item);
}
void csr_access(std::uint64_t pc, int f3, int rd, int rs1, int address,
                std::uint64_t old_value) {
  std::uint32_t word = csr(f3, rd, rs1, address);
  expect_system(pc, word, old_value);
  stop_at(pc, pc + 4, 3);
  send(pc, word, 0, 1, 0, 0);
  drain();
}

// Enter S through real M-mode CSR programming; no internal state pokes.
void supervisor_timer(std::uint8_t global_enable) {
  std::uint64_t pc = UINT64_C(0x14000);
  time_counter = timer_compare - 1;
  constant64(pc, 1, timer_compare);
  drain();
  csr_access(pc, 1, 0, 1, UINT64_C(0x14d), 0);
  pc += 4;
  constant64(pc, 1, UINT64_C(0x8000000000000000));
  drain();
  csr_access(pc, 1, 0, 1, UINT64_C(0x30a), 0);
  pc += 4;
  csr_access(pc, 5, 0, 2, UINT64_C(0x306), 0);
  pc += 4;
  send(pc, imm(2, 0, 32), imm(3, 0, UINT64_C(0x700)));
  pc += 8;
  drain();
  csr_access(pc, 1, 0, 2, UINT64_C(0x303), 0);
  pc += 4;
  csr_access(pc, 1, 0, 2, UINT64_C(0x304), 0);
  pc += 4;
  csr_access(pc, 1, 0, 3, UINT64_C(0x105), 0);
  pc += 4;
  constant64(pc, 1,
             UINT64_C(0x800) | (global_enable ? UINT64_C(2) : UINT64_C(0)));
  drain();
  csr_access(pc, 1, 0, 1, UINT64_C(0x300), UINT64_C(0xa00000000));
  pc += 4;
  send(pc, imm(4, 0, UINT64_C(0x500)), 0, 1);
  pc += 4;
  drain();
  csr_access(pc, 1, 0, 4, UINT64_C(0x341), 0);
  pc += 4;
  expect_system(pc, UINT64_C(0x30200073));
  stop_at(pc, UINT64_C(0x500), 3);
  send(pc, UINT64_C(0x30200073), 0, 1, 0, 0);
  drain();
  trap_target = UINT64_C(0x700);
}

int main() {
  return run_test([] {
    reset = 1;
    time_counter = 123;
    hart_id = 7;
    reset_core();
    // Independent pairs must sustain full bandwidth rather than merely finish correctly.
    for (int i = 0; i < 20; i++)
      send(((UINT64_C(0x100) + i * 8) & low_mask(64)), imm(1, 0, i),
           imm(2, 0, -i));
    drain();
    CHECK(longest_dual_run >= 16);

    // Partial packets coalesce, and a dependency can forward from either EX lane.
    send(UINT64_C(0x200), imm(5, 0, 7), regop(6, 5, 5));
    send(UINT64_C(0x208), imm(7, 0, 19), regop(8, 7, 6));
    send(UINT64_C(0x210), imm(9, 0, -1), imm(10, 0, 31));
    send(UINT64_C(0x218), regop(11, 9, 10, 5, 32), regop(12, 10, 9));
    drain();
    CHECK(saw_repacked);

    // Same-group WAW pairs; youngest of several older writers wins subsequent RAW.
    {
      int before_waw;
      before_waw = waw_dual;
      send(UINT64_C(0x2f0), imm(15, 0, 1), imm(15, 0, 2));
      send(UINT64_C(0x2f8), imm(16, 15, 0), imm(17, 15, 1));
      drain();
      CHECK(waw_dual == before_waw + 1);
      // Read again after all forwarding candidates have drained: younger won RF.
      send(UINT64_C(0x2fc), imm(16, 15, 0), imm(17, 15, 1));
      drain();
    }
    send(UINT64_C(0x300), imm(15, 0, 1), imm(15, 0, 2));
    send(UINT64_C(0x308), imm(15, 0, 3), imm(16, 15, 1));
    send(UINT64_C(0x310), imm(0, 15, 9), imm(17, 0, 4));
    drain();

    // Every selected ALU family, including six-bit shifts and RV64 word sign extension.
    send(UINT64_C(0x400),
         (field(UINT64_C(0x80000), 20, 12) | field(UINT64_C(18), 5, 7) |
          field(UINT64_C(0x37), 7, 0)),
         (field(UINT64_C(0xfffff), 20, 12) | field(UINT64_C(19), 5, 7) |
          field(UINT64_C(23), 7, 0)));
    for (int f3 = 0; f3 < 8; f3++) {
      send(((UINT64_C(0x408) + f3 * 16) & low_mask(64)),
           imm(20, 18, f3 == 1 || f3 == 5 ? 40 : -7, f3),
           regop(21, 18, 19, f3));
      send(((UINT64_C(0x410) + f3 * 16) & low_mask(64)),
           regop(22, 18, 19, f3 == 0 ? 0 : 5, 32),
           imm(23, 18, UINT64_C(0x428), 5));
    }
    send(UINT64_C(0x500), imm(24, 18, -1, 0, UINT64_C(27)),
         regop(25, 18, 19, 0, 0, UINT64_C(0x3b)));
    send(UINT64_C(0x508), imm(24, 18, 7, 1, UINT64_C(27)),
         regop(25, 18, 19, 1, 0, UINT64_C(0x3b)));
    send(UINT64_C(0x510), imm(24, 18, 7, 5, UINT64_C(27)),
         regop(25, 18, 19, 5, 0, UINT64_C(0x3b)));
    send(UINT64_C(0x518), imm(24, 18, UINT64_C(0x407), 5, UINT64_C(27)),
         regop(25, 18, 19, 5, 32, UINT64_C(0x3b)));
    send(UINT64_C(0x520), regop(24, 18, 19, 0, 32, UINT64_C(0x3b)),
         imm(25, 24, 1));
    drain();

    // Seeded dependency-heavy arithmetic checks against an independent sequential model.
    {
      std::uint32_t random_state;
      std::uint32_t words[2];
      random_state = UINT64_C(0x126789ab);
      for (int i = 0; i < 160; i++) {
        for (int lane = 0; lane < 2; lane++) {
          random_state =
              random_state * UINT64_C(0x19660d) + UINT64_C(0x3c6ef35f);
          words[lane] = regop(int(sv_slice(random_state, 0, (4) - (0) + 1)),
                              int(sv_slice(random_state, 5, (9) - (5) + 1)),
                              int(sv_slice(random_state, 10, (14) - (10) + 1)),
                              int(sv_slice(random_state, 15, (17) - (15) + 1)));
        }
        send(((UINT64_C(0x1000) + 8 * i) & low_mask(64)), words[0], words[1]);
      }
    }
    drain();

    reset_core();
    // Both branch positions; younger already-issued and queued work is discarded.
    stop_at(UINT64_C(0x2000), UINT64_C(0x2040));
    send(UINT64_C(0x2000), jump(1, 64), imm(2, 0, 99), 2, 1, 0);
    send(UINT64_C(0x2008), imm(3, 0, 99), imm(4, 0, 99), 2, 0, 0);
    drain();
    send(UINT64_C(0x2040), regop(5, 2, 3), imm(6, 1, 0));
    stop_at(UINT64_C(0x204c), UINT64_C(0x208c));
    send(UINT64_C(0x2048), imm(7, 0, 7), jump(8, 64));
    send(UINT64_C(0x2050), imm(9, 0, 99), imm(10, 0, 99), 2, 0, 0);
    drain();
    send(UINT64_C(0x208c), regop(11, 9, 10), regop(12, 7, 8));
    drain();

    // All branch predicates, taken and not taken, with negative versus positive operands.
    send(UINT64_C(0x2100), imm(1, 0, -1), imm(2, 0, 1));
    drain();
    for (int f3 = 0; f3 < 8; f3++) {
      if (f3 != 2 && f3 != 3) {
        bool taken;
        taken = f3 == 1 || f3 == 4 || f3 == 7;
        if (taken)
          stop_at(((UINT64_C(0x2200) + 16 * f3) & low_mask(64)),
                  ((UINT64_C(0x2220) + 16 * f3) & low_mask(64)));
        send(((UINT64_C(0x2200) + 16 * f3) & low_mask(64)),
             branch(1, 2, 32, f3), imm(3, 0, f3), 2, 1, !taken);
        drain();
        if (!taken)
          stop_at(((UINT64_C(0x2304) + 16 * f3) & low_mask(64)),
                  ((UINT64_C(0x2324) + 16 * f3) & low_mask(64)));
        send(((UINT64_C(0x2300) + 16 * f3) & low_mask(64)), imm(4, 0, f3),
             branch(2, 1, 32,
                    f3 == 0   ? 1
                    : f3 == 1 ? 0
                              : f3));
        drain();
      }
    }
    // Correct taken predictions retain a paired target instruction. Wrong
    // direction/target predictions kill that peer and recover exactly once.
    send_predicted(UINT64_C(0x2600), jump(0, 32), UINT64_C(0x2620),
                   UINT64_C(0x2620));
    drain();
    send_predicted(UINT64_C(0x2640), jump(1, 32), UINT64_C(0x2660),
                   UINT64_C(0x2660), 1, UINT64_C(1));
    drain();
    stop_at(UINT64_C(0x2680), UINT64_C(0x26a0));
    send_predicted(UINT64_C(0x2680), jump(0, 32), UINT64_C(0x26c0),
                   UINT64_C(0x26c0), 0);
    drain();
    stop_at(UINT64_C(0x26e0), UINT64_C(0x26e4));
    send_predicted(UINT64_C(0x26e0), branch(1, 2, 32), UINT64_C(0x2700),
                   UINT64_C(0x2700), 0);
    drain();
    // Next-PC accuracy alone cannot detect this direction mismatch: the taken
    // target equals fallthrough, but history must still append the actual one.
    stop_at(UINT64_C(0x2720), UINT64_C(0x2724));
    send(UINT64_C(0x2720), branch(0, 0, 4), imm(3, 0, 99), 2, 1, 0);
    drain();
    // JALR clears bit zero, and link data is available to the restarted stream.
    send(UINT64_C(0x2400), imm(5, 0, UINT64_C(0x601)), imm(6, 0, 9));
    drain();
    stop_at(UINT64_C(0x2408), UINT64_C(0x600));
    send(UINT64_C(0x2408), imm(7, 5, 0, 0, UINT64_C(0x67)), imm(8, 0, 99), 2, 1,
         0);
    drain();
    send(UINT64_C(0x600), imm(8, 7, 0), imm(9, 0, 10));
    drain();
    stop_at(UINT64_C(0x24d4), UINT64_C(0x24e4));
    send(UINT64_C(0x24d0), branch(1, 2, 32), branch(1, 2, 16, 1));
    drain();

    // C permits halfword-aligned jump targets; only odd instruction PCs fault.
    stop_at(UINT64_C(0x2500), UINT64_C(0x2502));
    send(UINT64_C(0x2500), jump(10, 2), imm(11, 0, 99), 2, 1, 0);
    drain();
    stop_at(UINT64_C(0x2510), UINT64_C(0x2510), 1, 2, UINT64_C(0xffffffff));
    send(UINT64_C(0x2510), UINT64_C(0xffffffff), imm(11, 0, 99), 2, 0, 0);
    drain();
    stop_at(UINT64_C(0x2530), UINT64_C(0x602));
    send(UINT64_C(0x2530), imm(10, 5, 2, 0, UINT64_C(0x67)), {}, 1);
    drain();
    stop_at(UINT64_C(0x2543), UINT64_C(0x2543), 1, 0, UINT64_C(0x2543));
    send(UINT64_C(0x2543), imm(10, 0, 1), {}, 1, 0, 0);
    drain();
    inject_enable = 1;
    inject_pc = UINT64_C(0x2550);
    inject_result = {.pguest = {},
                     .pdisposition = UINT64_C(1),
                     .pcause = UINT64_C(5),
                     .pvalue = UINT64_C(0xdead)};
    stop_at(UINT64_C(0x2550), UINT64_C(0x2550), 1, 2, UINT64_C(0xffffffff));
    send(UINT64_C(0x2550), UINT64_C(0xffffffff), {}, 1, 0, 0);
    drain();
    inject_enable = 0;
    for (int lane = 0; lane < 2; lane++) {
      for (int action = 1; action <= 2; action++) {
        std::uint64_t pc;
        pc = ((UINT64_C(0x2600) + 64 * lane + 16 * action) & low_mask(64));
        inject_enable = 1;
        inject_pc = pc + ((4 * lane) & low_mask(64));
        inject_result = {.pguest = {},
                         .pdisposition =
                             static_cast<uint8_t>(((action)&low_mask(2))),
                         .pcause = UINT64_C(13),
                         .pvalue = UINT64_C(0x3000)};
        stop_at(inject_pc, inject_pc, action, 13, UINT64_C(0x3000));
        send(pc, imm(12, 0, 101), imm(13, 0, 102), 2, lane == 1, 0);
        if (action == 1)
          tick();
        send(pc + 8, imm(14, 12, 1), imm(15, 13, 1), 2, 0, 0);
        if (action == 1) {
          settle();
          CHECK(issued == ((lane)&low_mask(2)));
        }
        drain();
        inject_enable = 0;
        if (action == 2) {
          if (lane == 0)
            send(pc, imm(12, 0, 101), imm(13, 0, 102));
          else
            send(pc + 4, imm(13, 0, 102), {}, 1);
          drain();
        }
        send(pc + 12, imm(14, 12, 0), imm(15, 13, 0));
        drain();
      }
    }

    // An older injected fault wins over a younger taken branch in the same pair.
    inject_enable = 1;
    inject_pc = UINT64_C(0x2800);
    inject_result = {.pguest = {},
                     .pdisposition = UINT64_C(1),
                     .pcause = UINT64_C(5),
                     .pvalue = UINT64_C(0xdead)};
    stop_at(UINT64_C(0x2800), UINT64_C(0x2800), 1, 5, UINT64_C(0xdead));
    send(UINT64_C(0x2800), imm(16, 0, 1), jump(17, 64), 2, 0, 0);
    drain();
    inject_enable = 0;

    // Reset discards queued/pipelined work without a late write or retirement.
    send(UINT64_C(0x2900), imm(20, 0, 99), imm(21, 0, 99), 2, 0, 0);
    tick();
    reset_core();
    drain();
    send(UINT64_C(0x3000), imm(22, 20, 0), imm(23, 21, 0));
    drain();
    CHECK(single_issues > 10 && stops == 23);

    // Warm loads use the normal MEM/WB path and sustain one LSU plus one ALU each cycle.
    reset_core();
    lookup_mode = 1;
    send(UINT64_C(0x4000), imm(1, 0, UINT64_C(0x300)), imm(2, 0, -128));
    drain();
    dual_run = 0;
    longest_dual_run = 0;
    for (int i = 0; i < 20; i++)
      send(((UINT64_C(0x4010) + i * 8) & low_mask(64)),
           imm(10 + i, 1, (i % 8) * 8, 3, UINT64_C(3)), imm(30, 0, i));
    drain();
    CHECK(longest_dual_run >= 16);

    // Every natural byte lane, store mask, signed/unsigned load width, and both age slots.
    for (int width = 0; width < 4; width++) {
      for (int offset = 0; offset < 8; offset += 1 << width) {
        send(((UINT64_C(0x4200) + width * 128 + offset * 8) & low_mask(64)),
             store(2, 1, offset, width), imm(3, 0, offset));
        drain();
        send(((UINT64_C(0x4500) + width * 128 + offset * 8) & low_mask(64)),
             imm(4, 0, width), imm(5, 1, offset, width, UINT64_C(3)));
        drain();
        send(UINT64_C(0x4780), imm(6, 1, offset, width, UINT64_C(3)),
             imm(7, 6, 1));
        drain();
        if (width < 3) {
          send(UINT64_C(0x4790), imm(8, 1, offset, width + 4, UINT64_C(3)), {},
               1);
          drain();
        }
      }
    }
    send(UINT64_C(0x4800), imm(3, 0, 3), store(2, 1, -8, 3));
    drain();
    send(UINT64_C(0x4808), imm(4, 1, -8, 3, UINT64_C(3)),
         imm(5, 1, 0, 3, UINT64_C(3)));
    drain();
    send(UINT64_C(0x4810), imm(0, 1, 0, 3, UINT64_C(3)), imm(6, 0, 7));
    drain();

    // Several accepted loads outlive WB; responses reserve younger issue while the older lane continues.
    lookup_mode = 0;
    configured_delay = 4;
    hold_responses = 1;
    for (int i = 0; i < 4; i++)
      send(((UINT64_C(0x4900) + i * 8) & low_mask(64)),
           imm(10 + i, 1, i * 8, 3, UINT64_C(3)), imm(20 + i, 0, i));
    for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
      tick();
    CHECK(response_count == 4);
    for (int i = 0; i < 16; i++) {
      if (i == 8)
        hold_responses = 0;
      send(((UINT64_C(0x4940) + i * 8) & low_mask(64)), imm(25, 0, i),
           imm(26, 0, -i));
    }
    drain();
    CHECK(max_outstanding == 4 && overlap_retirements > 8 &&
          shared_writes > 0 && reserved_slots >= 4);

    // A hit may bypass an older outstanding miss; RAW and WAW must still wait for its owner.
    hold_responses = 1;
    send(UINT64_C(0x4a00), imm(10, 1, 0, 3, UINT64_C(3)), imm(20, 0, 1));
    for (unsigned repeat_index = 0; repeat_index < (6); ++repeat_index)
      tick();
    lookup_mode = 1;
    send(UINT64_C(0x4a08), imm(11, 1, 8, 3, UINT64_C(3)), imm(21, 0, 2));
    for (unsigned repeat_index = 0; repeat_index < (6); ++repeat_index)
      tick();
    CHECK(expected.size() == 0 && response_count == 1);
    send(UINT64_C(0x4a10), imm(12, 10, 1), imm(13, 0, 3));
    for (int repeat_index = 0; repeat_index < (6); ++repeat_index) {
      tick();
      CHECK(issued == 0);
    }
    hold_responses = 0;
    drain();
    lookup_mode = 0;
    hold_responses = 1;
    send(UINT64_C(0x4a20), imm(10, 1, 0, 3, UINT64_C(3)), imm(20, 0, 1));
    for (unsigned repeat_index = 0; repeat_index < (6); ++repeat_index)
      tick();
    send(UINT64_C(0x4a28), imm(10, 0, 91), imm(13, 0, 3));
    for (int repeat_index = 0; repeat_index < (6); ++repeat_index) {
      tick();
      CHECK(issued == 0);
    }
    hold_responses = 0;
    drain();
    send(UINT64_C(0x4a30), imm(14, 10, 0), {}, 1);
    drain();

    // FIFO capacity failure replays only the unaccepted instruction; prior owners survive.
    hold_responses = 1;
    for (int i = 0; i < 4; i++)
      send(((UINT64_C(0x4b00) + i * 8) & low_mask(64)),
           imm(10 + i, 1, i * 8, 3, UINT64_C(3)), imm(20 + i, 0, i));
    for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
      tick();
    stop_at(UINT64_C(0x4b44), UINT64_C(0x4b44), 2);
    send(UINT64_C(0x4b40), imm(24, 0, 42), imm(14, 1, 32, 3, UINT64_C(3)), 2, 1,
         0);
    for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
      tick();
    CHECK(expected_redirects.size() == 0 && response_count == 4);
    hold_responses = 0;
    drain();
    send(UINT64_C(0x4b44), imm(14, 1, 32, 3, UINT64_C(3)), {}, 1);
    drain();

    // WB slow-request and hit-store rejection replay, with no accepted request or mutation.
    for (int lane = 0; lane < 2; lane++) {
      std::uint64_t pc;
      pc = ((UINT64_C(0x4c00) + lane * 32) & low_mask(64));
      block_requests = 1;
      stop_at(pc + ((lane * 4) & low_mask(64)),
              pc + ((lane * 4) & low_mask(64)), 2);
      if (lane == 0)
        send(pc, imm(15, 1, 0, 3, UINT64_C(3)), imm(16, 0, 99), 2, 0, 0);
      else
        send(pc, imm(16, 0, 8), store(2, 1, 0, 3), 2, 1, 0);
      drain();
      block_requests = 0;
      if (lane == 0)
        send(pc, imm(15, 1, 0, 3, UINT64_C(3)), imm(16, 0, 99));
      else
        send(pc + 4, store(2, 1, 0, 3), {}, 1);
      drain();
    }
    lookup_mode = 1;
    block_stores = 1;
    stop_at(UINT64_C(0x4c84), UINT64_C(0x4c84), 2);
    send(UINT64_C(0x4c80), imm(16, 0, 8), store(2, 1, 0, 3), 2, 1, 0);
    drain();
    block_stores = 0;
    send(UINT64_C(0x4c84), store(2, 1, 0, 3), {}, 1);
    drain();

    // Lookup replay/translation faults and admission access faults never issue slow work.
    for (int mode = 3; mode <= 5; mode++) {
      for (int lane = 0; lane < 2; lane++) {
        std::uint64_t pc;
        int cause;
        pc = ((UINT64_C(0x4d00) + mode * 32 + lane * 8) & low_mask(64));
        lookup_mode = mode;
        cause = mode == 4 ? (lane == 0 ? 13 : 15) : (lane == 0 ? 5 : 7);
        stop_at(pc + ((lane * 4) & low_mask(64)),
                pc + ((lane * 4) & low_mask(64)), mode == 3 ? 2 : 1,
                ((cause)&low_mask(64)), UINT64_C(0x300));
        if (lane == 0)
          send(pc, imm(0, 1, 0, 3, UINT64_C(3)), imm(16, 0, 99), 2, 0, 0);
        else
          send(pc, imm(16, 0, 8), store(2, 1, 0, 3), 2, 1, 0);
        drain();
      }
    }
    lookup_mode = 0;
    inject_memory_fault = 1;
    fault_address = UINT64_C(0x300);
    stop_at(UINT64_C(0x4e00), UINT64_C(0x4e00), 1, 5, UINT64_C(0x300));
    send(UINT64_C(0x4e00), imm(0, 1, 0, 3, UINT64_C(3)), imm(16, 0, 99), 2, 0,
         0);
    drain();
    stop_at(UINT64_C(0x4e14), UINT64_C(0x4e14), 1, 7, UINT64_C(0x300));
    send(UINT64_C(0x4e10), imm(16, 0, 8), store(2, 1, 0, 3), 2, 1, 0);
    drain();
    inject_memory_fault = 0;

    // Deferred responses use the same lane extraction and sign extension as hits.
    for (int f3 = 0; f3 < 7; f3++) {
      int offset;
      offset = f3 == 0 || f3 == 4   ? 7
               : f3 == 1 || f3 == 5 ? 6
               : f3 == 3            ? 0
                                    : 4;
      send(((UINT64_C(0x4e20) + f3 * 8) & low_mask(64)),
           imm(15, 1, offset, f3, UINT64_C(3)), imm(16, 15, 1));
      drain();
    }
    send(UINT64_C(0x4e90), imm(0, 1, 0, 3, UINT64_C(3)), store(2, 1, 7, 0));
    drain();

    // Split owners retire once after completion, preserving an older peer and
    // refetching younger work without adding a third RF write port.
    {
      int before_splits;
      before_splits = split_requests;
      for (int width = 1; width < 4; width++) {
        stop_at(UINT64_C(0x4e40), UINT64_C(0x4e44), 3);
        send(UINT64_C(0x4e40), imm(0, 1, 1, width, UINT64_C(3)), imm(16, 0, 99),
             2, 1, 0);
        drain();
        stop_at(UINT64_C(0x4e54), UINT64_C(0x4e58), 3);
        send(UINT64_C(0x4e50), imm(16, 0, 8), store(2, 1, 1, width));
        drain();
        stop_at(UINT64_C(0x4e60), UINT64_C(0x4e64), 3);
        send(UINT64_C(0x4e60), imm(17, 1, 1, width, UINT64_C(3)), 0, 1);
        drain();
        send(UINT64_C(0x4e64), imm(18, 17, 1), 0, 1);
        drain();
      }
      CHECK(split_requests == before_splits + 9);
      // The late outcome carries the exact second-fragment fault address.
      split_fault = 1;
      stop_at(UINT64_C(0x4e74), UINT64_C(0x4e74), 1, 13, UINT64_C(0x304));
      send(UINT64_C(0x4e70), imm(16, 0, 11), imm(17, 1, 1, 3, UINT64_C(3)), 2,
           1, 0);
      drain();
      split_fault = 0;
      hold_responses = 1;
      before_splits = split_requests;
      send(UINT64_C(0x4e80), imm(19, 1, 0, 3, UINT64_C(3)), 0, 1);
      stop_at(UINT64_C(0x4e84), UINT64_C(0x4e88), 3);
      send(UINT64_C(0x4e84), imm(0, 1, 7, 3, UINT64_C(3)), imm(16, 0, 99), 2, 1,
           0);
      for (unsigned repeat_index = 0; repeat_index < (14); ++repeat_index)
        tick();
      CHECK(response_count == 1 && split_requests == before_splits);
      hold_responses = 0;
      drain();
      before_splits = split_requests;
      stop_at(UINT64_C(0x4e88), UINT64_C(0x4ea8));
      send(UINT64_C(0x4e88), jump(0, 32), store(2, 1, 7, 3), 2, 1, 0);
      drain();
      CHECK(split_requests == before_splits);
    }

    // LR uses load fault classes; SC/AMO use store classes, including x0.
    // Both issue positions preserve a successful older peer and suppress younger work.
    for (int op = 0; op < 3; op++)
      for (int width = 2; width <= 3; width++)
        for (int lane = 0; lane < 2; lane++) {
          std::uint32_t word;
          std::uint64_t pc;
          int operation, cause;
          operation = op == 0 ? 2 : op == 1 ? 3 : 0;
          word = atomic_insn(operation, width, 0, 1, op == 0 ? 0 : 2);
          pc = ((UINT64_C(0xb000) + op * 128 + width * 32 + lane * 8) &
                low_mask(64));
          inject_memory_fault = 1;
          fault_address = UINT64_C(0x300);
          stop_at(pc + ((4 * lane) & low_mask(64)), pc, 1, op == 0 ? 5 : 7,
                  UINT64_C(0x300));
          send(pc, lane == 0 ? word : imm(16, 0, 8),
               lane == 0 ? imm(16, 0, 99) : word, 2, lane == 1, 0);
          drain();
          inject_memory_fault = 0;
          send(pc + 8, imm(1, 1, 1), 0, 1);
          drain();
          cause = op == 0 ? 4 : 6;
          stop_at(pc + 16 + ((4 * lane) & low_mask(64)), pc, 1,
                  ((cause)&low_mask(64)), UINT64_C(0x301));
          send(pc + 16, lane == 0 ? word : imm(16, 0, 8),
               lane == 0 ? imm(16, 0, 99) : word, 2, lane == 1, 0);
          drain();
          send(pc + 24, imm(1, 1, -1), 0, 1);
          drain();
        }
    // A successful older LR retains its owner and GPR result until drain,
    // even when the younger peer faults and flushes the speculative pipeline.
    configured_delay = 20;
    stop_at(UINT64_C(0xb384), UINT64_C(0xb384), 1, 2, UINT64_C(0xffffffff));
    send(UINT64_C(0xb380), atomic_insn(2, 2, 17, 1), UINT64_C(0xffffffff), 2, 1,
         0);
    drain();
    configured_delay = 8;
    send(UINT64_C(0xb388), imm(18, 17, 0), 0, 1);
    drain();
    // Taken older branches kill younger atomics before any memory authorization.
    for (int op = 0; op < 3; op++) {
      std::uint64_t pc;
      pc = ((UINT64_C(0xb400) + op * 16) & low_mask(64));
      stop_at(pc, pc + 32);
      send(pc, jump(16, 32),
           atomic_insn(op == 0   ? 2
                       : op == 1 ? 3
                                 : 0,
                       3, 0, 1, op == 0 ? 0 : 2),
           2, 1, 0);
      drain();
    }

    // An older stop prevents a younger store, including a speculative owned-store candidate.
    lookup_mode = 1;
    stop_at(UINT64_C(0x4f00), UINT64_C(0x4f20));
    send(UINT64_C(0x4f00), jump(16, 32), store(2, 1, 0, 3), 2, 1, 0);
    drain();
    inject_enable = 1;
    inject_pc = UINT64_C(0x4f40);
    inject_result = {.pguest = {},
                     .pdisposition = UINT64_C(1),
                     .pcause = UINT64_C(2),
                     .pvalue = UINT64_C(0xdead)};
    stop_at(UINT64_C(0x4f40), UINT64_C(0x4f40), 1, 2, UINT64_C(0xdead));
    send(UINT64_C(0x4f40), imm(16, 0, 9), store(2, 1, 0, 3), 2, 0, 0);
    drain();
    inject_enable = 0;

    // A younger branch flush must not cancel its older accepted load.
    lookup_mode = 0;
    hold_responses = 1;
    stop_at(UINT64_C(0x5004), UINT64_C(0x5044));
    send(UINT64_C(0x5000), imm(10, 1, 0, 3, UINT64_C(3)), jump(17, 64));
    for (unsigned repeat_index = 0; repeat_index < (10); ++repeat_index)
      tick();
    CHECK(expected_redirects.size() == 0 && response_count == 1);
    send(UINT64_C(0x5044), imm(18, 0, 5), imm(19, 0, 6));
    for (unsigned repeat_index = 0; repeat_index < (6); ++repeat_index)
      tick();
    hold_responses = 0;
    drain();

    // Return and branch enter RR together: MEM recovery preserves the reserved completion.
    hold_responses = 1;
    send(UINT64_C(0x5080), imm(10, 1, 0, 3, UINT64_C(3)), {}, 1);
    for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
      tick();
    stop_at(UINT64_C(0x5088), UINT64_C(0x50c8));
    send(UINT64_C(0x5088), jump(17, 64), imm(20, 0, 99), 2, 1, 0);
    hold_responses = 0;
    drain();

    // A younger fault drains older accepted work, including same-group WB acceptance.
    hold_responses = 1;
    inject_enable = 1;
    inject_pc = UINT64_C(0x5104);
    inject_result = {.pguest = {},
                     .pdisposition = UINT64_C(1),
                     .pcause = UINT64_C(2),
                     .pvalue = UINT64_C(0xbad)};
    stop_at(UINT64_C(0x5104), UINT64_C(0x5104), 1, 2, UINT64_C(0xbad));
    send(UINT64_C(0x5100), imm(11, 1, 8, 3, UINT64_C(3)), imm(20, 0, 7), 2, 1,
         0);
    for (unsigned repeat_index = 0; repeat_index < (10); ++repeat_index)
      tick();
    CHECK(expected_redirects.size() == 1 && response_count == 1 &&
          expected.size() == 0);
    hold_responses = 0;
    drain();
    inject_enable = 0;

    // Fetch faults ignore even valid memory encodings and preserve both age slots.
    // In particular, a younger fault must wait for its older accepted load.
    {
      int before_lookups;
      before_lookups = lookups;
      stop_at(UINT64_C(0x5140), UINT64_C(0x5140), 1, 1, UINT64_C(0x5140));
      send(UINT64_C(0x5140), imm(11, 1, 0, 3, UINT64_C(3)), {}, 1, 0, 0, 0);
      drain();
      CHECK(lookups == before_lookups);
    }
    hold_responses = 1;
    stop_at(UINT64_C(0x5184), UINT64_C(0x5184), 1, 1, UINT64_C(0x5184));
    send(UINT64_C(0x5180), imm(11, 1, 8, 3, UINT64_C(3)), store(2, 1, 0, 3), 2,
         1, 0, 1);
    for (unsigned repeat_index = 0; repeat_index < (10); ++repeat_index)
      tick();
    CHECK(expected_redirects.size() == 1 && response_count == 1 &&
          expected.size() == 0);
    hold_responses = 0;
    drain();

    // An older WB fault overrides a younger MEM branch on the same edge.
    stop_at(UINT64_C(0x51a0), UINT64_C(0x51a0), 1, 2, UINT64_C(0xffffffff));
    send(UINT64_C(0x51a0), UINT64_C(0xffffffff), {}, 1, 0, 0);
    send(UINT64_C(0x51a4), jump(17, 64), {}, 1, 0, 0);
    drain();
    CHECK(wb_overrides > 0);

    // Same-group WB authorization can override the prior cycle's MEM branch.
    // No link write or retirement survives the older memory replay/fault.
    for (int mode = 0; mode < 2; mode++) {
      std::uint64_t pc;
      pc = ((UINT64_C(0x51c0) + mode * 16) & low_mask(64));
      lookup_mode = 0;
      block_requests = mode == 0;
      inject_memory_fault = mode == 1;
      fault_address = 0;
      stop_at(pc + 4, pc + 68);
      stop_at(pc, pc, mode == 0 ? 2 : 1, 5, 0);
      send(pc, imm(11, 0, 0, 3, UINT64_C(3)), jump(17, 64), 2, 0, 0);
      drain();
      block_requests = 0;
      inject_memory_fault = 0;
    }
    CHECK(mem_branch_redirects > 10);
    CHECK(minimum_instruction_capacity == 0);

    // Reset is an epoch boundary for both core owners and the memory service.
    hold_responses = 1;
    send(UINT64_C(0x5200), imm(12, 1, 0, 3, UINT64_C(3)), {}, 1);
    for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
      tick();
    CHECK(response_count == 1);
    reset_core();
    drain();
    send(UINT64_C(0x5210), imm(13, 12, 1), imm(14, 0, 1));
    drain();
    CHECK(stores > 10 && hits > 10 && stall_cycles >= 2);
    CHECK(requests == responses + canceled && canceled == 1);

    // An unaligned CBO owns its response across branch recovery and blocks
    // younger memory, but does not reserve or write any integer destination.
    reset_core();
    send(UINT64_C(0x5800), imm(1, 0, 67), imm(2, 0, 7));
    drain();
    hold_responses = 1;
    send(UINT64_C(0x5808), UINT64_C(0x40a00f), imm(3, 0, 9));
    for (unsigned repeat_index = 0; repeat_index < (10); ++repeat_index)
      tick();
    CHECK(response_count == 1 && expected.size() == 0);
    stop_at(UINT64_C(0x5810), UINT64_C(0x5850));
    send(UINT64_C(0x5810), jump(4, 64), UINT64_C(0x40a00f), 2, 1, 0);
    for (unsigned repeat_index = 0; repeat_index < (6); ++repeat_index)
      tick();
    stop_at(UINT64_C(0x5850), UINT64_C(0x5850), 2);
    send(UINT64_C(0x5850), imm(5, 1, -3, 3, UINT64_C(3)), 0, 1, 0, 0);
    for (unsigned repeat_index = 0; repeat_index < (10); ++repeat_index)
      tick();
    CHECK(response_count == 1 && expected_redirects.size() == 0);
    hold_responses = 0;
    drain();
    send(UINT64_C(0x5850), imm(5, 1, -3, 3, UINT64_C(3)), 0, 1);
    drain();

    // Shared xenvcfg permission follows current privilege, independently of
    // MPRV. Denied CBOs trap before any memory request with the instruction TVAL.
    for (int mode = 0; mode < 2; mode++)
      for (int machine_enable = 0; machine_enable < 2; machine_enable++)
        for (int supervisor_enable = 0; supervisor_enable < 2;
             supervisor_enable++) {
          std::uint64_t pc;
          bool allowed;
          reset_core();
          pc = UINT64_C(0x5900);
          allowed =
              machine_enable != 0 && (mode == 1 || supervisor_enable != 0);
          constant64(pc, 1, 67);
          constant64(pc, 2, UINT64_C(0x500));
          constant64(pc, 3, ((mode)&low_mask(64)) << 11);
          constant64(pc, 4, 128);
          if (machine_enable != 0)
            csr_access(pc, 1, 0, 4, UINT64_C(0x30a), 0);
          pc += 4;
          if (supervisor_enable != 0)
            csr_access(pc, 1, 0, 4, UINT64_C(0x10a), 0);
          pc += 4;
          csr_access(pc, 1, 0, 3, UINT64_C(0x300), UINT64_C(0xa00000000));
          pc += 4;
          csr_access(pc, 1, 0, 2, UINT64_C(0x341), 0);
          pc += 4;
          expect_system(pc, UINT64_C(0x30200073));
          stop_at(pc, UINT64_C(0x500), 3);
          send(pc, UINT64_C(0x30200073), 0, 1, 0, 0);
          drain();
          if (allowed) {
            send(UINT64_C(0x500), UINT64_C(0x40a00f), 0, 1);
            drain();
          } else {
            stop_at(UINT64_C(0x500), 0, 1, 2, UINT64_C(0x40a00f));
            send(UINT64_C(0x500), UINT64_C(0x40a00f), 0, 1, 0, 0);
            drain();
            csr_access(UINT64_C(0x600), 2, 6, 0, UINT64_C(0x343),
                       UINT64_C(0x40a00f));
          }
        }
    // Physical admission faults preserve original rs1, not the cache-block base.
    reset_core();
    send(UINT64_C(0x5a00), imm(1, 0, 67), 0, 1);
    drain();
    inject_memory_fault = 1;
    fault_address = 67;
    stop_at(UINT64_C(0x5a04), 0, 1, 7, 67);
    send(UINT64_C(0x5a04), UINT64_C(0x40a00f), 0, 1, 0, 0);
    drain();
    inject_memory_fault = 0;
    csr_access(UINT64_C(0x5a08), 2, 6, 0, UINT64_C(0x343), 67);

    // Management retains WB through acceptance and delayed completion. Either
    // lane can own it; only an older peer retires before the response.
    for (int operation = 0; operation < 3; operation++)
      for (int slot = 0; slot < 2; slot++) {
        std::uint32_t cmo;
        std::uint64_t pc;
        int before_requests;
        reset_core();
        pc = UINT64_C(0x5b00);
        cmo = (((operation)&low_mask(32)) << 20) | UINT64_C(0xa00f);
        send(pc, imm(1, 0, 67), imm(2, 0, 7));
        drain();
        pc += 8;
        before_requests = requests;
        block_requests = 1;
        hold_responses = 1;
        stop_at(pc + 4 * ((slot)&low_mask(64)),
                pc + 4 * ((slot + 1) & low_mask(64)), 3);
        if (slot == 0)
          send(pc, cmo, imm(3, 0, 9), 2, 1, 0);
        else
          send(pc, imm(3, 0, 9), cmo, 2);
        for (unsigned repeat_index = 0; repeat_index < (12); ++repeat_index)
          tick();
        CHECK(expected.size() == 1 && requests == before_requests);
        block_requests = 0;
        for (unsigned repeat_index = 0; repeat_index < (14); ++repeat_index)
          tick();
        CHECK(requests == before_requests + 1 && response_count == 1 &&
              expected.size() == 1);
        hold_responses = 0;
        drain();
        send(pc + 4 * ((slot + 1) & low_mask(64)),
             imm(4, 1, -3, 3, UINT64_C(3)), 0, 1);
        drain();
      }
    // An older branch kills each management operation before WB authorization.
    for (int operation = 0; operation < 3; operation++) {
      int before_requests;
      reset_core();
      send(UINT64_C(0x5b80), imm(1, 0, 67), 0, 1);
      drain();
      before_requests = requests;
      stop_at(UINT64_C(0x5b84), UINT64_C(0x5bc4));
      send(UINT64_C(0x5b84), jump(2, 64),
           (((operation)&low_mask(32)) << 20) | UINT64_C(0xa00f), 2, 1, 0);
      drain();
      CHECK(requests == before_requests);
    }
    // Illegal operations and physical faults preserve the original instruction/VA.
    // CBIE=01 converts only INVAL; CBCFE remains independent. MPRV must not
    // apply lower-privilege xenvcfg restrictions to M-mode execution.
    for (int operation = 0; operation < 3; operation++)
      for (int mode = 0; mode < 3; mode++)
        for (int enabled = 0; enabled < 4; enabled++) {
          std::uint32_t cmo;
          std::uint64_t pc;
          bool allowed;
          reset_core();
          pc = UINT64_C(0x5c00);
          cmo = (((operation)&low_mask(32)) << 20) | UINT64_C(0xa00f);
          allowed = mode == 2 || enabled == 1 ||
                    (operation == 0 ? enabled == 3 : enabled == 2);
          constant64(pc, 1, 67);
          constant64(pc, 2, UINT64_C(0x500));
          constant64(pc, 3,
                     mode == 2 ? (UINT64_C(1) << 17) | (UINT64_C(1) << 11)
                               : ((mode)&low_mask(64)) << 11);
          constant64(pc, 4,
                     enabled == 1   ? UINT64_C(0x50)
                     : enabled == 2 ? UINT64_C(0x40)
                     : enabled == 3 ? UINT64_C(0x30)
                                    : 0);
          csr_access(pc, 1, 0, 4, UINT64_C(0x30a), 0);
          pc += 4;
          csr_access(pc, 1, 0, 4, UINT64_C(0x10a), 0);
          pc += 4;
          csr_access(pc, 1, 0, 3, UINT64_C(0x300), UINT64_C(0xa00000000));
          pc += 4;
          if (mode < 2) {
            csr_access(pc, 1, 0, 2, UINT64_C(0x341), 0);
            pc += 4;
            expect_system(pc, UINT64_C(0x30200073));
            stop_at(pc, UINT64_C(0x500), 3);
            send(pc, UINT64_C(0x30200073), 0, 1, 0, 0);
            drain();
            pc = UINT64_C(0x500);
          }
          if (allowed) {
            expect_instruction(pc, cmo);
            if (mode < 2 && operation == 0 && enabled == 1)
              expected_requests[0].paccess = 9;
            stop_at(pc, pc + 4, 3);
            send(pc, cmo, 0, 1, 0, 0);
            drain();
          } else {
            stop_at(pc, 0, 1, 2, ((cmo)&low_mask(64)));
            send(pc, cmo, 0, 1, 0, 0);
            drain();
          }
        }
    for (int slot = 0; slot < 2; slot++) {
      reset_core();
      send(UINT64_C(0x5d00), imm(1, 0, 67), 0, 1);
      drain();
      inject_memory_fault = 1;
      fault_address = 67;
      stop_at(UINT64_C(0x5d04) + 4 * ((slot)&low_mask(64)), 0, 1, 7, 67);
      if (slot == 0)
        send(UINT64_C(0x5d04), UINT64_C(0x10a00f), imm(2, 0, 4), 2, 0, 0);
      else
        send(UINT64_C(0x5d04), imm(2, 0, 4), UINT64_C(0x20a00f), 2, 1, 0);
      drain();
      inject_memory_fault = 0;
      csr_access(UINT64_C(0x5d10), 2, 6, 0, UINT64_C(0x343), 67);
    }
    // An interrupt arriving after acceptance waits for the maintenance owner
    // to retire, then uses its successor rather than reissuing the CBO.
    reset_core();
    send(UINT64_C(0x5e00), imm(1, 0, 67), imm(2, 0, 8));
    drain();
    csr_access(UINT64_C(0x5e08), 1, 0, 2, UINT64_C(0x304), 0);
    csr_access(UINT64_C(0x5e0c), 1, 0, 2, UINT64_C(0x300),
               UINT64_C(0xa00000000));
    hold_responses = 1;
    stop_at(UINT64_C(0x5e10), UINT64_C(0x5e14), 3);
    send(UINT64_C(0x5e10), UINT64_C(0x10a00f), 0, 1);
    for (unsigned repeat_index = 0; repeat_index < (12); ++repeat_index)
      tick();
    CHECK(response_count == 1 && expected.size() == 1);
    stop_at(UINT64_C(0x5e14), 0, 3);
    set_interrupts(UINT64_C(16));
    for (unsigned repeat_index = 0; repeat_index < (12); ++repeat_index)
      tick();
    CHECK(expected_redirects.size() == 2 && expected.size() == 1);
    hold_responses = 0;
    drain();
    set_interrupts(0);
    csr_access(UINT64_C(0x600), 2, 6, 0, UINT64_C(0x341), UINT64_C(0x5e14));

    // Prefetch offsets exclude the selector, use rs1 forwarding, and never
    // acquire a load/store owner. Simultaneous hints retain dual retirement.
    reset_core();
    send(UINT64_C(0x5f00), imm(1, 0, 33), imm(2, 0, 64));
    drain();
    for (int op = 0; op < 4; op++)
      if (op != 2)
        for (int slot = 0; slot < 2; slot++)
          for (int variant = 0; variant < 4; variant++) {
            int offset;
            std::uint64_t pc;
            offset = variant == 0   ? -2048
                     : variant == 1 ? -32
                     : variant == 2 ? 0
                                    : 2016;
            pc = ((UINT64_C(0x5f08) + op * 128 + slot * 32 + variant * 8) &
                  low_mask(64));
            if (slot == 0)
              send(pc, prefetch_insn(op, 1, offset), imm(3, 0, 9));
            else
              send(pc, imm(3, 0, 9), prefetch_insn(op, 1, offset));
            drain();
          }
    send(UINT64_C(0x6200), prefetch_insn(0, 1), prefetch_insn(3, 2));
    drain();
    send(UINT64_C(0x6208), imm(1, 1, 32), prefetch_insn(1, 1, -32));
    drain();
    send(UINT64_C(0x6210), imm(7, 1, 3, 6), prefetch_insn(2, 1));
    drain(); // ORI neighbors
    stop_at(UINT64_C(0x6218), UINT64_C(0x6258));
    send(UINT64_C(0x6218), jump(3, 64), prefetch_insn(3, 1), 2, 1, 0);
    drain();
    inject_enable = 1;
    inject_pc = UINT64_C(0x6260);
    inject_result = {.pguest = {},
                     .pdisposition = UINT64_C(1),
                     .pcause = UINT64_C(5),
                     .pvalue = UINT64_C(0xbad)};
    stop_at(UINT64_C(0x6260), 0, 1, 5, UINT64_C(0xbad));
    send(UINT64_C(0x6260), imm(4, 0, 1), prefetch_insn(1, 1), 2, 0, 0);
    drain();
    inject_enable = 0;
    CHECK(prefetch_count >= 26 && prefetch_pairs > 0);

    // Real CSR commands return the old value, preserve source-index write intent,
    // and serialize even without a GPR destination. Younger work is refetched.
    reset_core();
    send(UINT64_C(0x6000), imm(1, 0, UINT64_C(0x55)), imm(2, 0, 0));
    drain();
    csr_access(UINT64_C(0x6008), 1, 3, 1, UINT64_C(0x340),
               0); // CSRRW mscratch <- x1
    csr_access(UINT64_C(0x600c), 6, 4, 10, UINT64_C(0x340),
               UINT64_C(0x55)); // CSRRSI adds bits 1 and 3
    csr_access(UINT64_C(0x6010), 7, 5, 3, UINT64_C(0x340),
               UINT64_C(0x5f)); // CSRRCI removes bits 0 and 1
    csr_access(UINT64_C(0x6014), 2, 6, 0, UINT64_C(0x340),
               UINT64_C(0x5c)); // rs1=x0 is read-only
    csr_access(UINT64_C(0x6018), 3, 7, 2, UINT64_C(0x340),
               UINT64_C(0x5c)); // nonzero rs1 containing zero is still a write
    csr_access(UINT64_C(0x601c), 5, 0, 0, UINT64_C(0x340),
               UINT64_C(0x5c)); // CSRRWI with zero clears, even rd=x0
    csr_access(UINT64_C(0x6020), 2, 8, 0, UINT64_C(0x340), 0);
    csr_access(UINT64_C(0x6024), 2, 9, 0, UINT64_C(0xf14),
               7); // read-only mhartid
    stop_at(UINT64_C(0x6028), 0, 1, 2,
            ((csr(2, 10, 2, UINT64_C(0xf14))) & low_mask(64)));
    send(UINT64_C(0x6028), csr(2, 10, 2, UINT64_C(0xf14)), 0, 1, 0, 0);
    drain();
    csr_access(UINT64_C(0x602c), 2, 11, 0, UINT64_C(0x343),
               ((csr(2, 10, 2, UINT64_C(0xf14))) & low_mask(64)));

    // CSR WB recovery wins over a younger taken branch in MEM, and kills a
    // speculative store before authorization. All issue groups remain single-slot.
    {
      int before_override;
      before_override = wb_overrides;
      expect_system(UINT64_C(0x6040), csr(5, 12, 19, UINT64_C(0x340)), 0);
      stop_at(UINT64_C(0x6040), UINT64_C(0x6044), 3);
      send(UINT64_C(0x6040), csr(5, 12, 19, UINT64_C(0x340)), 0, 1, 0, 0);
      send(UINT64_C(0x6044), jump(13, 64), store(1, 0, 0, 3), 2, 0, 0);
      drain();
      CHECK(wb_overrides > before_override);
      csr_access(UINT64_C(0x6048), 2, 14, 0, UINT64_C(0x340), 19);
    }

    // Count the successful prefix: two ordinary instructions_in add two, a
    // younger fault adds only its older peer, and an older fault adds zero.
    reset_core();
    csr_access(UINT64_C(0x6100), 1, 0, 0, UINT64_C(0xb02),
               0); // explicit counter write wins over its own retirement
    send(UINT64_C(0x6104), imm(1, 0, 1), imm(2, 0, 2));
    drain();
    csr_access(UINT64_C(0x610c), 2, 3, 0, UINT64_C(0xb02), 2);
    stop_at(UINT64_C(0x6114), 0, 1, 1, UINT64_C(0x6114));
    send(UINT64_C(0x6110), imm(4, 0, 4), imm(5, 0, 5), 2, 1, 0, 1);
    drain();
    csr_access(UINT64_C(0x6118), 2, 6, 0, UINT64_C(0xb02), 4);
    csr_access(UINT64_C(0x611c), 2, 7, 0, UINT64_C(0x341), UINT64_C(0x6114));
    csr_access(UINT64_C(0x6120), 2, 8, 0, UINT64_C(0x342), 1);
    csr_access(UINT64_C(0x6124), 2, 9, 0, UINT64_C(0x343), UINT64_C(0x6114));
    stop_at(UINT64_C(0x6128), 0, 1, 2, UINT64_C(0xffffffff));
    send(UINT64_C(0x6128), UINT64_C(0xffffffff), imm(10, 0, 10), 2, 0, 0);
    drain();
    csr_access(UINT64_C(0x6130), 2, 11, 0, UINT64_C(0xb02), 8);
    csr_access(UINT64_C(0x6134), 2, 12, 0, UINT64_C(0x341), UINT64_C(0x6128));

    // Trap-vector/return state is architectural, not a controller stop.
    reset_core();
    send(UINT64_C(0x6200), imm(1, 0, UINT64_C(0x700)),
         imm(2, 0, UINT64_C(0x400)));
    drain();
    csr_access(UINT64_C(0x6208), 1, 0, 1, UINT64_C(0x305), 0);
    trap_target = UINT64_C(0x700);
    stop_at(UINT64_C(0x6210), 0, 1, 11, 0);
    send(UINT64_C(0x6210), UINT64_C(0x73), imm(15, 0, 99), 2, 0, 0);
    drain();
    csr_access(UINT64_C(0x700), 2, 3, 0, UINT64_C(0x341), UINT64_C(0x6210));
    csr_access(UINT64_C(0x704), 2, 4, 0, UINT64_C(0x342), 11);
    csr_access(UINT64_C(0x708), 2, 5, 0, UINT64_C(0x343), 0);
    csr_access(UINT64_C(0x70c), 1, 0, 2, UINT64_C(0x341), UINT64_C(0x6210));
    expect_system(UINT64_C(0x710), UINT64_C(0x30200073));
    stop_at(UINT64_C(0x710), UINT64_C(0x400), 3);
    send(UINT64_C(0x710), UINT64_C(0x30200073), 0, 1, 0, 0);
    drain();
    stop_at(UINT64_C(0x400), 0, 1, 3, 0);
    send(UINT64_C(0x400), UINT64_C(0x100073), 0, 1, 0, 0);
    drain();
    csr_access(UINT64_C(0x714), 2, 6, 0, UINT64_C(0x341), UINT64_C(0x400));
    csr_access(UINT64_C(0x718), 2, 7, 0, UINT64_C(0x342), 3);

    // MRET enters S, SRET enters U; denied CSR access traps back to M with the
    // original encoding. S-mode exception delegation uses stvec/sepc/scause.
    reset_core();
    send(UINT64_C(0x6300), imm(1, 0, UINT64_C(0x700)),
         imm(2, 0, UINT64_C(0x740)));
    drain();
    csr_access(UINT64_C(0x6308), 1, 0, 1, UINT64_C(0x305), 0);
    trap_target = UINT64_C(0x700);
    csr_access(UINT64_C(0x630c), 1, 0, 2, UINT64_C(0x105), 0);
    send(UINT64_C(0x6310), imm(3, 0, 1), imm(4, 0, UINT64_C(0x500)));
    drain();
    send(UINT64_C(0x6318), imm(3, 3, 11, 1), imm(5, 0, UINT64_C(0x540)));
    drain(); // x3 = MPP.S
    csr_access(UINT64_C(0x6320), 1, 0, 3, UINT64_C(0x300),
               UINT64_C(0xa00000000));
    csr_access(UINT64_C(0x6324), 1, 0, 4, UINT64_C(0x341), 0);
    csr_access(UINT64_C(0x6328), 1, 0, 5, UINT64_C(0x141), 0);
    expect_system(UINT64_C(0x632c), UINT64_C(0x30200073));
    stop_at(UINT64_C(0x632c), UINT64_C(0x500), 3);
    send(UINT64_C(0x632c), UINT64_C(0x30200073), 0, 1, 0, 0);
    drain();
    expect_system(UINT64_C(0x500), UINT64_C(0x10200073));
    stop_at(UINT64_C(0x500), UINT64_C(0x540), 3);
    send(UINT64_C(0x500), UINT64_C(0x10200073), 0, 1, 0, 0);
    drain();
    stop_at(UINT64_C(0x540), 0, 1, 2,
            ((csr(2, 6, 0, UINT64_C(0x340))) & low_mask(64)));
    send(UINT64_C(0x540), csr(2, 6, 0, UINT64_C(0x340)), 0, 1, 0, 0);
    drain();
    csr_access(UINT64_C(0x700), 2, 7, 0, UINT64_C(0x341), UINT64_C(0x540));
    csr_access(UINT64_C(0x704), 2, 8, 0, UINT64_C(0x343),
               ((csr(2, 6, 0, UINT64_C(0x340))) & low_mask(64)));
    csr_access(UINT64_C(0x708), 5, 0, 8, UINT64_C(0x302),
               0); // delegate breakpoint
    csr_access(UINT64_C(0x70c), 1, 0, 3, UINT64_C(0x300),
               UINT64_C(0xa00000000));
    csr_access(UINT64_C(0x710), 1, 0, 4, UINT64_C(0x341), UINT64_C(0x540));
    expect_system(UINT64_C(0x714), UINT64_C(0x30200073));
    stop_at(UINT64_C(0x714), UINT64_C(0x500), 3);
    send(UINT64_C(0x714), UINT64_C(0x30200073), 0, 1, 0, 0);
    drain();
    trap_target = UINT64_C(0x740);
    stop_at(UINT64_C(0x500), 0, 1, 3, 0);
    send(UINT64_C(0x500), UINT64_C(0x100073), 0, 1, 0, 0);
    drain();
    csr_access(UINT64_C(0x740), 2, 9, 0, UINT64_C(0x141), UINT64_C(0x500));
    csr_access(UINT64_C(0x744), 2, 10, 0, UINT64_C(0x142), 3);

    // An accepted load must finish and write its reserved port before the
    // following system command can read its source or mutate CSR state.
    reset_core();
    hold_responses = 1;
    send(UINT64_C(0x6400), imm(1, 0, 0, 3, UINT64_C(3)), imm(2, 0, 2));
    expect_system(UINT64_C(0x6408), csr(1, 3, 1, UINT64_C(0x340)), 0);
    stop_at(UINT64_C(0x6408), UINT64_C(0x640c), 3);
    send(UINT64_C(0x6408), csr(1, 3, 1, UINT64_C(0x340)), 0, 1, 0, 0);
    for (unsigned repeat_index = 0; repeat_index < (10); ++repeat_index)
      tick();
    CHECK(response_count == 1 && expected.size() == 1 &&
          expected_redirects.size() == 1);
    hold_responses = 0;
    drain();
    csr_access(UINT64_C(0x640c), 2, 4, 0, UINT64_C(0x340), model[1]);

    // Svinval uses the same precise, single-slot WB boundary. TVM restricts
    // invalidation, not the ordering instructions_in; U may execute neither.
    for (int privilege = 0; privilege < 3; privilege++)
      for (int tvm = 0; tvm < 2; tvm++)
        for (int operation = 0; operation < 3; operation++) {
          std::uint32_t word;
          std::uint64_t pc;
          bool allowed;
          int before_flushes;
          reset_core();
          pc = UINT64_C(0x16000);
          word = operation == 0 ? UINT64_C(0x17f08073)
                                : (operation == 1 ? UINT64_C(0x18000073)
                                                  : UINT64_C(0x18100073));
          allowed = privilege == 2 ||
                    (privilege == 1 && (operation != 0 || tvm == 0));
          constant64(pc, 1,
                     (tvm != 0 ? UINT64_C(0x100000) : 0) |
                         (privilege == 1 ? UINT64_C(0x800) : 0));
          drain();
          csr_access(pc, 1, 0, 1, UINT64_C(0x300), UINT64_C(0xa00000000));
          pc += 4;
          if (privilege != 2) {
            send(pc, imm(2, 0, UINT64_C(0x500)), 0, 1);
            pc += 4;
            drain();
            csr_access(pc, 1, 0, 2, UINT64_C(0x341), 0);
            pc += 4;
            expect_system(pc, UINT64_C(0x30200073));
            stop_at(pc, UINT64_C(0x500), 3);
            send(pc, UINT64_C(0x30200073), 0, 1, 0, 0);
            drain();
            pc = UINT64_C(0x500);
          }
          before_flushes = translation_invalidations;
          // An ordinary older peer retires, while the system command issues alone.
          expect_instruction(pc, imm(6, 0, 42));
          if (allowed) {
            expect_system(pc + 4, word);
            stop_at(pc + 4, pc + 8, 3);
          } else
            stop_at(pc + 4, 0, 1, 2, ((word)&low_mask(64)));
          send(pc, imm(6, 0, 42), word, 2, 0, 0);
          drain();
          CHECK(translation_invalidations ==
                before_flushes + int(allowed && operation == 0));
          if (!allowed) {
            csr_access(0, 2, 7, 0, UINT64_C(0x341), pc + 4);
            csr_access(4, 2, 8, 0, UINT64_C(0x343), ((word)&low_mask(64)));
          }
        }
    for (int operation = 0; operation < 3; operation++) {
      std::uint32_t word;
      int before_flushes;
      reset_core();
      hold_responses = 1;
      word = operation == 0 ? UINT64_C(0x16000073)
                            : (operation == 1 ? UINT64_C(0x18000073)
                                              : UINT64_C(0x18100073));
      before_flushes = translation_invalidations;
      send(UINT64_C(0x16100), imm(1, 0, 0, 3, UINT64_C(3)), imm(2, 0, 2));
      expect_system(UINT64_C(0x16108), word);
      stop_at(UINT64_C(0x16108), UINT64_C(0x1610c), 3);
      send(UINT64_C(0x16108), word, imm(3, 0, 99), 2, 0, 0);
      for (unsigned repeat_index = 0; repeat_index < (10); ++repeat_index)
        tick();
      CHECK(response_count == 1 && expected.size() == 1 &&
            translation_invalidations == before_flushes);
      hold_responses = 0;
      drain();
      CHECK(translation_invalidations == before_flushes + int(operation == 0));
    }

    // Interrupts stop before the oldest unretired instruction and retain the
    // precise boundary while already retired load owners drain.
    reset_core();
    send(UINT64_C(0x6500), imm(1, 0, UINT64_C(0x700)), imm(2, 0, 8));
    drain();
    csr_access(UINT64_C(0x6508), 1, 0, 1, UINT64_C(0x305), 0);
    trap_target = UINT64_C(0x700);
    csr_access(UINT64_C(0x650c), 1, 0, 2, UINT64_C(0x304), 0);
    csr_access(UINT64_C(0x6510), 1, 0, 2, UINT64_C(0x300),
               UINT64_C(0xa00000000));
    hold_responses = 1;
    send(UINT64_C(0x6514), imm(3, 0, 0, 3, UINT64_C(3)), imm(4, 0, 4));
    for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
      tick();
    stop_at(UINT64_C(0x651c), UINT64_C(0x700), 3);
    set_interrupts(UINT64_C(16));
    for (unsigned repeat_index = 0; repeat_index < (10); ++repeat_index)
      tick();
    CHECK(expected_redirects.size() == 1 && response_count == 1);
    hold_responses = 0;
    drain();
    set_interrupts(0);
    csr_access(UINT64_C(0x700), 2, 5, 0, UINT64_C(0x341), UINT64_C(0x651c));
    csr_access(UINT64_C(0x704), 2, 6, 0, UINT64_C(0x342),
               UINT64_C(0x8000000000000003));
    csr_access(UINT64_C(0x708), 2, 7, 0, UINT64_C(0x343), 0);
    // Return with MIE restored, then intercept a live pair at WB, neither retires.
    expect_system(UINT64_C(0x70c), UINT64_C(0x30200073));
    stop_at(UINT64_C(0x70c), UINT64_C(0x651c), 3);
    send(UINT64_C(0x70c), UINT64_C(0x30200073), 0, 1, 0, 0);
    drain();
    send(UINT64_C(0x651c), imm(8, 0, 8), imm(9, 0, 9), 2, 0, 0);
    until([&] { return memory_stage(0).pvalid; });
    falling();
    stop_at(UINT64_C(0x651c), UINT64_C(0x700), 3);
    set_interrupts(UINT64_C(16));
    drain();
    set_interrupts(0);
    csr_access(UINT64_C(0x710), 2, 10, 0, UINT64_C(0x341), UINT64_C(0x651c));

    // Interrupts cannot abandon a noncancelable split after prefix dispatch.
    // Complete and retire it first, then trap at its sequential successor.
    reset_core();
    send(UINT64_C(0x6540), imm(1, 0, UINT64_C(0x301)), imm(2, 0, 8));
    drain();
    send(UINT64_C(0x6548), imm(3, 0, UINT64_C(0x700)), 0, 1);
    drain();
    csr_access(UINT64_C(0x654c), 1, 0, 3, UINT64_C(0x305), 0);
    trap_target = UINT64_C(0x700);
    csr_access(UINT64_C(0x6550), 1, 0, 2, UINT64_C(0x304), 0);
    csr_access(UINT64_C(0x6554), 1, 0, 2, UINT64_C(0x300),
               UINT64_C(0xa00000000));
    hold_split = 1;
    stop_at(UINT64_C(0x6558), UINT64_C(0x655c), 3);
    send(UINT64_C(0x6558), imm(4, 1, 0, 1, UINT64_C(3)), 0, 1);
    until([&] { return split_active; });
    set_interrupts(UINT64_C(16));
    for (unsigned repeat_index = 0; repeat_index < (16); ++repeat_index)
      tick();
    CHECK(expected.size() == 1 && expected_redirects.size() == 1);
    stop_at(UINT64_C(0x655c), UINT64_C(0x700), 3);
    hold_split = 0;
    drain();
    set_interrupts(0);
    csr_access(UINT64_C(0x700), 2, 5, 0, UINT64_C(0x341), UINT64_C(0x655c));

    // WFI retires once, blocks younger effects, and wakes on locally enabled
    // pending interrupts even with global MIE clear. With MIE set it traps.
    reset_core();
    csr_access(UINT64_C(0x6600), 5, 0, 8, UINT64_C(0x304), 0);
    expect_system(UINT64_C(0x6604), UINT64_C(0x10500073));
    send(UINT64_C(0x6604), UINT64_C(0x10500073), imm(11, 0, 11), 2, 0, 0);
    drain();
    CHECK(sleeping);
    stop_at(UINT64_C(0x6608), UINT64_C(0x6608), 3);
    set_interrupts(UINT64_C(16));
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      tick();
    set_interrupts(0);
    drain();
    CHECK(!sleeping);
    csr_access(UINT64_C(0x6608), 2, 12, 0, UINT64_C(0xb02), 2);
    send(UINT64_C(0x660c), imm(1, 0, UINT64_C(0x700)), imm(2, 0, 8));
    drain();
    csr_access(UINT64_C(0x6614), 1, 0, 1, UINT64_C(0x305), 0);
    trap_target = UINT64_C(0x700);
    csr_access(UINT64_C(0x6618), 1, 0, 2, UINT64_C(0x300),
               UINT64_C(0xa00000000));
    expect_system(UINT64_C(0x661c), UINT64_C(0x10500073));
    send(UINT64_C(0x661c), UINT64_C(0x10500073), 0, 1, 0, 0);
    drain();
    stop_at(UINT64_C(0x6620), UINT64_C(0x700), 3);
    set_interrupts(UINT64_C(16));
    drain();
    set_interrupts(0);
    csr_access(UINT64_C(0x700), 2, 13, 0, UINT64_C(0x341), UINT64_C(0x6620));
    CHECK(!sleeping);

    // PAUSE is an exact FENCE overlay. It retires before its bounded cooldown,
    // without redirecting or discarding buffered successors, in either slot.
    for (int slot = 0; slot < 2; slot++) {
      int before_pause, before_dual;
      reset_core();
      before_pause = pause_commits;
      before_dual = pause_dual;
      if (slot == 0)
        send(UINT64_C(0x6640), UINT64_C(0x100000f), imm(1, 0, 7));
      else {
        send(UINT64_C(0x6640), imm(1, 0, 7), UINT64_C(0x100000f));
        send(UINT64_C(0x6648), imm(2, 0, 8), imm(3, 0, 9));
      }
      while (pause_commits == before_pause)
        tick();
      CHECK(issued == 0 && !sleeping);
      for (int repeat_index = 0; repeat_index < (15); ++repeat_index) {
        tick();
        CHECK(issued == 0);
      }
      tick();
      CHECK(issued != 0);
      drain();
      CHECK(pause_commits == before_pause + 1 &&
            pause_dual == before_dual + slot);
    }
    // Adjacent hints must not coissue across the older pause.
    {
      int before_pause;
      reset_core();
      before_pause = pause_commits;
      send(UINT64_C(0x6650), UINT64_C(0x100000f), UINT64_C(0x100000f));
      send(UINT64_C(0x6658), imm(1, 0, 1), 0, 1);
      drain();
      CHECK(pause_commits == before_pause + 2);
    }
    // Unlike FENCE, PAUSE does not drain an older accepted load, and its
    // cooldown does not obstruct the independent deferred completion path.
    {
      int before_pause;
      reset_core();
      before_pause = pause_commits;
      hold_responses = 1;
      send(UINT64_C(0x6660), imm(5, 0, 0, 3, UINT64_C(3)), UINT64_C(0x100000f));
      while (pause_commits == before_pause)
        tick();
      CHECK(response_count == 1);
      send(UINT64_C(0x6668), imm(1, 0, 7), 0, 1);
      for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
        tick();
      hold_responses = 0;
      drain();
      CHECK(pause_commits == before_pause + 1);
    }
    // A killed hint must neither retire nor delay the recovered stream.
    for (int reason = 0; reason < 3; reason++) {
      int before_pause, restart_cycle;
      reset_core();
      before_pause = pause_commits;
      if (reason == 0) {
        stop_at(UINT64_C(0x6670), UINT64_C(0x66b0));
        send(UINT64_C(0x6670), jump(0, 64), UINT64_C(0x100000f), 2, 1, 0);
      } else {
        inject_enable = 1;
        inject_pc = UINT64_C(0x6670);
        inject_result = {.pguest = {},
                         .pdisposition =
                             static_cast<uint8_t>(((reason)&low_mask(2))),
                         .pcause = UINT64_C(5),
                         .pvalue = UINT64_C(0xbad)};
        stop_at(UINT64_C(0x6670), reason == 1 ? 0 : UINT64_C(0x6670), reason, 5,
                UINT64_C(0xbad));
        send(UINT64_C(0x6670), imm(1, 0, 1), UINT64_C(0x100000f), 2, 0, 0);
      }
      drain();
      inject_enable = 0;
      restart_cycle = cycles;
      send(UINT64_C(0x66b0), imm(2, 0, 2), 0, 1);
      drain();
      CHECK(pause_commits == before_pause && cycles - restart_cycle < 16);
    }
    // Interrupts end cooldown promptly and report the already-retired hint's
    // successor. A later reset must likewise discard a pending cooldown.
    {
      int before_pause, before_stop, interrupt_cycle;
      reset_core();
      csr_access(UINT64_C(0x66c0), 5, 0, 8, UINT64_C(0x304), 0);
      csr_access(UINT64_C(0x66c4), 6, 0, 8, UINT64_C(0x300),
                 UINT64_C(0xa00000000));
      before_pause = pause_commits;
      send(UINT64_C(0x66c8), UINT64_C(0x100000f), 0, 1);
      while (pause_commits == before_pause)
        tick();
      before_stop = stops;
      interrupt_cycle = cycles;
      stop_at(UINT64_C(0x66cc), 0, 3);
      set_interrupts(UINT64_C(16));
      while (stops == before_stop)
        tick();
      CHECK(cycles - interrupt_cycle < 8);
      set_interrupts(0);
      drain();
      csr_access(0, 2, 3, 0, UINT64_C(0x341), UINT64_C(0x66cc));
      reset_core();
      before_pause = pause_commits;
      send(UINT64_C(0x66d0), UINT64_C(0x100000f), 0, 1);
      while (pause_commits == before_pause)
        tick();
      reset_core();
      interrupt_cycle = cycles;
      send(UINT64_C(0x66d4), imm(2, 0, 2), 0, 1);
      drain();
      CHECK(cycles - interrupt_cycle < 16);
    }

    // WRS has no register/memory effects, compacts from the younger slot, and
    // retires only once. Its younger speculative store is refetched, not committed.
    for (int short_wait = 0; short_wait < 2; short_wait++) {
      std::uint32_t word;
      word = short_wait != 0 ? UINT64_C(0x1d00073) : UINT64_C(0xd00073);
      reset_core();
      expect_instruction(UINT64_C(0x6700), imm(1, 0, 7));
      expect_system(UINT64_C(0x6704), word);
      stop_at(UINT64_C(0x6704), UINT64_C(0x6708), 3);
      send(UINT64_C(0x6700), imm(1, 0, 7), word, 2, 0, 0);
      drain();
      reservation_valid = 1;
      expect_system(UINT64_C(0x6708), word);
      stop_at(UINT64_C(0x6708), UINT64_C(0x670c), 3);
      send(UINT64_C(0x6708), word, store(1, 0, 0, 3), 2, 0, 0);
      until([&] { return sleeping; });
      for (unsigned repeat_index = 0; repeat_index < (12); ++repeat_index)
        tick();
      CHECK(expected.size() == 1 && expected_redirects.size() == 1 &&
            !memory_out.prequest.pvalid && !split_out.prequest.pvalid);
      reservation_valid = 0;
      drain();
      send(UINT64_C(0x670c), imm(2, 1, 1), 0, 1);
      drain();
    }
    // An accepted LR response and its RF write drain before WRS may start.
    reset_core();
    hold_responses = 1;
    send(UINT64_C(0x6720), atomic_insn(2, 3, 1, 0), 0, 1);
    for (unsigned repeat_index = 0; repeat_index < (10); ++repeat_index)
      tick();
    expect_system(UINT64_C(0x6724), UINT64_C(0xd00073));
    stop_at(UINT64_C(0x6724), UINT64_C(0x6728), 3);
    send(UINT64_C(0x6724), UINT64_C(0xd00073), 0, 1, 0, 0);
    for (unsigned repeat_index = 0; repeat_index < (12); ++repeat_index)
      tick();
    CHECK(!sleeping && response_count == 1 && expected.size() == 1);
    reservation_valid = 1;
    hold_responses = 0;
    until([&] { return sleeping; });
    for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
      tick();
    CHECK(response_count == 0 && expected_completions.size() == 0);
    reservation_valid = 0;
    drain();
    // Locally enabled interrupts wake with global MIE clear; enabled interrupts
    // trap at the successor after the retained WRS has retired.
    for (int global_enable = 0; global_enable < 2; global_enable++) {
      reset_core();
      csr_access(UINT64_C(0x6740), 5, 0, 8, UINT64_C(0x304), 0);
      if (global_enable != 0)
        csr_access(UINT64_C(0x6744), 6, 0, 8, UINT64_C(0x300),
                   UINT64_C(0xa00000000));
      reservation_valid = 1;
      expect_system(UINT64_C(0x6748), UINT64_C(0xd00073));
      stop_at(UINT64_C(0x6748), UINT64_C(0x674c), 3);
      send(UINT64_C(0x6748), UINT64_C(0xd00073), 0, 1, 0, 0);
      until([&] { return sleeping; });
      for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
        tick();
      if (global_enable != 0)
        stop_at(UINT64_C(0x674c), 0, 3);
      set_interrupts(UINT64_C(16));
      drain();
      set_interrupts(0);
      CHECK(!sleeping);
      if (global_enable != 0)
        csr_access(0, 2, 3, 0, UINT64_C(0x341), UINT64_C(0x674c));
    }
    // U and S permit WRS even with TW set: STO completes normally after the
    // short bound, while NTO traps only when that bound expires. M ignores TW.
    for (int privilege = 0; privilege < 3; privilege++)
      for (int short_wait = 0; short_wait < (privilege == 0 ? 3 : 2);
           short_wait++) {
        std::uint64_t pc, mstatus;
        std::uint32_t word;
        int start_cycle;
        word = short_wait == 1 ? UINT64_C(0x1d00073) : UINT64_C(0xd00073);
        reset_core();
        pc = UINT64_C(0x100);
        mstatus = (UINT64_C(1) << 21) |
                  (((privilege == 2 ? 3 : privilege) & low_mask(64)) << 11);
        constant64(pc, 1, mstatus);
        drain();
        csr_access(pc, 1, 0, 1, UINT64_C(0x300), UINT64_C(0xa00000000));
        pc += 4;
        if (privilege != 2) {
          send(pc, imm(2, 0, UINT64_C(0x500)), 0, 1);
          drain();
          pc += 4;
          csr_access(pc, 1, 0, 2, UINT64_C(0x341), 0);
          pc += 4;
          expect_system(pc, UINT64_C(0x30200073));
          stop_at(pc, UINT64_C(0x500), 3);
          send(pc, UINT64_C(0x30200073), 0, 1, 0, 0);
          drain();
        }
        reservation_valid = 1;
        if (short_wait == 0 && privilege != 2)
          stop_at(UINT64_C(0x500), 0, 1, 2, ((word)&low_mask(64)));
        else {
          expect_system(UINT64_C(0x500), word);
          stop_at(UINT64_C(0x500), UINT64_C(0x504), 3);
        }
        send(UINT64_C(0x500), word, 0, 1, 0, 0);
        until([&] { return sleeping; });
        start_cycle = cycles;
        for (unsigned repeat_index = 0; repeat_index < (32); ++repeat_index)
          tick();
        CHECK(sleeping && expected_redirects.size() == 1);
        // Reservation loss on the timeout edge must win over TW's exception.
        if (short_wait == 2) {
          for (unsigned repeat_index = 0; repeat_index < (4063); ++repeat_index)
            tick();
          reservation_valid = 0;
        }
        if (privilege == 2 && short_wait == 0) {
          for (unsigned repeat_index = 0; repeat_index < (4100); ++repeat_index)
            tick();
          CHECK(sleeping && expected_redirects.size() == 1);
          reservation_valid = 0;
        }
        drain();
        CHECK(cycles - start_cycle >= 4095 && cycles - start_cycle < 4300);
        if (short_wait == 0 && privilege != 2) {
          csr_access(0, 2, 3, 0, UINT64_C(0x341), UINT64_C(0x500));
          csr_access(4, 2, 4, 0, UINT64_C(0x343), ((word)&low_mask(64)));
        }
      }
    // STO also wakes on reservation loss. Reset discards a pending owner
    // without a delayed retirement in the next epoch.
    reset_core();
    reservation_valid = 1;
    expect_system(UINT64_C(0x6760), UINT64_C(0x1d00073));
    stop_at(UINT64_C(0x6760), UINT64_C(0x6764), 3);
    send(UINT64_C(0x6760), UINT64_C(0x1d00073), 0, 1, 0, 0);
    until([&] { return sleeping; });
    for (unsigned repeat_index = 0; repeat_index < (4094); ++repeat_index)
      tick();
    reservation_valid = 0;
    drain();
    reservation_valid = 1;
    send(UINT64_C(0x6764), UINT64_C(0xd00073), 0, 1, 0, 0);
    until([&] { return sleeping; });
    for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
      tick();
    reset_core();
    send(UINT64_C(0x6780), imm(3, 0, 3), imm(4, 0, 4));
    drain();
    stop_at(UINT64_C(0x6788), UINT64_C(0x67c8));
    send(UINT64_C(0x6788), jump(0, 64), UINT64_C(0xd00073), 2, 1, 0);
    drain();
    CHECK(!sleeping);

    // Fences retain no WB bubble: they wait in RR for the accepted owner and
    // its deferred GPR write, issue alone, then flush younger branch recovery.
    for (int instruction_fence = 0; instruction_fence < 2;
         instruction_fence++) {
      std::uint32_t word;
      reset_core();
      hold_responses = 1;
      word = instruction_fence != 0 ? UINT64_C(0x100f) : UINT64_C(0xff0000f);
      send(UINT64_C(0x6800), imm(5, 0, 0, 3, UINT64_C(3)), 0, 1);
      for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
        tick();
      expect_system(UINT64_C(0x6804), word);
      stop_at(UINT64_C(0x6804), UINT64_C(0x6808), 3);
      send(UINT64_C(0x6804), word, jump(0, 64), 2, 0, 0);
      for (unsigned repeat_index = 0; repeat_index < (12); ++repeat_index)
        tick();
      CHECK(response_count == 1 && expected_redirects.size() == 1 &&
            invalidations == 0);
      hold_responses = 0;
      drain();
      CHECK(invalidations == instruction_fence);
    }
    // Every M operation, including word projection, signed high products,
    // divide-by-zero, signed overflow, and independently varying operand signs.
    for (int scenario = 0; scenario < 6; scenario++) {
      std::uint64_t pc;
      std::uint64_t a, b;
      pc = UINT64_C(0x8000);
      reset_core();
      switch (scenario) {
      case 0: {
        {
          a = UINT64_C(0xfedcba9876543210);
          b = UINT64_C(0x123456789abcdef);
        }
      } break;
      case 1: {
        {
          a = UINT64_C(0x8000000000000000);
          b = UINT64_MAX;
        }
      } break;
      case 2: {
        {
          a = UINT64_C(0xffffffff80000000);
          b = UINT64_C(0xffffffffffffffff);
        }
      } break;
      case 3: {
        {
          a = UINT64_C(0x8000000080000001);
          b = 0;
        }
      } break;
      case 4: {
        {
          a = UINT64_C(0xffffffffffffffff);
          b = UINT64_C(0x8000000000000001);
        }
      } break;
      case 5: {
        {
          a = UINT64_C(0x7fffffff7fffffff);
          b = 7;
        }
      } break;
      }
      constant64(pc, 1, a);
      constant64(pc, 2, b);
      drain();
      for (int word = 0; word < 2; word++)
        for (int funct3 = 0; funct3 < 8; funct3++)
          if (word == 0 || funct3 == 0 || funct3 >= 4) {
            send(pc, m_insn(3, 1, 2, funct3, word != 0), imm(4, 0, funct3));
            pc += 8;
            send(pc, imm(5, 3, 1), imm(6, 4, 1));
            pc += 8;
            drain();
          }
      send(pc, m_insn(0, 1, 2, 4), m_insn(0, 1, 2, 0));
      drain();
    }
    // Pipelined requests overlap each other and slow loads; response order is
    // allowed to differ from retirement. The second GPR port remains reserved.
    reset_core();
    configured_delay = 50;
    send(UINT64_C(0x9000), imm(1, 0, -17), imm(2, 0, 7));
    drain();
    send(UINT64_C(0x9008), imm(20, 0, 0, 3, UINT64_C(3)), imm(21, 0, 21));
    for (int i = 0; i < 12; i++)
      send(((UINT64_C(0x9010) + i * 8) & low_mask(64)),
           m_insn(3 + i, 1, 2, i % 4), imm(22, 0, i));
    send(UINT64_C(0x9070), m_insn(17, 1, 2, 4), imm(18, 0, 18));
    send(UINT64_C(0x9078), imm(19, 3, 1), imm(2, 0, 3));
    drain();
    configured_delay = 8;
    // Accepted multiply/divide owners survive a younger trap and drain before
    // trap entry. A rejected owner must never complete or set the scoreboard.
    for (int funct3 = 0; funct3 <= 4; funct3 += 4) {
      reset_core();
      send(UINT64_C(0x9200), imm(1, 0, -101), imm(2, 0, 3));
      drain();
      stop_at(UINT64_C(0x920c), UINT64_C(0x920c), 1, 2, UINT64_C(0xffffffff));
      send(UINT64_C(0x9208), m_insn(3, 1, 2, funct3), UINT64_C(0xffffffff), 2,
           1, 0);
      drain();
      send(UINT64_C(0x9300), imm(4, 3, 0), imm(3, 0, 9));
      drain();
      reset_core();
      send(UINT64_C(0x9400), imm(1, 0, 101), imm(2, 0, 3));
      drain();
      stop_at(UINT64_C(0x9408), UINT64_C(0x9408), 1, 13, UINT64_C(0xdead));
      inject_enable = 1;
      inject_pc = UINT64_C(0x9408);
      inject_result = {.pguest = {},
                       .pdisposition = UINT64_C(1),
                       .pcause = UINT64_C(13),
                       .pvalue = UINT64_C(0xdead)};
      send(UINT64_C(0x9408), imm(7, 0, 7), m_insn(3, 1, 2, funct3), 2, 0, 0);
      drain();
      inject_enable = 0;
      send(UINT64_C(0x9500), imm(3, 0, 5), imm(4, 3, 1));
      drain();
    }
    // The busy divider replays before acceptance; the accepted owner survives.
    reset_core();
    {
      std::uint64_t pc;
      pc = UINT64_C(0xa000);
      constant64(pc, 1, UINT64_C(0x7fffffffffffffff));
      constant64(pc, 2, 3);
      drain();
    }
    send(UINT64_C(0xa100), m_insn(3, 1, 2, 4), imm(8, 0, 8));
    stop_at(UINT64_C(0xa108), UINT64_C(0xa108), 2);
    send(UINT64_C(0xa108), m_insn(4, 1, 2, 5), imm(9, 0, 9), 2, 0, 0);
    drain();
    send(UINT64_C(0xa108), m_insn(4, 1, 2, 5), imm(9, 0, 9));
    drain();
    send(UINT64_C(0xa110), imm(5, 3, 1), imm(6, 4, 1));
    drain();
    // Same-group older preacceptance replay rejects an EX-launched multiply.
    reset_core();
    send(UINT64_C(0xa200), imm(1, 0, 17), imm(2, 0, 3));
    drain();
    stop_at(UINT64_C(0xa208), UINT64_C(0xa208), 2);
    inject_enable = 1;
    inject_pc = UINT64_C(0xa208);
    inject_result = {.pguest = {},
                     .pdisposition = UINT64_C(2),
                     .pcause = UINT64_C(0),
                     .pvalue = UINT64_C(0)};
    send(UINT64_C(0xa208), imm(7, 0, 7), m_insn(3, 1, 2, 0), 2, 0, 0);
    drain();
    inject_enable = 0;
    send(UINT64_C(0xa208), imm(7, 0, 7), m_insn(3, 1, 2, 0));
    drain();
    // A returned product writes immediately; even a read-modify-write consumer
    // can issue without a deferred WAW tail after the fixed arithmetic latency.
    reset_core();
    send(UINT64_C(0xab00), imm(1, 0, 17), imm(2, 0, 3));
    drain();
    send(UINT64_C(0xab08), m_insn(3, 1, 2, 0), imm(8, 0, 8));
    send(UINT64_C(0xab10), imm(3, 3, 1), imm(5, 3, 2));
    send(UINT64_C(0xab18), imm(6, 3, 3), imm(7, 3, 4));
    drain();
    CHECK(multiply_mem_cycle >= 0 &&
          dependent_mem_cycle - multiply_mem_cycle <= 4);
    // More independent work than the arithmetic depth must still sustain one
    // direct write per cycle, without completion-buffer capacity admission.
    reset_core();
    send(UINT64_C(0xac00), imm(1, 0, 17), imm(2, 0, 3));
    drain();
    for (int pair = 0; pair < 8; pair++)
      send(UINT64_C(0xac08) + ((pair * 8) & low_mask(64)),
           m_insn(3 + 2 * pair, 1, 2, 0), m_insn(4 + 2 * pair, 1, 2, 0));
    drain();
    CHECK(multiply_stream_count == 16);
    send(UINT64_C(0xac48), imm(20, 3, 1), imm(21, 18, 2));
    drain();
    // A MEM branch kills a younger EX launch but accepted older work survives.
    reset_core();
    send(UINT64_C(0x9600), imm(1, 0, 17), imm(2, 0, 3));
    drain();
    stop_at(UINT64_C(0x9608), UINT64_C(0x9700));
    send(UINT64_C(0x9608), jump(0, UINT64_C(0xf8)), m_insn(3, 1, 2, 0), 2, 1,
         0);
    drain();
    send(UINT64_C(0x9700), imm(3, 0, 5), imm(4, 3, 1));
    drain();
    csr_access(UINT64_C(0x9708), 2, 5, 0, UINT64_C(0x301),
               UINT64_C(0x8000000000141187));
    // Complete B catalog in both issue slots, with independent paired B work
    // and dependent consumers. Dirty upper words expose .UW/word/unary shaping.
    for (int scenario = 0; scenario < 7; scenario++) {
      std::uint64_t pc;
      std::uint64_t a, b;
      int shift;
      pc = UINT64_C(0xb000);
      reset_core();
      switch (scenario) {
      case 0: {
        {
          a = 0;
          b = 0;
          shift = 0;
        }
      } break;
      case 1: {
        {
          a = UINT64_MAX;
          b = UINT64_MAX;
          shift = 63;
        }
      } break;
      case 2: {
        {
          a = UINT64_C(0x800000007fffffff);
          b = UINT64_C(0x8000000000000020);
          shift = 32;
        }
      } break;
      case 3: {
        {
          a = UINT64_C(0x123456789abcdef);
          b = UINT64_C(0xfedcba987654321f);
          shift = 31;
        }
      } break;
      case 4: {
        {
          a = UINT64_C(0x8000000080000001);
          b = UINT64_C(0x8000000000000041);
          shift = 1;
        }
      } break;
      case 5: {
        {
          a = UINT64_C(0x8000000000000000);
          b = 63;
          shift = 63;
        }
      } break;
      case 6: {
        {
          a = 1;
          b = 64;
          shift = 0;
        }
      } break;
      }
      constant64(pc, 1, a);
      constant64(pc, 2, b);
      drain();
      for (int operation = 0; operation < 40; operation++) {
        send(pc, b_insn(operation, 3, 1, 2, shift),
             b_insn((operation + 1) % 40, 4, 1, 2, shift));
        pc += 8;
        send(pc, imm(5, 3, 1), regop(6, 4, 3, 4));
        pc += 8;
      }
      drain();
      CHECK(model[0] == 0);
    }
    // Unary and shift-immediate encoding bits are not a GPR dependency. The
    // pending divider owns x2 while CPOP (sv_slice(imm,0,(4)-(0)+1)=2) executes without waiting.
    reset_core();
    {
      std::uint64_t pc;
      pc = UINT64_C(0xc000);
      constant64(pc, 1, UINT64_C(0x7fffffffffffffff));
      send(pc, imm(2, 0, 3), imm(3, 0, 63));
      drain();
    }
    send(UINT64_C(0xc100), m_insn(2, 1, 2, 4), b_insn(15, 4, 1, 0));
    send(UINT64_C(0xc108), b_insn(33, 5, 1, 0, 2), b_insn(34, 6, 1, 3));
    for (unsigned repeat_index = 0; repeat_index < (12); ++repeat_index)
      tick();
    CHECK(expected.size() == 0 && expected_completions.size() == 1);
    send(UINT64_C(0xc110), b_insn(0, 7, 2, 3), b_insn(31, 0, 1, 0));
    drain();
    // A deferred return feeding B and a B result feeding M share the ordinary
    // forwarding policy; WAW against a deferred owner still waits for RF write.
    send(UINT64_C(0xc118), m_insn(8, 1, 3, 0), b_insn(24, 9, 1, 3));
    send(UINT64_C(0xc120), b_insn(21, 10, 8, 0), m_insn(11, 9, 3, 0));
    send(UINT64_C(0xc128), b_insn(29, 8, 10, 0), b_insn(38, 12, 11, 3));
    drain();

    // Both conditional-zero operations use every bit of rs2. Independent pairs
    // exercise both slots; following consumers check ordinary EX/MEM forwarding.
    for (int scenario = 0; scenario < 5; scenario++) {
      std::uint64_t pc;
      std::uint64_t a;
      pc = UINT64_C(0xd000);
      reset_core();
      switch (scenario) {
      case 0: {
        a = 0;
      } break;
      case 1: {
        a = UINT64_MAX;
      } break;
      case 2: {
        a = UINT64_C(0x8000000000000000);
      } break;
      case 3: {
        a = UINT64_C(0x123456789abcdef);
      } break;
      case 4: {
        a = 1;
      } break;
      }
      constant64(pc, 1, a);
      drain();
      for (int condition = 0; condition < 4; condition++) {
        std::uint64_t b;
        switch (condition) {
        case 0: {
          b = 0;
        } break;
        case 1: {
          b = 1;
        } break;
        case 2: {
          b = UINT64_C(0x8000000000000000);
        } break;
        case 3: {
          b = UINT64_MAX;
        } break;
        }
        constant64(pc, 2, b);
        drain();
        for (int swap = 0; swap < 2; swap++) {
          send(pc, regop(3, 1, 2, swap == 0 ? 5 : 7, 7),
               regop(4, 1, 2, swap == 0 ? 7 : 5, 7));
          pc += 8;
          send(pc, imm(5, 3, 1), regop(6, 4, 3));
          pc += 8;
        }
        // x0 in either source, discarded writes, and same-group RAW/WAW.
        send(pc, regop(0, 1, 2, 5, 7), regop(7, 0, 2, 7, 7));
        pc += 8;
        send(pc, regop(8, 1, 0, 5, 7), regop(9, 1, 0, 7, 7));
        pc += 8;
        send(pc, regop(8, 1, 2, 7, 7), regop(8, 1, 2, 5, 7));
        pc += 8;
        send(pc, regop(9, 1, 2, 5, 7), regop(10, 9, 2, 7, 7));
        pc += 8;
        drain();
      }
    }
    // Every MOP.R and MOP.RR index in each age slot writes zero, independent
    // of encoded register fields, and forwards that zero to dependent consumers.
    reset_core();
    {
      std::uint64_t pc;
      pc = UINT64_C(0xe000);
      constant64(pc, 1, UINT64_MAX);
      constant64(pc, 2, UINT64_C(0xfedcba9876543210));
      drain();
      for (int two_sources = 0; two_sources < 2; two_sources++) {
        for (int index = 0; index < (two_sources != 0 ? 8 : 32); index++) {
          send(pc, mop_insn(two_sources != 0, index, 3, 1, 2),
               mop_insn(two_sources != 0, index, 4, 2, 1));
          pc += 8;
          send(pc, imm(5, 3, 1), imm(6, 4, 2));
          pc += 8;
        }
      }
      send(pc, mop_insn(0, 31, 0, 1), mop_insn(1, 7, 7, 0, 0));
      pc += 8;
      send(pc, imm(8, 0, 19), mop_insn(1, 0, 8, 8, 8));
      pc += 8;
      send(pc, imm(9, 8, 1), regop(10, 1, 9, 7, 7));
      drain();
    }
    CHECK(conditional_dual >= 40 && mop_dual >= 40);

    // MOP encoded sources must not wait for either pending deferred register.
    // Conditional-zero must wait for a real rs2 dependency, even when rs1 is x0.
    reset_core();
    {
      std::uint64_t pc;
      pc = UINT64_C(0xf000);
      constant64(pc, 1, UINT64_C(0x7fffffffffffffff));
      send(pc, imm(2, 0, 3), 0, 1);
      drain();
    }
    hold_responses = 1;
    send(UINT64_C(0xf100), m_insn(10, 1, 2, 4), imm(30, 0, 0, 3, UINT64_C(3)));
    send(UINT64_C(0xf108), mop_insn(0, 2, 3, 10), mop_insn(1, 7, 4, 30, 10));
    for (unsigned repeat_index = 0; repeat_index < (12); ++repeat_index)
      tick();
    CHECK(expected.size() == 0 && expected_completions.size() == 2);
    send(UINT64_C(0xf110), regop(5, 0, 30, 5, 7), imm(6, 0, 6));
    for (unsigned repeat_index = 0; repeat_index < (12); ++repeat_index)
      tick();
    CHECK(expected.size() == 2 && response_count == 1);
    hold_responses = 0;
    drain();
    // Destination WAW still waits, even though neither MOP source is read.
    reset_core();
    hold_responses = 1;
    send(UINT64_C(0xf200), imm(30, 0, 0, 3, UINT64_C(3)), imm(1, 0, 1));
    send(UINT64_C(0xf208), mop_insn(1, 3, 30, 0, 0), imm(2, 30, 1));
    for (unsigned repeat_index = 0; repeat_index < (12); ++repeat_index)
      tick();
    CHECK(expected.size() == 2 && expected_completions.size() == 1);
    hold_responses = 0;
    drain();

    // Wrong-path results and SYSTEM-opcode lookalikes must not commit. Faults
    // in the younger slot preserve the older MOP/conditional retirement.
    for (int lane = 0; lane < 2; lane++) {
      std::uint32_t invalid_word;
      reset_core();
      invalid_word = mop_insn(lane != 0, 0, 3, 1, 2) ^ UINT64_C(0x20000000);
      stop_at(((UINT64_C(0xf300) + lane * 4) & low_mask(64)),
              ((UINT64_C(0xf300) + lane * 4) & low_mask(64)), 1, 2,
              ((invalid_word)&low_mask(64)));
      if (lane == 0)
        send(UINT64_C(0xf300), invalid_word, regop(4, 0, 0, 7, 7), 2, 0, 0);
      else
        send(UINT64_C(0xf300), mop_insn(0, 0, 4, 1), invalid_word, 2, 1, 0);
      drain();
      stop_at(UINT64_C(0xf400), UINT64_C(0xf440));
      send(UINT64_C(0xf400), jump(0, 64), mop_insn(1, 7, 5, 1, 2), 2, 1, 0);
      send(UINT64_C(0xf408), regop(6, 1, 0, 7, 7), mop_insn(0, 0, 7, 1), 2, 0,
           0);
      drain();
      send(UINT64_C(0xf440), imm(8, 5, 1), imm(9, 6, 1));
      drain();
    }
    // A same-destination younger load can hit at WB or reserve a deferred write.
    for (int slow = 0; slow < 2; slow++) {
      int before_waw;
      reset_core();
      lookup_mode = slow == 0 ? 1 : 0;
      hold_responses = slow != 0;
      send(UINT64_C(0x10000), imm(1, 0, UINT64_C(0x300)), imm(2, 0, 3));
      drain();
      before_waw = slow == 0 ? waw_dual : waw_deferred;
      send(UINT64_C(0x10008), imm(10, 0, 91), imm(10, 1, 0, 3, UINT64_C(3)));
      if (slow != 0) {
        for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
          tick();
        CHECK(expected.size() == 0 && response_count == 1 &&
              waw_deferred == before_waw + 1);
      }
      send(UINT64_C(0x10010), imm(11, 10, 1), imm(12, 0, 12));
      if (slow != 0) {
        for (int repeat_index = 0; repeat_index < (6); ++repeat_index) {
          tick();
          CHECK(issued == 0);
        }
        hold_responses = 0;
      }
      drain();
      CHECK((slow == 0 ? waw_dual : waw_deferred) == before_waw + 1);
      send(UINT64_C(0x10018), imm(13, 10, 0), 0, 1);
      drain();
    }

    // Younger multiply/divide writes also follow the older normal-WB write.
    for (int operation = 0; operation < 2; operation++) {
      int before_waw;
      reset_core();
      send(UINT64_C(0x10100), imm(1, 0, 21), imm(2, 0, 3));
      drain();
      before_waw = waw_deferred;
      send(UINT64_C(0x10108), imm(10, 0, 91),
           m_insn(10, 1, 2, operation == 0 ? 0 : 4));
      send(UINT64_C(0x10110), imm(11, 10, 1), imm(12, 0, 12));
      drain();
      CHECK(waw_deferred == before_waw + 1);
      send(UINT64_C(0x10118), imm(13, 10, 0), 0, 1);
      drain();
    }

    // A deferred older writer must finish before a same-destination overwrite.
    for (int service = 0; service < 3; service++) {
      int before_waw;
      reset_core();
      send(UINT64_C(0x10140), imm(1, 0, UINT64_C(0x300)), imm(2, 0, 3));
      drain();
      before_waw = waw_dual + waw_deferred;
      hold_responses = service == 0;
      send(UINT64_C(0x10148),
           service == 0 ? imm(10, 1, 0, 3, UINT64_C(3))
                        : m_insn(10, 1, 2, service == 1 ? 0 : 4),
           imm(10, 0, 91));
      if (service == 0) {
        for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
          tick();
        CHECK(expected.size() == 1 && response_count == 1);
        hold_responses = 0;
      }
      drain();
      CHECK(waw_dual + waw_deferred == before_waw);
      send(UINT64_C(0x10150), imm(11, 10, 0), 0, 1);
      drain();
    }

    // A retained misaligned younger load preserves the older write on a fault.
    for (int fault = 0; fault < 2; fault++) {
      reset_core();
      send(UINT64_C(0x10180), imm(1, 0, UINT64_C(0x301)), 0, 1);
      drain();
      split_fault = fault != 0;
      stop_at(UINT64_C(0x1018c), UINT64_C(0x10190), fault != 0 ? 1 : 3, 13,
              UINT64_C(0x304));
      send(UINT64_C(0x10188), imm(10, 0, 91), imm(10, 1, 0, 3, UINT64_C(3)), 2,
           1, fault == 0);
      drain();
      split_fault = 0;
      send(UINT64_C(0x10190), imm(11, 10, 0), 0, 1);
      drain();
    }

    // Younger lookup/admission faults and replay preserve the older RF value.
    for (int scenario = 0; scenario < 5; scenario++) {
      std::uint64_t pc;
      int disposition, cause, before_commits, before_waw;
      reset_core();
      send(UINT64_C(0x10200), imm(1, 0, UINT64_C(0x300)), imm(2, 0, 3));
      drain();
      pc = ((UINT64_C(0x10210) + scenario * 32) & low_mask(64));
      lookup_mode = scenario < 3 ? scenario + 3 : 0;
      inject_memory_fault = scenario == 3;
      fault_address = UINT64_C(0x300);
      block_requests = scenario == 4;
      disposition = scenario == 0 || scenario == 4 ? 2 : 1;
      cause = scenario == 1 ? 13 : 5;
      stop_at(pc + 4, pc + 4, disposition, ((cause)&low_mask(64)),
              UINT64_C(0x300));
      before_commits = commits;
      send(pc, imm(10, 0, 91), imm(10, 1, 0, 3, UINT64_C(3)), 2, 1, 0);
      drain();
      CHECK(commits == before_commits + 1);
      inject_memory_fault = 0;
      block_requests = 0;
      send(pc + 8, imm(11, 10, 0), 0, 1);
      drain();
      if (disposition == 2) {
        lookup_mode = 1;
        send(pc + 4, imm(10, 1, 0, 3, UINT64_C(3)), 0, 1);
        drain();
        send(pc + 12, imm(12, 10, 0), 0, 1);
        drain();
      }
      // An older fault cancels a same-destination younger write as well.
      inject_enable = 1;
      inject_pc = pc + 16;
      inject_result = {.pguest = {},
                       .pdisposition = UINT64_C(1),
                       .pcause = UINT64_C(5),
                       .pvalue = UINT64_C(0xdead)};
      stop_at(pc + 16, pc + 16, 1, 5, UINT64_C(0xdead));
      before_waw = waw_dual;
      send(pc + 16, imm(10, 0, 1), imm(10, 0, 2), 2, 0, 0);
      drain();
      CHECK(waw_dual == before_waw);
      inject_enable = 0;
      send(pc + 24, imm(13, 10, 0), 0, 1);
      drain();
    }
    // Both architectural values survive U/I folding, including signed overflow
    // of a 32-bit folded offset, PC carry/borrow, and RV64 modular wrap.
    for (int pc_case = 0; pc_case < 3; pc_case++) {
      for (int upper_case = 0; upper_case < 5; upper_case++) {
        for (int lower_case = 0; lower_case < 5; lower_case++) {
          for (int destination = 0; destination < 3; destination++) {
            std::uint64_t pc;
            std::uint32_t upper;
            int lower, rd, before_pairs, before_dual;
            reset_core();
            switch (pc_case) {
            case 0: {
              pc = UINT64_C(0xffe);
            } break;
            case 1: {
              pc = UINT64_C(0x80000000fffffff8);
            } break;
            case 2: {
              pc = UINT64_C(0xfffffffffffffff8);
            } break;
            }
            switch (upper_case) {
            case 0: {
              upper = 0;
            } break;
            case 1: {
              upper = 1;
            } break;
            case 2: {
              upper = UINT64_C(0x7ffff);
            } break;
            case 3: {
              upper = UINT64_C(0x80000);
            } break;
            case 4: {
              upper = UINT64_C(0xfffff);
            } break;
            }
            switch (lower_case) {
            case 0: {
              lower = -2048;
            } break;
            case 1: {
              lower = -1;
            } break;
            case 2: {
              lower = 0;
            } break;
            case 3: {
              lower = 1;
            } break;
            case 4: {
              lower = 2047;
            } break;
            }
            rd = destination == 0 ? 3 : destination == 1 ? 4 : 0;
            before_pairs = auipc_addi_pairs;
            before_dual = dual_commits;
            send(pc,
                 (field(upper, 20, 12) | field(UINT64_C(3), 5, 7) |
                  field(UINT64_C(23), 7, 0)),
                 imm(rd, 3, lower));
            send(pc + 8, imm(5, 3, 0), imm(6, rd, 0));
            drain();
            CHECK(auipc_addi_pairs == before_pairs + 1 &&
                  dual_commits >= before_dual + 1);
            send(pc + 16, imm(7, 3, 0), imm(8, rd, 0));
            drain();
          }
        }
      }
    }
    {
      int before_pairs;
      reset_core();
      before_pairs = auipc_addi_pairs;
      longest_dual_run = 0;
      dual_run = 0;
      for (int i = 0; i < 16; i++)
        send(UINT64_C(0x10800) + ((8 * i) & low_mask(64)),
             (field(((i - 8) & low_mask(20)), 20, 12) |
              field(UINT64_C(3), 5, 7) | field(UINT64_C(23), 7, 0)),
             imm(3, 3, i - 8));
      drain();
      CHECK(auipc_addi_pairs == before_pairs + 16 && longest_dual_run >= 12);
    }
    // Older/younger destination reservations still block the appropriate prefix;
    // an immediate's apparent rs2 field must not cause a false source hazard.
    for (int reservation = 0; reservation < 3; reservation++) {
      int before_pairs, before_commits, pending_rd;
      reset_core();
      hold_responses = 1;
      pending_rd = reservation == 0 ? 3 : reservation == 1 ? 4 : 2;
      send(UINT64_C(0x10900),
           imm(pending_rd, 0, UINT64_C(0x300), 3, UINT64_C(3)), 0, 1);
      for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
        tick();
      before_pairs = auipc_addi_pairs;
      before_commits = commits;
      send(UINT64_C(0x10904),
           (field(UINT64_C(1), 20, 12) | field(UINT64_C(3), 5, 7) |
            field(UINT64_C(23), 7, 0)),
           imm(4, 3, 2));
      for (unsigned repeat_index = 0; repeat_index < (6); ++repeat_index)
        tick();
      CHECK(commits == before_commits + (reservation == 0   ? 0
                                         : reservation == 1 ? 1
                                                            : 2));
      hold_responses = 0;
      drain();
      CHECK(auipc_addi_pairs == before_pairs + (reservation == 1 ? 0 : 1));
      send(UINT64_C(0x1090c), imm(5, 3, 0), imm(6, 4, 0));
      drain();
    }
    // Nearby operations retain normal RAW splitting, and x0 is not a producer.
    for (int scenario = 0; scenario < 4; scenario++) {
      std::uint32_t first, second;
      int before_pairs, before_dual;
      reset_core();
      before_pairs = auipc_addi_pairs;
      before_dual = dual_commits;
      first = (field(UINT64_C(1), 20, 12) | field(UINT64_C(3), 5, 7) |
               field(scenario == 2 ? UINT64_C(0x37) : UINT64_C(23), 7, 0));
      second = imm(4, 3, 7, scenario == 0 ? 4 : 0,
                   scenario == 1 ? UINT64_C(27) : UINT64_C(19));
      if (scenario == 3) {
        first = (field(UINT64_C(1), 20, 12) | field(UINT64_C(0), 5, 7) |
                 field(UINT64_C(23), 7, 0));
        second = imm(4, 0, 7);
      }
      send(UINT64_C(0x10920), first, second);
      drain();
      CHECK(auipc_addi_pairs == before_pairs &&
            dual_commits == before_dual + (scenario == 3 ? 1 : 0));
      send(UINT64_C(0x10928), imm(5, 3, 0), imm(6, 4, 0));
      drain();
    }
    // A stop in either member preserves only the successful prefix, including
    // the intermediate AUIPC value when the rejected ADDI targets the same RF entry.
    for (int lane = 0; lane < 2; lane++) {
      for (int replay = 0; replay < 2; replay++) {
        int before_pairs;
        reset_core();
        before_pairs = auipc_addi_pairs;
        inject_enable = 1;
        inject_pc = UINT64_C(0x10944) + ((4 * lane) & low_mask(64));
        inject_result = {.pguest = {},
                         .pdisposition = static_cast<uint8_t>(
                             ((replay != 0 ? 2 : 1) & low_mask(2))),
                         .pcause = UINT64_C(5),
                         .pvalue = UINT64_C(0xdead)};
        stop_at(inject_pc, inject_pc, replay != 0 ? 2 : 1, 5, UINT64_C(0xdead));
        send(UINT64_C(0x10944),
             (field(UINT64_C(0x80000), 20, 12) | field(UINT64_C(3), 5, 7) |
              field(UINT64_C(23), 7, 0)),
             imm(3, 3, -2048), 2, lane == 1, 0);
        drain();
        CHECK(auipc_addi_pairs == before_pairs + 1);
        inject_enable = 0;
        send(UINT64_C(0x1094c), imm(5, 3, 0), 0, 1);
        drain();
        if (replay != 0) {
          if (lane == 0)
            send(UINT64_C(0x10944),
                 (field(UINT64_C(0x80000), 20, 12) | field(UINT64_C(3), 5, 7) |
                  field(UINT64_C(23), 7, 0)),
                 imm(3, 3, -2048));
          else
            send(UINT64_C(0x10948), imm(3, 3, -2048), 0, 1);
          drain();
          send(UINT64_C(0x10954), imm(6, 3, 0), 0, 1);
          drain();
        }
      }
    }
    reset_core();
    send(UINT64_C(0x10960),
         (field(UINT64_C(1), 20, 12) | field(UINT64_C(3), 5, 7) |
          field(UINT64_C(23), 7, 0)),
         imm(3, 3, 1), 2, 0, 0);
    reset_core();
    send(UINT64_C(0x10968), imm(5, 3, 0), 0, 1);
    drain();

    // All ordinary load widths can use the current older ALU result, not stale RF.
    reset_core();
    lookup_mode = 1;
    send(UINT64_C(0x11000), imm(1, 0, UINT64_C(0x100)),
         imm(2, 0, UINT64_C(0x200)));
    drain();
    for (int same_rd = 0; same_rd < 2; same_rd++) {
      for (int width = 0; width < 7; width++) {
        int before_pairs;
        send(UINT64_C(0x11008), imm(3, 0, UINT64_C(0x280)), 0, 1);
        drain();
        before_pairs = address_pairs;
        send(UINT64_C(0x11010), regop(3, 1, 2),
             imm(same_rd != 0 ? 3 : 4, 3, 0, width, UINT64_C(3)));
        send(UINT64_C(0x11018), imm(5, same_rd != 0 ? 3 : 4, 1), 0, 1);
        drain();
        CHECK(address_pairs == before_pairs + 1);
        send(UINT64_C(0x11020), imm(6, 3, 0), imm(7, same_rd != 0 ? 3 : 4, 0));
        drain();
      }
    }
    // Admission fixes the bypass bit even when the pair reads an older EX result.
    {
      int before_pairs;
      before_pairs = address_pairs;
      send(UINT64_C(0x11028), imm(3, 0, UINT64_C(0x288)), 0, 1);
      send(UINT64_C(0x1102c), imm(3, 0, UINT64_C(0x308)),
           imm(4, 3, 0, 3, UINT64_C(3)));
      send(UINT64_C(0x11034), imm(5, 4, 0), 0, 1);
      drain();
      CHECK(address_pairs == before_pairs + 1);
      // AUIPC uses its PC result; the load can overwrite that same register.
      before_pairs = address_pairs;
      send(UINT64_C(0x320),
           (field(UINT64_C(0), 20, 12) | field(UINT64_C(3), 5, 7) |
            field(UINT64_C(23), 7, 0)),
           imm(3, 3, 0, 3, UINT64_C(3)));
      drain();
      CHECK(address_pairs == before_pairs + 1);
      send(UINT64_C(0x328), imm(5, 3, 0), 0, 1);
      drain();
      // Word and shift producers use their selected ALU result, not just the adder.
      before_pairs = address_pairs;
      send(UINT64_C(0x11038), imm(3, 1, 1, 1), imm(4, 3, 0, 3, UINT64_C(3)));
      drain();
      send(UINT64_C(0x11040), imm(3, 1, UINT64_C(0x200), 0, UINT64_C(27)),
           imm(0, 3, 0, 3, UINT64_C(3)));
      drain();
      CHECK(address_pairs == before_pairs + 2);
      send(UINT64_C(0x11048), imm(5, 3, 0), imm(6, 0, 0));
      drain();
      // An x0 "producer" must not redirect_out a load away from architectural zero.
      before_pairs = address_pairs;
      send(UINT64_C(0x11050), imm(0, 0, UINT64_C(0x300)),
           imm(4, 0, 0, 3, UINT64_C(3)));
      drain();
      CHECK(address_pairs == before_pairs);
      dual_run = 0;
      longest_dual_run = 0;
      before_pairs = address_pairs;
      for (int i = 0; i < 16; i++)
        send(((UINT64_C(0x11060) + 8 * i) & low_mask(64)),
             imm(3, 0, UINT64_C(0x300) + 8 * i),
             imm(10 + i, 3, 0, 3, UINT64_C(3)));
      drain();
      CHECK(address_pairs == before_pairs + 16 && longest_dual_run >= 12);
    }

    // A paired miss reserves the younger value and blocks consumers until return.
    for (int same_rd = 0; same_rd < 2; same_rd++) {
      int before_pairs;
      reset_core();
      lookup_mode = 0;
      hold_responses = 1;
      before_pairs = address_pairs;
      send(UINT64_C(0x11100), imm(3, 0, UINT64_C(0x300)),
           imm(same_rd != 0 ? 3 : 4, 3, 0, 3, UINT64_C(3)));
      for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
        tick();
      CHECK(address_pairs == before_pairs + 1 && expected.size() == 0 &&
            response_count == 1);
      send(UINT64_C(0x11108), imm(5, same_rd != 0 ? 3 : 4, 0), 0, 1);
      for (int repeat_index = 0; repeat_index < (6); ++repeat_index) {
        tick();
        CHECK(issued == 0);
      }
      hold_responses = 0;
      drain();
      send(UINT64_C(0x1110c), imm(6, 3, 0), imm(7, same_rd != 0 ? 3 : 4, 0));
      drain();
    }
    // Waiving the load's base read does not waive the producer's older WAW hazard.
    reset_core();
    hold_responses = 1;
    send(UINT64_C(0x11120), imm(3, 0, 0, 3, UINT64_C(3)), 0, 1);
    for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
      tick();
    {
      int before_pairs;
      before_pairs = address_pairs;
      lookup_mode = 1;
      send(UINT64_C(0x11124), imm(3, 0, UINT64_C(0x300)),
           imm(4, 3, 0, 3, UINT64_C(3)));
      for (int repeat_index = 0; repeat_index < (6); ++repeat_index) {
        tick();
        CHECK(issued == 0);
      }
      hold_responses = 0;
      drain();
      CHECK(address_pairs == before_pairs + 1);
      send(UINT64_C(0x1112c), imm(5, 4, 0), 0, 1);
      drain();
    }

    // Offset and resource boundaries retain ordinary dependent instruction ordering.
    for (int scenario = 0; scenario < 6; scenario++) {
      int before_pairs;
      std::uint32_t first, second;
      reset_core();
      lookup_mode = scenario == 5 ? 0 : 1;
      send(UINT64_C(0x11200), imm(1, 0, UINT64_C(0x180)), imm(2, 0, 2));
      drain();
      switch (scenario) {
      case 0: {
        {
          first = imm(3, 0, UINT64_C(0x300));
          second = imm(4, 3, 8, 3, UINT64_C(3));
        }
      } break;
      case 1: {
        {
          first = imm(3, 0, UINT64_C(0x308));
          second = imm(4, 3, -8, 3, UINT64_C(3));
        }
      } break;
      case 2: {
        {
          first = m_insn(3, 1, 2, 0);
          second = imm(4, 3, 0, 3, UINT64_C(3));
        }
      } break;
      case 3: {
        {
          first = m_insn(3, 1, 2, 4);
          second = imm(4, 3, 0, 3, UINT64_C(3));
        }
      } break;
      case 4: {
        {
          first = imm(3, 0, UINT64_C(0x300));
          second = store(2, 3, 0, 3);
        }
      } break;
      case 5: {
        {
          first = imm(3, 0, UINT64_C(0x300));
          second = atomic_insn(2, 3, 4, 3);
        }
      } break;
      }
      before_pairs = address_pairs;
      send(UINT64_C(0x11208), first, second);
      drain();
      CHECK(address_pairs == before_pairs);
      send(UINT64_C(0x11210), imm(5, 3, 0), imm(6, 4, 0));
      drain();
    }

    // Fault/replay reports the bypassed address, while the producer remains committed.
    for (int scenario = 0; scenario < 5; scenario++) {
      int before_pairs;
      reset_core();
      lookup_mode = scenario < 3 ? scenario + 3 : 0;
      inject_memory_fault = scenario == 3;
      fault_address = UINT64_C(0x300);
      block_requests = scenario == 4;
      stop_at(UINT64_C(0x11304), UINT64_C(0x11304),
              scenario == 0 || scenario == 4 ? 2 : 1, scenario == 1 ? 13 : 5,
              UINT64_C(0x300));
      before_pairs = address_pairs;
      send(UINT64_C(0x11300), imm(3, 0, UINT64_C(0x300)),
           imm(3, 3, 0, 3, UINT64_C(3)), 2, 1, 0);
      drain();
      CHECK(address_pairs == before_pairs + 1);
      inject_memory_fault = 0;
      block_requests = 0;
      send(UINT64_C(0x11308), imm(4, 3, 0), 0, 1);
      drain();
      if (scenario == 0 || scenario == 4) {
        lookup_mode = 1;
        send(UINT64_C(0x11304), imm(3, 3, 0, 3, UINT64_C(3)), 0, 1);
        drain();
        send(UINT64_C(0x1130c), imm(4, 3, 0), 0, 1);
        drain();
      }
    }
    // Misaligned zero-offset loads retain the bypassed address through the split owner.
    for (int fault = 0; fault < 2; fault++) {
      int before_pairs;
      reset_core();
      split_fault = fault != 0;
      before_pairs = address_pairs;
      stop_at(UINT64_C(0x11404), UINT64_C(0x11408), fault != 0 ? 1 : 3, 13,
              UINT64_C(0x304));
      send(UINT64_C(0x11400), imm(3, 0, UINT64_C(0x301)),
           imm(3, 3, 0, 3, UINT64_C(3)), 2, 1, fault == 0);
      drain();
      CHECK(address_pairs == before_pairs + 1);
      split_fault = 0;
      send(UINT64_C(0x11408), imm(4, 3, 0), 0, 1);
      drain();
    }
    // An older fault may perform a speculative lookup, never younger authorization.
    reset_core();
    inject_enable = 1;
    inject_pc = UINT64_C(0x11500);
    inject_result = {.pguest = {},
                     .pdisposition = UINT64_C(1),
                     .pcause = UINT64_C(5),
                     .pvalue = UINT64_C(0xdead)};
    stop_at(UINT64_C(0x11500), UINT64_C(0x11500), 1, 5, UINT64_C(0xdead));
    send(UINT64_C(0x11500), imm(3, 0, UINT64_C(0x300)),
         imm(3, 3, 0, 3, UINT64_C(3)), 2, 0, 0);
    drain();
    inject_enable = 0;
    send(UINT64_C(0x11508), imm(4, 3, 0), 0, 1);
    drain();
    // Reset cancels a pair already admitted to EX, including its bypass selection.
    send(UINT64_C(0x11510), imm(3, 0, UINT64_C(0x300)),
         imm(3, 3, 0, 3, UINT64_C(3)), 2, 0, 0);
    tick();
    reset_core();
    drain();
    send(UINT64_C(0x11518), imm(4, 3, 0), 0, 1);
    drain();
    // Every store width/lane consumes the selected ALU result, not the stale rs2.
    for (int slow = 0; slow < 2; slow++) {
      reset_core();
      lookup_mode = slow == 0 ? 1 : 0;
      send(UINT64_C(0x12000), imm(1, 0, UINT64_C(0x300)), imm(2, 0, -128));
      drain();
      send(UINT64_C(0x12008), imm(4, 0, 65), 0, 1);
      drain();
      for (int width = 0; width < 4; width++) {
        for (int lane = 0; lane < 8; lane += 1 << width) {
          int before_pairs;
          before_pairs = store_data_pairs;
          send(UINT64_C(0x12010), imm(3, 0, 17), 0, 1);
          send(UINT64_C(0x12014),
               width % 2 == 0 ? regop(3, 2, 4) : imm(3, 2, 3, 1, UINT64_C(27)),
               store(3, 1, (slow == 0 ? 24 : -8) + lane, width));
          drain();
          CHECK(store_data_pairs == before_pairs + 1);
          send(UINT64_C(0x1201c),
               imm(5, 1, (slow == 0 ? 24 : -8) + lane, width, UINT64_C(3)),
               imm(6, 3, 0));
          drain();
        }
      }
    }
    // A stream keeps both lanes busy; a nonzero negative offset needs no extra adder.
    {
      int before_pairs;
      reset_core();
      lookup_mode = 1;
      send(UINT64_C(0x12040), imm(1, 0, UINT64_C(0x300)), 0, 1);
      drain();
      before_pairs = store_data_pairs;
      dual_run = 0;
      longest_dual_run = 0;
      for (int i = 0; i < 16; i++)
        send(((UINT64_C(0x12048) + 8 * i) & low_mask(64)), imm(3, 0, i - 8),
             store(3, 1, -8, 3));
      drain();
      CHECK(store_data_pairs == before_pairs + 16 && longest_dual_run >= 12);
    }
    // Producer WAW admission remains mandatory even though its result replaces rs2.
    {
      int before_pairs;
      reset_core();
      hold_responses = 1;
      send(UINT64_C(0x12100), imm(1, 0, UINT64_C(0x300)), 0, 1);
      drain();
      send(UINT64_C(0x12104), imm(3, 1, 0, 3, UINT64_C(3)), 0, 1);
      for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
        tick();
      lookup_mode = 1;
      before_pairs = store_data_pairs;
      send(UINT64_C(0x12108), imm(3, 0, -93), store(3, 1, 8, 3));
      for (int repeat_index = 0; repeat_index < (6); ++repeat_index) {
        tick();
        CHECK(issued == 0);
      }
      hold_responses = 0;
      drain();
      CHECK(store_data_pairs == before_pairs + 1);
    }
    // A dependent address and deferred producers still split; x0 supplies zero.
    for (int scenario = 0; scenario < 4; scenario++) {
      int before_pairs;
      std::uint32_t first, second;
      reset_core();
      lookup_mode = 1;
      send(UINT64_C(0x12140), imm(1, 0, UINT64_C(0x300)), imm(2, 0, 2));
      drain();
      switch (scenario) {
      case 0: {
        {
          first = imm(3, 0, UINT64_C(0x308));
          second = store(3, 3, 0, 3);
        }
      } break;
      case 1: {
        {
          first = m_insn(3, 1, 2, 0);
          second = store(3, 1, 0, 3);
        }
      } break;
      case 2: {
        {
          first = imm(3, 1, 0, 3, UINT64_C(3));
          second = store(3, 1, 8, 3);
        }
      } break;
      case 3: {
        {
          first = imm(0, 0, 99);
          second = store(0, 1, 0, 3);
        }
      } break;
      }
      before_pairs = store_data_pairs;
      send(UINT64_C(0x12148), first, second);
      drain();
      CHECK(store_data_pairs == before_pairs);
      send(UINT64_C(0x12150),
           imm(4, scenario == 0 ? 3 : 1, scenario == 2 ? 8 : 0, 3, UINT64_C(3)),
           0, 1);
      drain();
    }
    // Rejection retires the producer once and publishes no younger store effect.
    for (int scenario = 0; scenario < 6; scenario++) {
      int before_pairs, before_stores, before_commits;
      bool replay;
      reset_core();
      send(UINT64_C(0x12200), imm(1, 0, UINT64_C(0x300)), 0, 1);
      drain();
      lookup_mode = scenario < 3 ? scenario + 3 : scenario == 5 ? 1 : 0;
      inject_memory_fault = scenario == 3;
      fault_address = UINT64_C(0x308);
      block_requests = scenario == 4;
      block_stores = scenario == 5;
      replay = scenario == 0 || scenario >= 4;
      stop_at(UINT64_C(0x1220c), UINT64_C(0x1220c), replay ? 2 : 1,
              scenario == 1 ? 15 : 7, UINT64_C(0x308));
      before_pairs = store_data_pairs;
      before_stores = stores;
      before_commits = commits;
      send(UINT64_C(0x12208), imm(3, 0, -111), store(3, 1, 8, 3), 2, 1, 0);
      drain();
      CHECK(store_data_pairs == before_pairs + 1 && stores == before_stores &&
            commits == before_commits + 1);
      inject_memory_fault = 0;
      block_requests = 0;
      block_stores = 0;
      send(UINT64_C(0x12210), imm(4, 3, 0), 0, 1);
      drain();
      if (replay) {
        lookup_mode = 1;
        send(UINT64_C(0x1220c), store(3, 1, 8, 3), 0, 1);
        drain();
        send(UINT64_C(0x12214), imm(5, 1, 8, 3, UINT64_C(3)), 0, 1);
        drain();
      }
    }
    // Split service retains raw bypassed data, including bytes across beat boundaries.
    for (int width = 1; width < 4; width++) {
      int before_pairs, before_splits;
      reset_core();
      send(UINT64_C(0x12300), imm(1, 0, UINT64_C(0x300)), 0, 1);
      drain();
      before_pairs = store_data_pairs;
      before_splits = split_requests;
      stop_at(UINT64_C(0x1230c), UINT64_C(0x12310), 3);
      send(UINT64_C(0x12308), imm(3, 0, -117), store(3, 1, 7, width));
      drain();
      CHECK(store_data_pairs == before_pairs + 1 &&
            split_requests == before_splits + 1);
      lookup_mode = 1;
      for (int b = 0; b < (1 << width); b++) {
        send(((UINT64_C(0x12310) + 4 * b) & low_mask(64)),
             imm(4, 1, 7 + b, 4, UINT64_C(3)), 0, 1);
        drain();
      }
    }
    // Older failure and reset cancel speculative store data without a memory effect.
    reset_core();
    lookup_mode = 1;
    send(UINT64_C(0x12400), imm(1, 0, UINT64_C(0x300)), 0, 1);
    drain();
    {
      int before_stores;
      before_stores = stores;
      inject_enable = 1;
      inject_pc = UINT64_C(0x12408);
      inject_result = {.pguest = {},
                       .pdisposition = UINT64_C(1),
                       .pcause = UINT64_C(5),
                       .pvalue = UINT64_C(0xdead)};
      stop_at(UINT64_C(0x12408), UINT64_C(0x12408), 1, 5, UINT64_C(0xdead));
      send(UINT64_C(0x12408), imm(3, 0, 77), store(3, 1, 0, 3), 2, 0, 0);
      drain();
      CHECK(stores == before_stores);
      inject_enable = 0;
      send(UINT64_C(0x12410), imm(3, 0, 88), store(3, 1, 0, 3), 2, 0, 0);
      tick();
      reset_core();
      drain();
      CHECK(stores == before_stores);
    }
    // NTL applies to the next instruction, including a younger co-retiring
    // memory operation. An ordinary target consumes it; hints replace it.
    for (int locality = 1; locality <= 4; locality++) {
      std::uint32_t ntl;
      int before_dual;
      ntl = UINT64_C(0x33) | (((locality + 1) & low_mask(32)) << 20);
      reset_core();
      before_dual = dual_commits;
      expect_instruction(UINT64_C(0x13000), ntl);
      expect_instruction(UINT64_C(0x13004),
                         imm(3, 0, UINT64_C(0x300), 3, UINT64_C(3)));
      expected_requests.back().plocality = ((locality)&low_mask(3));
      send(UINT64_C(0x13000), ntl, imm(3, 0, UINT64_C(0x300), 3, UINT64_C(3)),
           2, 0, 0);
      drain();
      CHECK(dual_commits == before_dual + 1);
      send(UINT64_C(0x13008), imm(4, 0, UINT64_C(0x300), 3, UINT64_C(3)), 0, 1);
      drain();
      send(UINT64_C(0x1300c), imm(5, 0, 17), ntl);
      drain();
      expect_instruction(UINT64_C(0x13014), store(5, 0, UINT64_C(0x308), 3));
      expected_requests.back().plocality = ((locality)&low_mask(3));
      send(UINT64_C(0x13014), store(5, 0, UINT64_C(0x308), 3), 0, 1, 0, 0);
      drain();
      send(UINT64_C(0x13018), ntl, imm(6, 0, 1));
      drain();
      send(UINT64_C(0x13020), imm(7, 0, UINT64_C(0x300), 3, UINT64_C(3)), 0, 1);
      drain();
    }
    reset_core();
    send(UINT64_C(0x13100), UINT64_C(0x200033), UINT64_C(0x500033));
    drain();
    expect_instruction(UINT64_C(0x13108),
                       imm(3, 0, UINT64_C(0x300), 3, UINT64_C(3)));
    expected_requests.back().plocality = 4;
    send(UINT64_C(0x13108), imm(3, 0, UINT64_C(0x300), 3, UINT64_C(3)), 0, 1, 0,
         0);
    drain();
    // The encoded selector must not wait on its apparent rs2 register.
    {
      int before_commits;
      reset_core();
      hold_responses = 1;
      send(UINT64_C(0x13180), imm(2, 0, UINT64_C(0x300), 3, UINT64_C(3)), 0, 1);
      for (unsigned repeat_index = 0; repeat_index < (10); ++repeat_index)
        tick();
      before_commits = commits;
      send(UINT64_C(0x13184), UINT64_C(0x200033), imm(3, 0, 1));
      for (unsigned repeat_index = 0; repeat_index < (10); ++repeat_index)
        tick();
      CHECK(commits == before_commits + 2 && response_count == 1);
      hold_responses = 0;
      drain();
    }
    // Rejecting the younger target preserves the older retired hint across replay.
    reset_core();
    block_requests = 1;
    stop_at(UINT64_C(0x13204), UINT64_C(0x13204), 2);
    send(UINT64_C(0x13200), UINT64_C(0x400033),
         imm(3, 0, UINT64_C(0x300), 3, UINT64_C(3)), 2, 1, 0);
    drain();
    block_requests = 0;
    expect_instruction(UINT64_C(0x13204),
                       imm(3, 0, UINT64_C(0x300), 3, UINT64_C(3)));
    expected_requests.back().plocality = 3;
    send(UINT64_C(0x13204), imm(3, 0, UINT64_C(0x300), 3, UINT64_C(3)), 0, 1, 0,
         0);
    drain();
    // A split owner retains its selector until the single completion retires.
    reset_core();
    expected_split_locality = 2;
    stop_at(UINT64_C(0x13304), UINT64_C(0x13308), 3);
    send(UINT64_C(0x13300), UINT64_C(0x300033),
         imm(3, 0, UINT64_C(0x307), 3, UINT64_C(3)));
    drain();
    send(UINT64_C(0x13308), imm(4, 0, UINT64_C(0x300), 3, UINT64_C(3)), 0, 1);
    drain();
    // A trapping target clears the hint before handler memory; a killed hint
    // must not establish pending state either.
    for (int fault_slot = 0; fault_slot < 2; fault_slot++) {
      reset_core();
      inject_enable = 1;
      inject_pc = ((UINT64_C(0x13400) + 4 * fault_slot) & low_mask(64));
      inject_result = {.pguest = {},
                       .pdisposition = UINT64_C(1),
                       .pcause = UINT64_C(5),
                       .pvalue = UINT64_C(0xdead)};
      stop_at(inject_pc, inject_pc, 1, 5, UINT64_C(0xdead));
      if (fault_slot == 1)
        send(UINT64_C(0x13400), UINT64_C(0x500033), imm(3, 0, 1), 2, 1, 0);
      else
        send(UINT64_C(0x13400), imm(3, 0, 1), UINT64_C(0x500033), 2, 0, 0);
      drain();
      inject_enable = 0;
      send(UINT64_C(0x13410), imm(4, 0, UINT64_C(0x300), 3, UINT64_C(3)), 0, 1);
      drain();
    }
    // M-mode always owns stimecmp; S-mode requires both STCE and TM, and
    // U-mode cannot access the supervisor CSR even with both gates open.
    for (int scenario = 0; scenario < 4; scenario++) {
      std::uint64_t pc;
      pc = UINT64_C(0x14100);
      reset_core();
      send(pc, imm(1, 0, 1000), 0, 1);
      pc += 4;
      drain();
      csr_access(pc, 1, 0, 1, UINT64_C(0x14d), 0);
      pc += 4;
      csr_access(pc, 2, 6, 0, UINT64_C(0x14d), 1000);
      pc += 4;
      if (scenario != 0) {
        constant64(pc, 1, UINT64_C(0x8000000000000000));
        drain();
        csr_access(pc, 1, 0, 1, UINT64_C(0x30a), 0);
        pc += 4;
      }
      csr_access(pc, 5, 0, scenario == 1 ? 0 : 2, UINT64_C(0x306), 0);
      pc += 4;
      constant64(pc, 1, scenario == 3 ? UINT64_C(0) : UINT64_C(0x800));
      drain();
      csr_access(pc, 1, 0, 1, UINT64_C(0x300), UINT64_C(0xa00000000));
      pc += 4;
      send(pc, imm(2, 0, UINT64_C(0x500)), 0, 1);
      pc += 4;
      drain();
      csr_access(pc, 1, 0, 2, UINT64_C(0x341), 0);
      pc += 4;
      expect_system(pc, UINT64_C(0x30200073));
      stop_at(pc, UINT64_C(0x500), 3);
      send(pc, UINT64_C(0x30200073), 0, 1, 0, 0);
      drain();
      if (scenario == 2)
        csr_access(UINT64_C(0x500), 2, 6, 0, UINT64_C(0x14d), 1000);
      else {
        stop_at(UINT64_C(0x500), 0, 1, 2,
                ((csr(2, 6, 0, UINT64_C(0x14d))) & low_mask(64)));
        send(UINT64_C(0x500), csr(2, 6, 0, UINT64_C(0x14d)), 0, 1, 0, 0);
        drain();
        csr_access(0, 2, 7, 0, UINT64_C(0x343),
                   ((csr(2, 6, 0, UINT64_C(0x14d))) & low_mask(64)));
      }
    }
    // A 64-bit comparison, read-only STIP, reprogramming, and locally enabled
    // WFI wake with global SIE clear. No external interrupt pin is asserted.
    {
      std::uint64_t pc;
      pc = UINT64_C(0x510);
      reset_core();
      supervisor_timer(0);
      csr_access(UINT64_C(0x500), 2, 6, 0, UINT64_C(0x144), 0);
      time_counter = timer_compare;
      csr_access(UINT64_C(0x504), 2, 6, 0, UINT64_C(0x144), 32);
      csr_access(UINT64_C(0x508), 1, 0, 0, UINT64_C(0x144), 32);
      csr_access(UINT64_C(0x50c), 2, 6, 0, UINT64_C(0x144), 32);
      constant64(pc, 1, timer_compare + 1);
      drain();
      csr_access(pc, 1, 0, 1, UINT64_C(0x14d), timer_compare);
      pc += 4;
      csr_access(pc, 2, 6, 0, UINT64_C(0x144), 0);
      pc += 4;
      expect_system(pc, UINT64_C(0x10500073));
      send(pc, UINT64_C(0x10500073), 0, 1, 0, 0);
      drain();
      CHECK(sleeping);
      stop_at(pc + 4, pc + 4, 3);
      time_counter = timer_compare + 1;
      drain();
      CHECK(!sleeping);
    }
    // Delivery stops before a live pair, follows an already retired pair,
    // drains an accepted load, and wakes WFI into a delegated S-mode handler.
    for (int scenario = 0; scenario < 4; scenario++) {
      std::uint64_t boundary;
      reset_core();
      supervisor_timer(1);
      boundary = scenario == 0   ? UINT64_C(0x500)
                 : scenario == 3 ? UINT64_C(0x504)
                                 : UINT64_C(0x508);
      if (scenario == 0) {
        send(UINT64_C(0x500), imm(8, 0, 8), imm(9, 0, 9), 2, 0, 0);
        until([&] { return memory_stage(0).pvalid; });
        falling();
      } else if (scenario == 3) {
        expect_system(UINT64_C(0x500), UINT64_C(0x10500073));
        send(UINT64_C(0x500), UINT64_C(0x10500073), 0, 1, 0, 0);
        drain();
        CHECK(sleeping);
      } else {
        hold_responses = scenario == 2;
        send(UINT64_C(0x500),
             scenario == 2 ? imm(8, 0, UINT64_C(0x300), 3, UINT64_C(3))
                           : imm(8, 0, 8),
             imm(9, 0, 9));
        for (unsigned repeat_index = 0; repeat_index < (10); ++repeat_index)
          tick();
      }
      stop_at(boundary, UINT64_C(0x700), 3);
      time_counter = timer_compare;
      if (scenario == 2) {
        for (unsigned repeat_index = 0; repeat_index < (10); ++repeat_index)
          tick();
        CHECK(response_count == 1 && expected_redirects.size() == 1);
        hold_responses = 0;
      }
      drain();
      csr_access(UINT64_C(0x700), 2, 10, 0, UINT64_C(0x141), boundary);
      csr_access(UINT64_C(0x704), 2, 11, 0, UINT64_C(0x142),
                 UINT64_C(0x8000000000000005));
      csr_access(UINT64_C(0x708), 2, 12, 0, UINT64_C(0x143), 0);
      CHECK(!sleeping && (interrupts.pmachine_usoftware == 0 &&
                          interrupts.psupervisor_usoftware == 0 &&
                          interrupts.pmachine_utimer == 0 &&
                          interrupts.psupervisor_utimer == 0 &&
                          interrupts.pmachine_uexternal == 0 &&
                          interrupts.psupervisor_uexternal == 0));
      if (scenario == 3) {
        std::uint64_t pc;
        pc = UINT64_C(0x70c);
        constant64(pc, 1, timer_compare + 10);
        drain();
        csr_access(pc, 1, 0, 1, UINT64_C(0x14d), timer_compare);
        pc += 4;
        expect_system(pc, UINT64_C(0x10200073));
        stop_at(pc, boundary, 3);
        send(pc, UINT64_C(0x10200073), 0, 1, 0, 0);
        drain();
        send(boundary, imm(13, 0, 13), 0, 1);
        drain();
      }
    }
    // Every conditional predicate uses the new value on rs1, rs2, or both,
    // including signed/unsigned disagreement and both prediction directions.
    for (int f3 = 0; f3 < 8; f3++)
      if (f3 != 2 && f3 != 3) {
        for (int dependency = 0; dependency < 3; dependency++) {
          for (int value_case = 0; value_case < 3; value_case++) {
            for (int predicted = 0; predicted < 2; predicted++) {
              std::uint64_t produced, left, right, pc;
              bool taken;
              int before_pairs;
              prediction_t prediction;
              reset_core();
              produced = value_case == 0 ? UINT64_MAX : value_case == 1 ? 1 : 0;
              send(UINT64_C(0x12800), imm(1, 0, int(produced) - 1),
                   imm(2, 0, 1));
              send(UINT64_C(0x12808), imm(3, 0, 42), 0, 1);
              drain();
              left = dependency == 1 ? 1 : produced;
              right = dependency == 0 ? 1 : produced;
              switch (f3) {
              case 0: {
                taken = left == right;
              } break;
              case 1: {
                taken = left != right;
              } break;
              case 4: {
                taken = std::int64_t(left) < std::int64_t(right);
              } break;
              case 5: {
                taken = std::int64_t(left) >= std::int64_t(right);
              } break;
              case 6: {
                taken = left < right;
              } break;
              case 7: {
                taken = left >= right;
              } break;
              }
              pc = UINT64_C(0x12810);
              prediction = {static_cast<uint8_t>(((predicted)&low_mask(1))),
                            pc + 4, pc + 36, UINT64_C(0), UINT64_C(0)};
              if (taken != bool(predicted))
                stop_at(pc + 4, taken ? pc + 36 : pc + 8);
              before_pairs = branch_pairs;
              expected_branch_taken[pc + 4] = taken;
              send(pc, imm(3, 1, 1),
                   branch(dependency == 1 ? 2 : 3, dependency == 0 ? 2 : 3, 32,
                          f3),
                   2, 1, 1, -1, prediction);
              if (taken != bool(predicted))
                send(pc + 8, imm(4, 0, 99), imm(5, 0, 99), 2, 0, 0);
              drain();
              CHECK(branch_pairs == before_pairs + 1 &&
                    !expected_branch_taken.contains(pc + 4));
              send(taken ? pc + 36 : pc + 8, imm(4, 3, 0), 0, 1);
              drain();
            }
          }
        }
      }
    // Registered ALU result selection includes word sign extension, bit
    // manipulation, and PC-based results, not simply an rs1 passthrough.
    for (int producer = 0; producer < 3; producer++) {
      std::uint32_t word;
      int before_pairs;
      reset_core();
      send(UINT64_C(0x12880), imm(1, 0, -128), imm(2, 0, 1));
      drain();
      switch (producer) {
      case 0: {
        word = imm(3, 1, 31, 1, UINT64_C(27));
      } break;
      case 1: {
        word = b_insn(29, 3, 1, 0);
      } break;
      case 2: {
        word = UINT64_C(0x197); // AUIPC x3,0
      } break;
      }
      before_pairs = branch_pairs;
      stop_at(UINT64_C(0x1288c), UINT64_C(0x128ac));
      send(UINT64_C(0x12888), word, branch(3, 3, 32));
      drain();
      CHECK(branch_pairs == before_pairs + 1);
      send(UINT64_C(0x128ac), imm(4, 3, 0), 0, 1);
      drain();
    }
    // Sixteen dependent not-taken pairs sustain dual retirement.
    {
      int before_pairs;
      reset_core();
      send(UINT64_C(0x12900), imm(2, 0, 100), 0, 1);
      drain();
      before_pairs = branch_pairs;
      dual_run = 0;
      longest_dual_run = 0;
      for (int i = 0; i < 16; i++)
        send(UINT64_C(0x12908) + ((8 * i) & low_mask(64)), imm(3, 0, i),
             branch(3, 2, 32));
      drain();
      CHECK(branch_pairs == before_pairs + 16 && longest_dual_run >= 12);
    }
    // The producer's RAW/WAW interlocks still hold both instructions_in, even
    // when both comparison operands will be replaced after the old load returns.
    for (int destination_wait = 0; destination_wait < 2; destination_wait++) {
      int before_pairs;
      reset_core();
      hold_responses = 1;
      send(UINT64_C(0x12a00), imm(5, 0, UINT64_C(0x300)), 0, 1);
      drain();
      send(UINT64_C(0x12a04),
           imm(destination_wait != 0 ? 3 : 1, 5, 0, 3, UINT64_C(3)), 0, 1);
      for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
        tick();
      before_pairs = branch_pairs;
      stop_at(UINT64_C(0x12a0c), UINT64_C(0x12a2c));
      expected_branch_taken[UINT64_C(0x12a0c)] = 1;
      send(UINT64_C(0x12a08), imm(3, destination_wait != 0 ? 0 : 1, 1),
           branch(3, 3, 32));
      for (int repeat_index = 0; repeat_index < (6); ++repeat_index) {
        tick();
        CHECK(issued == 0);
      }
      hold_responses = 0;
      drain();
      // RAW can forward at completion arbitration, whose RF return reserves
      // the younger slot; WAW waits until the write edge and then pairs.
      CHECK(branch_pairs <= before_pairs + 1 &&
            (destination_wait == 0 || branch_pairs == before_pairs + 1) &&
            !expected_branch_taken.contains(UINT64_C(0x12a0c)));
    }
    // An unrelated pending comparison operand is never waived.
    {
      int before_pairs;
      reset_core();
      hold_responses = 1;
      send(UINT64_C(0x12a40), imm(1, 0, UINT64_C(0x300)), 0, 1);
      drain();
      send(UINT64_C(0x12a44), imm(2, 1, 0, 3, UINT64_C(3)), 0, 1);
      for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
        tick();
      before_pairs = branch_pairs;
      send(UINT64_C(0x12a48), imm(3, 0, 1), branch(3, 2, 32));
      for (int repeat_index = 0; repeat_index < (6); ++repeat_index) {
        tick();
        CHECK(issued == 0);
      }
      hold_responses = 0;
      drain();
      CHECK(branch_pairs == before_pairs);
    }
    // x0 never selects a producer result, even when the older instruction names it.
    {
      int before_pairs;
      reset_core();
      send(UINT64_C(0x12ac0), imm(2, 0, 1), 0, 1);
      drain();
      before_pairs = branch_pairs;
      stop_at(UINT64_C(0x12acc), UINT64_C(0x12aec));
      expected_branch_taken[UINT64_C(0x12acc)] = 1;
      send(UINT64_C(0x12ac8), imm(0, 0, 99), branch(0, 2, 32, 1));
      drain();
      CHECK(branch_pairs == before_pairs &&
            !expected_branch_taken.contains(UINT64_C(0x12acc)));
    }
    // Deferred producers and a dependent JALR retain ordinary splitting.
    for (int producer = 0; producer < 4; producer++) {
      std::uint32_t first, second;
      int before_pairs;
      reset_core();
      lookup_mode = 1;
      send(UINT64_C(0x12b00), imm(1, 0, UINT64_C(0x300)), imm(2, 0, 2));
      drain();
      switch (producer) {
      case 0: {
        first = imm(3, 1, 0, 3, UINT64_C(3));
      } break;
      case 1: {
        first = m_insn(3, 1, 2, 0);
      } break;
      case 2: {
        first = m_insn(3, 1, 2, 4);
      } break;
      case 3: {
        first = imm(3, 0, UINT64_C(0x600));
      } break;
      }
      second =
          producer == 3 ? imm(4, 3, 0, 0, UINT64_C(0x67)) : branch(3, 3, 32);
      stop_at(UINT64_C(0x12b0c),
              producer == 3 ? UINT64_C(0x600) : UINT64_C(0x12b2c));
      before_pairs = branch_pairs;
      send(UINT64_C(0x12b08), first, second);
      drain();
      CHECK(branch_pairs == before_pairs);
      send(UINT64_C(0x12b30), imm(5, 3, 0), 0, 1);
      drain();
    }
    // Older MEM fault/replay suppresses even a dependent taken branch's
    // redirect_out and training; younger rejection preserves the producer once.
    for (int rejected_lane = 0; rejected_lane < 2; rejected_lane++) {
      for (int replay = 0; replay < 2; replay++) {
        int before_pairs, before_training, before_commits;
        reset_core();
        inject_enable = 1;
        inject_pc = UINT64_C(0x12c00) + ((4 * rejected_lane) & low_mask(64));
        inject_result = {.pguest = {},
                         .pdisposition = static_cast<uint8_t>(
                             replay != 0 ? UINT64_C(2) : UINT64_C(1)),
                         .pcause = UINT64_C(5),
                         .pvalue = UINT64_C(0xdead)};
        stop_at(inject_pc, inject_pc, replay != 0 ? 2 : 1, 5, UINT64_C(0xdead));
        before_pairs = branch_pairs;
        before_training = branch_updates;
        before_commits = commits;
        send(UINT64_C(0x12c00), imm(3, 0, 1), branch(3, 3, 32), 2,
             rejected_lane == 1, 0);
        drain();
        CHECK(branch_pairs == before_pairs + 1 &&
              branch_updates == before_training &&
              commits == before_commits + rejected_lane);
        inject_enable = 0;
        send(UINT64_C(0x12c08), imm(4, 3, 0), 0, 1);
        drain();
      }
    }
    // Coordinated reset cancels a pair captured into EX before MEM comparison.
    reset_core();
    send(UINT64_C(0x12c40), imm(3, 0, 1), branch(3, 3, 32), 2, 0, 0);
    tick();
    reset_core();
    drain();
    send(UINT64_C(0x12c48), imm(4, 3, 0), 0, 1);
    drain();
    // Both comparators resolve independently, including two correctly taken branches.
    for (int outcomes = 0; outcomes < 4; outcomes++) {
      int before_dual, before_training;
      std::uint64_t pc;
      prediction_t first_prediction, second_prediction;
      reset_core();
      pc = UINT64_C(0x13000) + ((outcomes * 8) & low_mask(64));
      first_prediction = {static_cast<uint8_t>(((outcomes & 1) & low_mask(1))),
                          pc, pc + 4, UINT64_C(0), UINT64_C(0)};
      second_prediction = {
          static_cast<uint8_t>((((outcomes >> 1) & 1) & low_mask(1))), pc + 4,
          pc + 8, UINT64_C(0), UINT64_C(0)};
      expected_branch_taken[pc] = ((outcomes & 1) & low_mask(1));
      expected_branch_taken[pc + 4] = (((outcomes >> 1) & 1) & low_mask(1));
      before_dual = dual_branches;
      before_training = branch_updates;
      send(pc, branch(0, 0, 4, sv_slice(outcomes, 0, 1) ? 0 : 1),
           branch(0, 0, 4, sv_slice(outcomes, 1, 1) ? 0 : 1), 2, 1, 1, -1,
           second_prediction, first_prediction);
      drain();
      CHECK(dual_branches == before_dual + 1 &&
            branch_updates == before_training + 2 &&
            expected_branch_taken.size() == 0);
    }
    // A sustained two-branch stream must backpressure RR, never lose a table write.
    {
      int before_dual, before_training;
      reset_core();
      before_dual = dual_branches;
      before_training = branch_updates;
      for (int pair = 0; pair < 64; pair++)
        send(UINT64_C(0x14000) + ((pair * 8) & low_mask(64)),
             branch(0, 0, 32, 1), branch(0, 0, 32, 1));
      drain();
      CHECK(branch_updates == before_training + 128 &&
            dual_branches >= before_dual + 2 && max_training_pending == 3);
    }
    // Oldest correction wins; a younger correction retains both retirement updates.
    for (int correcting_lane = 0; correcting_lane < 2; correcting_lane++) {
      int before_training;
      reset_core();
      before_training = branch_updates;
      stop_at(UINT64_C(0x15000) + ((correcting_lane * 4) & low_mask(64)),
              UINT64_C(0x15020) + ((correcting_lane * 4) & low_mask(64)));
      send(UINT64_C(0x15000), branch(0, 0, 32, correcting_lane == 0 ? 0 : 1),
           branch(0, 0, 32, correcting_lane == 1 ? 0 : 1), 2, 1,
           correcting_lane == 1);
      drain();
      CHECK(branch_updates == before_training + 1 + correcting_lane);
    }
    // Fault/replay in either branch qualifies only its successful older prefix.
    for (int rejected_lane = 0; rejected_lane < 2; rejected_lane++)
      for (int replay = 0; replay < 2; replay++) {
        int before_training, before_commits;
        reset_core();
        before_training = branch_updates;
        before_commits = commits;
        inject_enable = 1;
        inject_pc = UINT64_C(0x15100) + ((rejected_lane * 4) & low_mask(64));
        inject_result = {.pguest = {},
                         .pdisposition = static_cast<uint8_t>(
                             replay != 0 ? UINT64_C(2) : UINT64_C(1)),
                         .pcause = UINT64_C(5),
                         .pvalue = UINT64_C(0xdead)};
        stop_at(inject_pc, inject_pc, replay != 0 ? 2 : 1, 5, UINT64_C(0xdead));
        send(UINT64_C(0x15100), branch(0, 0, 32, 1), branch(0, 0, 32, 1), 2,
             rejected_lane == 1, 0);
        drain();
        // Trap entry clears table training, including the simultaneous successful prefix.
        CHECK(branch_updates ==
                  before_training + (replay != 0 ? rejected_lane : 0) &&
              commits == before_commits + rejected_lane);
      }
    // Two calls split, but a conditional plus one call may retire together.
    {
      int before_dual, before_ras;
      prediction_t first_prediction, second_prediction;
      reset_core();
      before_dual = dual_branches;
      before_ras = ras_resolutions;
      first_prediction = {UINT64_C(1), UINT64_C(0x15200), UINT64_C(0x15204),
                          UINT64_C(0), UINT64_C(1)};
      second_prediction = {UINT64_C(1), UINT64_C(0x15204), UINT64_C(0x15208),
                           UINT64_C(0), UINT64_C(1)};
      send(UINT64_C(0x15200), jump(1, 4), jump(5, 4), 2, 1, 1, -1,
           second_prediction, first_prediction);
      drain();
      CHECK(dual_branches == before_dual && ras_resolutions == before_ras + 2);
      reset_core();
      before_dual = dual_branches;
      before_ras = ras_resolutions;
      second_prediction = {UINT64_C(1), UINT64_C(0x15244), UINT64_C(0x15248),
                           UINT64_C(0), UINT64_C(1)};
      send(UINT64_C(0x15240), branch(0, 0, 32, 1), jump(1, 4), 2, 1, 1, -1,
           second_prediction);
      drain();
      CHECK(dual_branches == before_dual + 1 &&
            ras_resolutions == before_ras + 1);
    }
    // Ssnpm masks data addresses after generation; neither GPR values nor code PCs are modified.
    for (int pmm = 2; pmm <= 3; pmm++) {
      std::uint64_t pc, tag;
      pc = UINT64_C(0x16000);
      tag = pmm == 2 ? UINT64_C(0xfe00000000000300)
                     : UINT64_C(0xabcd000000000300);
      reset_core();
      constant64(pc, 1, ((pmm)&low_mask(64)) << 32);
      drain();
      csr_access(pc, 1, 0, 1, UINT64_C(0x10a), 0);
      pc += 4;
      constant64(pc, 1, tag);
      drain();
      constant64(pc, 2, UINT64_C(0x20000));
      drain();
      csr_access(pc, 1, 0, 2, UINT64_C(0x300), UINT64_C(0xa00000000));
      pc += 4; // MPRV U
      expect_system(pc, imm(3, 1, 0, 3, UINT64_C(3)),
                    UINT64_C(0x9f9e9d9c9b9a9998));
      expect_memory(UINT64_C(0x300), 0, 8, 0);
      send(pc, imm(3, 1, 0, 3, UINT64_C(3)), 0, 1, 0, 0);
      drain();
      pc += 4;
      send(pc, imm(4, 1, 0), 0, 1);
      drain(); // Still holds the complete tagged pointer.
    }
    // A dual-retirement count advances the one implemented HPM counter by two.
    {
      std::uint64_t pc;
      pc = UINT64_C(0x17000);
      reset_core();
      csr_access(pc, 5, 0, 8, UINT64_C(0x320), 0);
      pc += 4;
      csr_access(pc, 5, 0, 2, UINT64_C(0x323), 0);
      pc += 4; // instret selector
      csr_access(pc, 5, 0, 0, UINT64_C(0xb03), 0);
      pc += 4;
      csr_access(pc, 5, 0, 0, UINT64_C(0x320), 8);
      pc += 4;
      send(pc, imm(3, 0, 1), imm(4, 0, 2));
      pc += 8;
      drain();
      csr_access(pc, 5, 0, 8, UINT64_C(0x320), 0);
      pc += 4;
      csr_access(pc, 2, 5, 0, UINT64_C(0xb03),
                 3); // pair plus the inhibiting CSR's retiring edge
    }
    // Overflow is based on the carry from the full two-instruction retirement count.
    {
      std::uint64_t pc;
      pc = UINT64_C(0x17800);
      reset_core();
      csr_access(pc, 5, 0, 8, UINT64_C(0x320), 0);
      pc += 4;
      csr_access(pc, 5, 0, 2, UINT64_C(0x323), 0);
      pc += 4;
      constant64(pc, 1, UINT64_C(0xffffffffffffffff));
      drain();
      csr_access(pc, 1, 0, 1, UINT64_C(0xb03), 0);
      pc += 4;
      csr_access(pc, 5, 0, 0, UINT64_C(0x320), 8);
      pc += 4;
      send(pc, imm(3, 0, 1), imm(4, 0, 2));
      pc += 8;
      drain();
      csr_access(pc, 5, 0, 8, UINT64_C(0x320), 0);
      pc += 4;
      csr_access(pc, 2, 5, 0, UINT64_C(0xb03), 2);
      pc += 4;
      csr_access(pc, 2, 6, 0, UINT64_C(0x344), 8192);
      pc += 4;
      csr_access(pc, 2, 7, 0, UINT64_C(0x323), UINT64_C(0x8000000000000002));
      pc += 4;
      constant64(pc, 1, UINT64_C(0x4000000000000002));
      drain();
      csr_access(pc, 1, 0, 1, UINT64_C(0x323), UINT64_C(0x8000000000000002));
      pc += 4;
      csr_access(pc, 5, 0, 0, UINT64_C(0xb03), 2);
      pc += 4;
      csr_access(pc, 5, 0, 0, UINT64_C(0x320), 8);
      pc += 4;
      send(pc, imm(3, 0, 3), imm(4, 0, 4));
      pc += 8;
      drain();
      csr_access(pc, 2, 5, 0, UINT64_C(0xb03), 0);
    }
    // State-enable gates lower-mode access to senvcfg, independently of its PMM bits.
    for (int enabled = 0; enabled < 2; enabled++) {
      std::uint64_t pc;
      pc = UINT64_C(0x18000);
      reset_core();
      if (enabled != 0) {
        constant64(pc, 1, UINT64_C(0x4000000000000000));
        drain();
        csr_access(pc, 1, 0, 1, UINT64_C(0x30c), 0);
        pc += 4;
      }
      constant64(pc, 1, UINT64_C(0x800));
      drain();
      csr_access(pc, 1, 0, 1, UINT64_C(0x300), UINT64_C(0xa00000000));
      pc += 4;
      send(pc, imm(2, 0, UINT64_C(0x500)), 0, 1);
      pc += 4;
      drain();
      csr_access(pc, 1, 0, 2, UINT64_C(0x341), 0);
      pc += 4;
      expect_system(pc, UINT64_C(0x30200073));
      stop_at(pc, UINT64_C(0x500), 3);
      send(pc, UINT64_C(0x30200073), 0, 1, 0, 0);
      drain();
      if (enabled != 0)
        csr_access(UINT64_C(0x500), 2, 3, 0, UINT64_C(0x10a), 0);
      else {
        stop_at(UINT64_C(0x500), 0, 1, 2,
                ((csr(2, 3, 0, UINT64_C(0x10a))) & low_mask(64)));
        send(UINT64_C(0x500), csr(2, 3, 0, UINT64_C(0x10a)), 0, 1, 0, 0);
        drain();
      }
    }
    // HLV.D has zero decoded displacement despite nonzero sv_slice(instruction,20,(31)-(20)+1).
    // It uses the same address route as ordinary loads, without a dependent adder.
    {
      std::uint64_t pc;
      std::uint32_t hlv;
      int before_pairs;
      pc = UINT64_C(0x19000);
      hlv = UINT64_C(0x6c00c1f3); // hlv.d x3,(x1)
      reset_core();
      before_pairs = guest_address_pairs;
      expect_instruction(pc, imm(1, 0, UINT64_C(0x300)));
      expect_system(pc + 4, hlv, UINT64_C(0x9f9e9d9c9b9a9998));
      expect_memory(UINT64_C(0x300), 0, 8, 0);
      send(pc, imm(1, 0, UINT64_C(0x300)), hlv, 2, 0, 0);
      drain();
      pc += 8;
      CHECK(guest_address_pairs == before_pairs + 1);
      send(pc, imm(4, 3, 1), 0, 1);
      drain();
      // Full invalidation fences serialize and invalidate once at successful WB.
      for (int i = 0; i < 4; i++) {
        std::uint32_t word;
        int before_flush;
        word = i == 0   ? UINT64_C(0x22000073)
               : i == 1 ? UINT64_C(0x62000073)
               : i == 2 ? UINT64_C(0x26000073)
                        : UINT64_C(0x66000073);
        before_flush = translation_invalidations;
        pc += 4;
        expect_system(pc, word);
        stop_at(pc, pc + 4, 3);
        send(pc, word, 0, 1, 0, 0);
        drain();
        CHECK(translation_invalidations == before_flush + 1);
      }
    }
    // Enter VS using MPV; an explicit guest load traps as virtual instruction,
    // with the older successful slot preserved and no accepted memory request.
    for (int lane = 0; lane < 2; lane++) {
      std::uint64_t pc;
      std::uint32_t hlv;
      int before_commits, before_requests;
      pc = UINT64_C(0x1a000);
      hlv = UINT64_C(0x6c00c1f3);
      reset_core();
      constant64(pc, 1, UINT64_C(0x8000000800));
      drain();
      csr_access(pc, 1, 0, 1, UINT64_C(0x300), UINT64_C(0xa00000000));
      pc += 4;
      send(pc, imm(2, 0, UINT64_C(0x500)), 0, 1);
      pc += 4;
      drain();
      csr_access(pc, 1, 0, 2, UINT64_C(0x341), 0);
      pc += 4;
      expect_system(pc, UINT64_C(0x30200073));
      stop_at(pc, UINT64_C(0x500), 3);
      send(pc, UINT64_C(0x30200073), 0, 1, 0, 0);
      drain();
      before_commits = commits;
      before_requests = requests;
      stop_at(UINT64_C(0x500) + ((lane * 4) & low_mask(64)), 0, 1, 22,
              ((hlv)&low_mask(64)));
      if (lane == 0)
        send(UINT64_C(0x500), hlv, imm(5, 0, 7), 2, 0, 0);
      else
        send(UINT64_C(0x500), imm(5, 0, 7), hlv, 2, 1, 0);
      drain();
      CHECK(commits == before_commits + lane && requests == before_requests);
      csr_access(0, 2, 6, 0, UINT64_C(0x342), 22);
    }
    // Either fault slot retains implicit-PTE provenance into the architectural trap CSRs.
    for (int lane = 0; lane < 2; lane++) {
      reset_core();
      inject_enable = 1;
      inject_pc = UINT64_C(0x1b000) + ((lane * 4) & low_mask(64));
      inject_result = {.pdisposition = UINT64_C(1),
                       .pcause = UINT64_C(21),
                       .pvalue = UINT64_C(0x500008),
                       .pguest = {1, 131072, 1}};
      stop_at(inject_pc, 0, 1, 21, UINT64_C(0x500008));
      if (lane == 0)
        send(UINT64_C(0x1b000), imm(5, 0, 7), imm(6, 0, 8), 2, 0, 0);
      else
        send(UINT64_C(0x1b000), imm(5, 0, 7), imm(6, 0, 8), 2, 1, 0);
      drain();
      inject_enable = 0;
      csr_access(0, 2, 7, 0, UINT64_C(0x34b), UINT64_C(0x8000));
      csr_access(4, 2, 8, 0, UINT64_C(0x34a), UINT64_C(0x3000));
      csr_access(8, 2, 9, 0, UINT64_C(0x343), UINT64_C(0x500008));
    }

    CHECK(branch_updates > 0);
    CHECK(direction_updates > 0 && history_recoveries > 0);
  });
}

void drive() {
  {
    split_in.prequest.pready = !split_active;
    split_in.presponse.pbits = split_reply;
    memory_in.pfault.pvalid =
        memory_out.prequest.pvalid && inject_memory_fault &&
        memory_out.prequest.pbits.paddress == fault_address;
    memory_in.pfault.pbits = {.pguest = {},
                              .pdisposition = UINT64_C(1),
                              .pcause =
                                  (memory_out.prequest.pbits.paccess == 2 ||
                                   memory_out.prequest.pbits.paccess == 4 ||
                                   memory_out.prequest.pbits.paccess == 5 ||
                                   memory_out.prequest.pbits.paccess == 6 ||
                                   memory_out.prequest.pbits.paccess == 7 ||
                                   memory_out.prequest.pbits.paccess == 8 ||
                                   memory_out.prequest.pbits.paccess == 9)
                                      ? UINT64_C(7)
                                      : UINT64_C(5),
                              .pvalue = fault_address};
    memory_in.prequest.pready =
        !block_requests && response_count < 16 && !memory_in.pfault.pvalid;
    memory_in.pdrained = response_count == 0 && !memory_out.prequest.pvalid;
    pipeline_in.presponse = lookup_response;
    pipeline_in.pcommit_uready = store_candidate_valid && !block_stores;
    for (int lane = 0; lane < 2; lane++) {
      resolution(lane) = {};
      if (inject_enable && memory_stage(lane).pvalid &&
          memory_stage(lane).pbits.ppc == inject_pc) {
        resolution(lane).pvalid = 1;
        resolution(lane).pbits = inject_result;
      }
    }
  }
  memory_in.pordered_ubusy = 0;
  memory_in.preservation_uvalid = reservation_valid;
  memory_in.presponse.pbits = response_data[response_read];
}

void observe() {
  if (!reset) {
    int ras_count;
    ras_count = 0;
    for (int lane = 0; lane < 2; lane++) {
      bool conditional, stack_event;
      conditional = retired(lane).pvalid &&
                    sv_slice(retired(lane).pbits.pfetched.pinstruction, 0,
                             (6) - (0) + 1) == UINT64_C(0x63);
      CHECK((history_commit_out.pvalid &&
             history_commit_out.pbits.penable[lane]) == conditional);
      if (conditional &&
          expected_branch_taken.contains(retired(lane).pbits.pfetched.ppc))
        CHECK(history_commit_out.pbits.ptaken[lane] ==
              expected_branch_taken[retired(lane).pbits.pfetched.ppc]);
      if (retired(lane).pvalid &&
          branch_encoding(retired(lane).pbits.pfetched.pinstruction))
        training_pending.push_back(retired(lane).pbits.pfetched.ppc);
      stack_event =
          retired(lane).pvalid &&
          branch_encoding(retired(lane).pbits.pfetched.pinstruction) &&
          (ras_action(retired(lane).pbits.pfetched.pinstruction) != 0 ||
           retired(lane).pbits.pfetched.pspeculated_uras_uaction != 0);
      if (stack_event) {
        ras_count++;
        CHECK(ras_resolution_out.pvalid &&
              ras_resolution_out.pbits.pactual.paction ==
                  ras_action(retired(lane).pbits.pfetched.pinstruction) &&
              ras_resolution_out.pbits.pactual.preturn_uaddress ==
                  retired(lane).pbits.pfetched.psequential_upc &&
              ras_resolution_out.pbits.ppredicted_uaction ==
                  retired(lane).pbits.pfetched.pspeculated_uras_uaction);
      }
    }
    CHECK(ras_count <= 1 && ras_resolution_out.pvalid == (ras_count != 0));
    ras_resolutions += ras_count;
    if (retired(0).pvalid && retired(1).pvalid &&
        branch_encoding(retired(0).pbits.pfetched.pinstruction) &&
        branch_encoding(retired(1).pbits.pfetched.pinstruction))
      dual_branches++;
    if (predictor_clear_out.pvalid)
      training_pending.clear();
    if (training_pending.size() > max_training_pending)
      max_training_pending = training_pending.size();
    if (branch_update_out.pvalid) {
      std::uint64_t expected_pc;
      CHECK(training_pending.size() > 0);
      expected_pc = training_pending.take();
      CHECK(branch_update_out.pbits.ppc == expected_pc);
      branch_updates++;
      if (expected_branch_taken.contains(branch_update_out.pbits.ppc)) {
        CHECK(branch_update_out.pbits.ptaken ==
              expected_branch_taken[branch_update_out.pbits.ppc]);
        expected_branch_taken.erase(branch_update_out.pbits.ppc);
      }
      CHECK(branch_update_out.pbits.pbranch);
      CHECK(direction_update_out.pvalid ==
            branch_update_out.pbits.pconditional);
      if (direction_update_out.pvalid) {
        direction_updates++;
        CHECK(direction_update_out.pbits.pindex ==
                  offered_direction[branch_update_out.pbits.ppc].pindex &&
              direction_update_out.pbits.ptaken ==
                  branch_update_out.pbits.ptaken);
        if (corrected_history.contains(branch_update_out.pbits.ppc)) {
          CHECK(corrected_history[branch_update_out.pbits.ppc] ==
                (field(sv_slice(offered_direction[branch_update_out.pbits.ppc]
                                    .phistory,
                                0, (8) - (0) + 1),
                       9, 1) |
                 field(branch_update_out.pbits.ptaken, 1, 0)));
          corrected_history.erase(branch_update_out.pbits.ppc);
        }
      }
    }
    CHECK(training_pending.size() <= 2);
  }
  if (!reset && redirect_out.pvalid &&
      (redirect_out.pbits.presolution.pdisposition == 0 ||
       redirect_out.pbits.presolution.pdisposition == 2)) {
    direction_t checkpoint;
    std::uint16_t want;
    checkpoint = offered_direction[redirect_out.pbits.ppc];
    want = checkpoint.phistory;
    CHECK(history_restore_out.pvalid);
    if (redirect_out.pbits.presolution.pdisposition == 0 && checkpoint.pvalid)
      corrected_history[redirect_out.pbits.ppc] = history_restore_out.pbits;
    else
      CHECK(history_restore_out.pbits == want);
    history_recoveries++;
  }
  if (!reset && translation_flush)
    translation_invalidations++;
  if (!reset && instruction_invalidate_out.pvalid) {
    invalidations++;
    CHECK(retired(0).pvalid &&
          retired(0).pbits.pfetched.pinstruction == UINT64_C(0x100f) &&
          redirect_out.pvalid &&
          redirect_out.pbits.presolution.pdisposition == 3);
  }
  {
    if (reset) {
      defer(split_active, 0);
      defer(response_count, 0);
      defer(response_read, 0);
      defer(response_write, 0);
      defer(lookup_response, std::remove_cvref_t<decltype(lookup_response)>{});
      defer(store_candidate_valid, 0);
    } else {
      bool push_response, pop_response;
      if (split_out.prequest.pvalid && split_in.prequest.pready) {
        std::uint64_t data;
        int bytes_count;
        CHECK(response_count == 0 && expected_completions.size() == 0);
        CHECK((split_out.prequest.pbits.paccess == 1 ||
               split_out.prequest.pbits.paccess == 2));
        CHECK(split_out.prequest.pbits.plocality == expected_split_locality);
        bytes_count = 1 << split_out.prequest.pbits.pwidth;
        data = 0;
        for (int b = 0; b < bytes_count; b++) {
          if (split_out.prequest.pbits.paccess == 2 && !split_fault)
            memory_bytes[int(split_out.prequest.pbits.paddress) + b] =
                sv_slice(split_out.prequest.pbits.pdata, b * 8, 8);
          sv_slice(data, b * 8, 8) =
              memory_bytes[int(split_out.prequest.pbits.paddress) + b];
        }
        if (!split_out.prequest.pbits.punsigned && bytes_count < 8 &&
            sv_slice(data, bytes_count * 8 - 1, 1))
          data |= UINT64_MAX << (bytes_count * 8);
        defer(split_reply,
              std::remove_cvref_t<decltype(split_reply)>{
                  .presponse = {.paccess_ufault = UINT64_C(0),
                                .pdata = data,
                                .pcontext = UINT64_C(0)},
                  .ppage_ufault = split_fault,
                  .pfault_uaddress = split_out.prequest.pbits.paddress + 3,
                  .pguest = {}});
        defer(split_active, 1);
        defer(split_due, cycles + 12);
        split_requests++;
      } else if (split_in.presponse.pvalid)
        defer(split_active, 0);
      push_response = memory_out.prequest.pvalid && memory_in.prequest.pready;
      pop_response = memory_in.presponse.pvalid && memory_out.presponse.pready;
      defer(response_count,
            response_count + int(push_response) - int(pop_response));
      if (response_count > max_outstanding)
        max_outstanding = response_count;
      defer(lookup_response.pvalid, pipeline_out.prequest.pvalid);
      if (pipeline_out.prequest.pvalid)
        lookups++;
      defer(lookup_request, pipeline_out.prequest.pbits);
      defer(lookup_response.pbits.poutcome,
            lookup_mode == 1
                ? (pipeline_out.prequest.pbits.paccess == 2 ? UINT64_C(2)
                                                            : UINT64_C(1))
                : ((lookup_mode)&low_mask(3)));
      for (int b = 0; b < 8; b++)
        defer(sv_slice(lookup_response.pbits.pdata, b * 8, 8),
              memory_bytes[int(sv_slice(pipeline_out.prequest.pbits.paddress, 3,
                                        (11) - (3) + 1)) *
                               8 +
                           b]);
      defer(store_candidate, lookup_request);
      defer(store_candidate_valid,
            lookup_response.pvalid && lookup_response.pbits.poutcome == 2);
      if (memory_out.prequest.pvalid && !memory_in.prequest.pready) {
        stall_cycles++;
      }
      if (push_response) {
        std::uint64_t beat;
        check_request(memory_out.prequest.pbits);
        for (int b = 0; b < 8; b++)
          sv_slice(beat, b * 8, 8) =
              memory_bytes[int(sv_slice(memory_out.prequest.pbits.paddress, 3,
                                        (11) - (3) + 1)) *
                               8 +
                           b];
        if (memory_out.prequest.pbits.paccess == 3 &&
            memory_out.prequest.pbits.pwidth == 2) {
          beat = beat >> (8 * int(sv_slice(memory_out.prequest.pbits.paddress,
                                           0, (2) - (0) + 1)));
          beat = sign_extend(beat, 32);
        }
        defer(response_data[response_write], beat);
        defer(response_management[response_write],
              (memory_out.prequest.pbits.paccess == 7 ||
               memory_out.prequest.pbits.paccess == 8 ||
               memory_out.prequest.pbits.paccess == 9));
        defer(response_due[response_write], cycles + configured_delay);
        defer(response_write, (response_write + 1) % 16);
        requests++;
      }
      if (pipeline_out.pcommit && pipeline_in.pcommit_uready) {
        CHECK(store_candidate_valid);
        check_request(store_candidate);
        hits++;
      }
      if (pop_response) {
        defer(response_read, (response_read + 1) % 16);
        responses++;
      }
    }
  }
  {
    if (!reset) {
      cycles++;
      {
        bool p0, p1;
        std::uint64_t pc;
        p0 = retired(0).pvalid &&
             prefetch_encoding(retired(0).pbits.pfetched.pinstruction);
        p1 = retired(1).pvalid &&
             prefetch_encoding(retired(1).pbits.pfetched.pinstruction);
        CHECK(prefetch_out.pvalid == (p0 || p1));
        if (p0 || p1) {
          pc = p0 ? retired(0).pbits.pfetched.ppc
                  : retired(1).pbits.pfetched.ppc;
          CHECK(
              (prefetch_out.pbits.paddress == expected_prefetch[pc].paddress &&
               prefetch_out.pbits.poperation ==
                   expected_prefetch[pc].poperation));
          CHECK(!(p0 && retired(0).pbits.pdeferred) &&
                !(p1 && retired(1).pbits.pdeferred));
          prefetch_count++;
          if (p0 && p1)
            prefetch_pairs++;
        }
      }
      for (int lane = 0; lane < 2; lane++)
        if (memory_stage(lane).pvalid) {
          if (memory_stage(lane).pbits.ppc == UINT64_C(0xab08))
            multiply_mem_cycle = cycles;
          if (memory_stage(lane).pbits.ppc == UINT64_C(0xab10))
            dependent_mem_cycle = cycles;
        }
      if (memory_stage(0).pvalid && memory_stage(1).pvalid &&
          sv_slice(memory_stage(0).pbits.pinstruction, 0, (6) - (0) + 1) ==
              UINT64_C(23) &&
          sv_slice(memory_stage(0).pbits.pinstruction, 7, (11) - (7) + 1) !=
              0 &&
          sv_slice(memory_stage(0).pbits.pinstruction, 7, (11) - (7) + 1) ==
              sv_slice(memory_stage(1).pbits.pinstruction, 15,
                       (19) - (15) + 1) &&
          sv_slice(memory_stage(1).pbits.pinstruction, 0, (6) - (0) + 1) ==
              UINT64_C(19) &&
          sv_slice(memory_stage(1).pbits.pinstruction, 12, (14) - (12) + 1) ==
              0)
        auipc_addi_pairs++;
      if (memory_stage(0).pvalid && memory_stage(1).pvalid &&
          sv_slice(memory_stage(0).pbits.pinstruction, 7, (11) - (7) + 1) !=
              0 &&
          sv_slice(memory_stage(0).pbits.pinstruction, 7, (11) - (7) + 1) ==
              sv_slice(memory_stage(1).pbits.pinstruction, 15,
                       (19) - (15) + 1) &&
          (one_of(memory_stage(0).pbits.pinstruction & 127,
                  {19, 27, 51, 59, 55, 23, 3, 47, 111, 103}) ||
           mop_encoding(memory_stage(0).pbits.pinstruction)) &&
          sv_slice(memory_stage(1).pbits.pinstruction, 0, (6) - (0) + 1) ==
              UINT64_C(3)) {
        CHECK(sv_slice(memory_stage(1).pbits.pinstruction, 20,
                       (31) - (20) + 1) == 0 &&
              !(one_of(memory_stage(0).pbits.pinstruction & 127,
                       {3, 47, 111, 103})) &&
              !((one_of(memory_stage(0).pbits.pinstruction & 127, {51, 59})) &&
                sv_slice(memory_stage(0).pbits.pinstruction, 25,
                         (31) - (25) + 1) == 1));
        address_pairs++;
      }
      if (memory_stage(0).pvalid && memory_stage(1).pvalid &&
          memory_stage(1).pbits.pinstruction == UINT64_C(0x6c00c1f3) &&
          memory_stage(0).pbits.pinstruction == imm(1, 0, UINT64_C(0x300)))
        guest_address_pairs++;
      if (memory_stage(0).pvalid && memory_stage(1).pvalid &&
          sv_slice(memory_stage(0).pbits.pinstruction, 7, (11) - (7) + 1) !=
              0 &&
          sv_slice(memory_stage(0).pbits.pinstruction, 7, (11) - (7) + 1) ==
              sv_slice(memory_stage(1).pbits.pinstruction, 20,
                       (24) - (20) + 1) &&
          (one_of(memory_stage(0).pbits.pinstruction & 127,
                  {19, 27, 51, 59, 55, 23, 3, 111, 103}) ||
           mop_encoding(memory_stage(0).pbits.pinstruction)) &&
          sv_slice(memory_stage(1).pbits.pinstruction, 0, (6) - (0) + 1) ==
              UINT64_C(0x23)) {
        CHECK(sv_slice(memory_stage(0).pbits.pinstruction, 7, (11) - (7) + 1) !=
                  sv_slice(memory_stage(1).pbits.pinstruction, 15,
                           (19) - (15) + 1) &&
              !(one_of(memory_stage(0).pbits.pinstruction & 127,
                       {3, 111, 103})) &&
              !((one_of(memory_stage(0).pbits.pinstruction & 127, {51, 59})) &&
                sv_slice(memory_stage(0).pbits.pinstruction, 25,
                         (31) - (25) + 1) == 1));
        store_data_pairs++;
      }
      if (memory_stage(0).pvalid && memory_stage(1).pvalid &&
          sv_slice(memory_stage(0).pbits.pinstruction, 7, (11) - (7) + 1) !=
              0 &&
          (one_of(memory_stage(0).pbits.pinstruction & 127,
                  {19, 27, 51, 59, 55, 23, 3, 111, 103}) ||
           mop_encoding(memory_stage(0).pbits.pinstruction)) &&
          sv_slice(memory_stage(1).pbits.pinstruction, 0, (6) - (0) + 1) ==
              UINT64_C(0x63) &&
          (sv_slice(memory_stage(0).pbits.pinstruction, 7, (11) - (7) + 1) ==
               sv_slice(memory_stage(1).pbits.pinstruction, 15,
                        (19) - (15) + 1) ||
           sv_slice(memory_stage(0).pbits.pinstruction, 7, (11) - (7) + 1) ==
               sv_slice(memory_stage(1).pbits.pinstruction, 20,
                        (24) - (20) + 1))) {
        CHECK(!(one_of(memory_stage(0).pbits.pinstruction & 127,
                       {3, 111, 103})) &&
              !((one_of(memory_stage(0).pbits.pinstruction & 127, {51, 59})) &&
                sv_slice(memory_stage(0).pbits.pinstruction, 25,
                         (31) - (25) + 1) == 1));
        branch_pairs++;
      }
      if (memory_stage(0).pvalid && memory_stage(1).pvalid &&
          sv_slice(memory_stage(0).pbits.pinstruction, 7, (11) - (7) + 1) !=
              0 &&
          one_of(memory_stage(0).pbits.pinstruction & 127,
                 {19, 27, 51, 59, 55, 23}) &&
          sv_slice(memory_stage(1).pbits.pinstruction, 0, (6) - (0) + 1) ==
              UINT64_C(0x67))
        CHECK(
            sv_slice(memory_stage(0).pbits.pinstruction, 7, (11) - (7) + 1) !=
            sv_slice(memory_stage(1).pbits.pinstruction, 15, (19) - (15) + 1));
      if (cycles > 80000)
        fail(1,
             "watchdog: commits=%0d pending=%0d next_pc=%h "
             "conditional_pairs=%0d mop_pairs=%0d split=%0d/%b offer=%b "
             "response=%b loads=%0d requests=%0d completions=%0d",
             commits, expected.size(),
             expected.size() != 0 ? expected[0].pfetched.ppc : 0,
             conditional_dual, mop_dual, split_requests, split_active,
             split_out.prequest.pvalid, split_in.presponse.pvalid,
             response_count, expected_requests.size(),
             expected_completions.size());
      CHECK(issued <= 2 && retired_count <= 2);
      if (int(instruction_capacity) < minimum_instruction_capacity)
        minimum_instruction_capacity = int(instruction_capacity);
      CHECK(!retired(1).pvalid || retired(0).pvalid);
      if (memory_stage(0).pvalid &&
          serializing_encoding(memory_stage(0).pbits.pinstruction))
        CHECK(!memory_stage(1).pvalid);
      if (memory_stage(1).pvalid)
        CHECK(!serializing_encoding(memory_stage(1).pbits.pinstruction));
      if (retired(0).pvalid && retired(1).pvalid && retired(0).pbits.pwrite &&
          retired(1).pbits.pwrite &&
          retired(0).pbits.prd == retired(1).pbits.prd) {
        CHECK(!retired(0).pbits.pdeferred);
        if (retired(1).pbits.pdeferred)
          waw_deferred++;
        else
          waw_dual++;
      }
      CHECK(int(retired_count) ==
            int(retired(0).pvalid) + int(retired(1).pvalid));
      if (issued == 1)
        single_issues++;
      if (response_count > 0 && retired_count != 0)
        overlap_retirements++;
      if (completed_out.pvalid && completed_out.pbits.pwrite) {
        CHECK(!retired(1).pvalid);
        if (retired(0).pvalid && retired(0).pbits.pwrite &&
            !retired(0).pbits.pdeferred)
          shared_writes++;
      }
      if (retired_count == 2) {
        if (conditional_encoding(retired(0).pbits.pfetched.pinstruction) &&
            conditional_encoding(retired(1).pbits.pfetched.pinstruction))
          conditional_dual++;
        if (mop_encoding(retired(0).pbits.pfetched.pinstruction) &&
            mop_encoding(retired(1).pbits.pfetched.pinstruction))
          mop_dual++;
        dual_commits++;
        dual_run++;
        if (dual_run > longest_dual_run)
          longest_dual_run = dual_run;
      } else
        dual_run = 0;
      if (retired(0).pvalid && retired(1).pvalid &&
          retired(0).pbits.pfetched.ppc == UINT64_C(0x204) &&
          retired(1).pbits.pfetched.ppc == UINT64_C(0x208))
        saw_repacked = 1;
      for (int lane = 0; lane < 2; lane++) {
        if (retired(lane).pvalid) {
          retirement_t want;
          CHECK(expected.size() > 0);
          want = expected.take();
          if (want.pfetched.pinstruction == UINT64_C(0x100000f)) {
            CHECK(!retired(lane).pbits.pwrite &&
                  !retired(lane).pbits.pdeferred && !sleeping);
            pause_commits++;
            pause_cycle = cycles;
            if (lane == 1)
              pause_dual++;
          }
          if (sv_slice(want.pfetched.pinstruction, 0, (6) - (0) + 1) ==
                  UINT64_C(15) &&
              sv_slice(want.pfetched.pinstruction, 12, (14) - (12) + 1) == 2 &&
              sv_slice(want.pfetched.pinstruction, 20, (31) - (20) + 1) < 3) {
            CHECK(!retired(lane).pbits.pdeferred &&
                  memory_in.presponse.pvalid && memory_out.presponse.pready &&
                  response_management[response_read]);
          }
          CHECK(same_instruction(retired(lane).pbits.pfetched, want.pfetched) &&
                retired(lane).pbits.pwrite == want.pwrite);
          if (retired(lane).pbits.pdeferred) {
            expected_completions.push_back(want);
            if (((sv_slice(want.pfetched.pinstruction, 0, (6) - (0) + 1) ==
                      UINT64_C(0x33) ||
                  sv_slice(want.pfetched.pinstruction, 0, (6) - (0) + 1) ==
                      UINT64_C(0x3b))) &&
                sv_slice(want.pfetched.pinstruction, 25, (31) - (25) + 1) ==
                    1 &&
                sv_slice(want.pfetched.pinstruction, 12, (14) - (12) + 1) < 4)
              multiply_authorized_cycle[want.pfetched.ppc] = cycles;
            if ((sv_slice(want.pfetched.pinstruction, 0, (6) - (0) + 1) ==
                     UINT64_C(3) ||
                 sv_slice(want.pfetched.pinstruction, 0, (6) - (0) + 1) ==
                     UINT64_C(0x23) ||
                 sv_slice(want.pfetched.pinstruction, 0, (6) - (0) + 1) ==
                     UINT64_C(0x2f) ||
                 sv_slice(want.pfetched.pinstruction, 0, (6) - (0) + 1) ==
                     UINT64_C(15)) ||
                (sv_slice(want.pfetched.pinstruction, 28, (31) - (28) + 1) ==
                     UINT64_C(6) &&
                 sv_slice(want.pfetched.pinstruction, 12, (14) - (12) + 1) ==
                     4))
              response_owners.push_back(want);
          }
          if (want.pwrite && !retired(lane).pbits.pdeferred)
            CHECK(retired(lane).pbits.prd == want.prd &&
                  retired(lane).pbits.pdata == want.pdata);
          commits++;
        }
      }
      if (memory_in.presponse.pvalid && memory_out.presponse.pready &&
          !response_management[response_read]) {
        retirement_t owner;
        CHECK(response_owners.size() > 0);
        owner = response_owners.take();
        if (owner.pwrite) {
          CHECK(issued <= 1);
          reserved_slots++;
        }
      }
      if (completed_out.pvalid) {
        retirement_t want;
        int index;
        if (completed_out.pbits.pfetched.ppc >= UINT64_C(0xac08) &&
            completed_out.pbits.pfetched.ppc < UINT64_C(0xac48)) {
          if (multiply_stream_count != 0)
            CHECK(cycles == multiply_stream_cycle + 1);
          multiply_stream_cycle = cycles;
          multiply_stream_count++;
        }
        if (multiply_authorized_cycle.contains(
                completed_out.pbits.pfetched.ppc)) {
          CHECK(cycles ==
                multiply_authorized_cycle[completed_out.pbits.pfetched.ppc] +
                    1);
          multiply_authorized_cycle.erase(completed_out.pbits.pfetched.ppc);
        }
        index = -1;
        CHECK(expected_completions.size() > 0);
        for (unsigned i = 0; i < expected_completions.size(); ++i)
          if (expected_completions[i].pfetched.ppc ==
              completed_out.pbits.pfetched.ppc)
            index = i;
        CHECK(index >= 0);
        want = expected_completions[index];
        expected_completions.erase(expected_completions.begin() + index);
        CHECK(same_instruction(completed_out.pbits.pfetched, want.pfetched) &&
              completed_out.pbits.pwrite == want.pwrite &&
              !completed_out.pbits.pdeferred);
        if (want.pwrite)
          CHECK(completed_out.pbits.prd == want.prd &&
                completed_out.pbits.pdata == want.pdata);
      }
      if (redirect_out.pvalid) {
        redirect_t want;
        if (redirect_out.pbits.presolution.pdisposition == 0) {
          CHECK((memory_stage(0).pvalid &&
                 memory_stage(0).pbits.ppc == redirect_out.pbits.ppc) ||
                (memory_stage(1).pvalid &&
                 memory_stage(1).pbits.ppc == redirect_out.pbits.ppc));
          for (int lane = 0; lane < 2; lane++)
            CHECK(!retired(lane).pvalid ||
                  retired(lane).pbits.pfetched.ppc != redirect_out.pbits.ppc);
          mem_branch_redirects++;
        } else {
          for (int lane = 0; lane < 2; lane++)
            if (memory_stage(lane).pvalid &&
                sv_slice(memory_stage(lane).pbits.pinstruction, 0,
                         (6) - (0) + 1) == UINT64_C(0x6f))
              wb_overrides++;
        }
        CHECK(expected_redirects.size() > 0);
        want = expected_redirects.take();
        CHECK(redirect_out.pbits.ppc == want.ppc &&
              redirect_out.pbits.ptarget == want.ptarget &&
              redirect_out.pbits.presolution.pdisposition ==
                  want.presolution.pdisposition);
        if (want.presolution.pdisposition == 1)
          CHECK(redirect_out.pbits.presolution.pcause ==
                    want.presolution.pcause &&
                redirect_out.pbits.presolution.pvalue ==
                    want.presolution.pvalue &&
                response_count == 0 && expected_completions.size() == 0);
        CHECK(!instructions_out.pready && issued == 0);
        stops++;
      }
    }
  }
}

void falling_update() {
  split_in.presponse.pvalid =
      !reset && split_active && cycles >= split_due && !hold_split;
  memory_in.presponse.pvalid = !reset && response_count > 0 &&
                               cycles >= response_due[response_read] &&
                               !hold_responses;
}
