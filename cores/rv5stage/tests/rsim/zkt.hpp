// Compares two independent models under identical delays and distinct operand data.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
using Word = std::conditional_t<XLEN == 32, std::uint32_t, std::uint64_t>;
inline void set_multiply_control(unsigned control) {
  multiply_control.pmode.pleft_usigned = (control >> 3) & 1;
  multiply_control.pmode.pright_usigned = (control >> 2) & 1;
  multiply_control.phigh_uresult = (control >> 1) & 1;
  multiply_control.pword_uresult = control & 1;
}
inline Model second;
inline Model &model(unsigned index) { return index ? second : dut; }
inline auto &pair_multiply_left(unsigned index) {
  return model(index).inputs.pmultiply_uleft;
}
inline auto &pair_multiply_right(unsigned index) {
  return model(index).inputs.pmultiply_uright;
}
inline auto &pair_load_value(unsigned index) {
  return model(index).inputs.pload_uvalue;
}
inline auto &pair_ii(unsigned index) {
  return model(index).inputs.pinstruction_uaccess_uin;
}
inline auto &pair_di(unsigned index) {
  return model(index).inputs.pdata_uaccess_uin;
}
inline auto &pair_completion_value(unsigned index) {
  return model(index).outputs().pcompletion_uvalue;
}
inline auto &pair_multiply_available(unsigned index) {
  return model(index).outputs().pmultiply_uavailable;
}
inline auto &pair_multiply_valid(unsigned index) {
  return model(index).outputs().pmultiply_uvalid;
}
inline auto &pair_completion_valid(unsigned index) {
  return model(index).outputs().pcompletion_uvalid;
}
inline auto &pair_completion_chosen(unsigned index) {
  return model(index).outputs().pcompletion_uchosen;
}
inline auto &pair_privilege(unsigned index) {
  return model(index).outputs().pprivilege;
}
inline auto &pair_mstatus(unsigned index) {
  return model(index).outputs().pmstatus;
}
inline auto &pair_satp(unsigned index) { return model(index).outputs().psatp; }
inline auto &pair_translation_flush(unsigned index) {
  return model(index).outputs().ptranslation_uflush;
}
inline auto &pair_program_word(unsigned index) {
  return model(index).outputs().pprogram_uword;
}
inline auto &pair_probe_count(unsigned index) {
  return model(index).outputs().pprobe_ucount;
}
inline auto &pair_io(unsigned index) {
  return model(index).outputs().pinstruction_uaccess_uout;
}
inline auto &pair_dout(unsigned index) {
  return model(index).outputs().pdata_uaccess_uout;
}
inline uint128 extend_operand(std::uint64_t value, bool signed_operand) {
  auto low = uint128(value & low_mask(XLEN));
  return signed_operand && (low >> (XLEN - 1)) ? low | (mask128(XLEN) << XLEN)
                                               : low;
}

bool run_core = 0;

int contended_cycles = 0;

bool instruction_pending;
std::uint32_t instruction_word;
bool instruction_request_fire;
bool instruction_response_fire;
int response_delay;
std::uint8_t response_rd;
bool response_second;
int cycle, stores, trial, schedule, total_probes = 0, differing_results = 0,
                                    flush_restarts = 0;
std::uint64_t total_cycles = 0;
Word operand_a[2], operand_b[2];

Word varied_operand(int index) {
  switch (index) {
  case 0: {
    return {};
  } break;
  case 1: {
    return ((1) & low_mask(XLEN));
  } break;
  case 2: {
    return low_mask(XLEN);
  } break;
  case 3: {
    return ((1) & low_mask(XLEN)) << (XLEN - 1);
  } break;
  case 4: {
    return (((1) & low_mask(XLEN)) << (XLEN - 1)) - ((1) & low_mask(XLEN));
  } break;
  case 5: {
    return ((UINT64_C(0xaaaaaaaaaaaaaaaa)) & low_mask(XLEN));
  } break;
  case 6: {
    return ((UINT64_C(0x5555555555555555)) & low_mask(XLEN));
  } break;
  case 7: {
    return ((UINT64_C(0x80000000)) & low_mask(XLEN));
  } break;
  case 8: {
    return ((UINT64_C(0x7fffffff)) & low_mask(XLEN));
  } break;
  case 9: {
    return ((UINT64_C(0xffffffff)) & low_mask(XLEN));
  } break;
  default: {
    {
      if (index < 10 + XLEN)
        return ((1) & low_mask(XLEN)) << (index - 10);
      return ((UINT64_C(0x9e3779b97f4a7c15) *
               (((index)&low_mask(64)) + UINT64_C(1))) &
              low_mask(XLEN)) ^
             ((UINT64_C(0xd1b54a32d192ed03)) & low_mask(XLEN));
    }
  } break;
  }
}

// Sample pre-edge interface events. Both environments use exactly the same
// delays and addresses; only the data returned by operand loads differs.

void tick() {
  rising();
  settle();
}

// The same public multiplier adapter and priority topology as core WB.
// Force valid overlap and sink stalls; check latency, priority, held data,
// and exactly-once consumption without naming any generated internal wire.
void check_completion_contention() {
  uint128 left_extended, right_extended, product;
  Word expected[2];
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    tick();
  falling();
  reset = 0;
  for (int mode = 0; mode < (XLEN == 64 ? 5 : 4); mode++) {
    for (int sample = 0; sample < 96; sample++) {
      falling();
      // low, signed high, signed/unsigned high, unsigned high, word low
      switch (mode) {
      case 0: {
        set_multiply_control(0);
      } break;
      case 1: {
        set_multiply_control(14);
      } break;
      case 2: {
        set_multiply_control(10);
      } break;
      case 3: {
        set_multiply_control(2);
      } break;
      case 4: {
        set_multiply_control(1);
      } break;
      }
      pair_multiply_left(0) = 0;
      pair_multiply_right(0) = 0;
      pair_multiply_left(1) = varied_operand(sample);
      pair_multiply_right(1) = varied_operand((sample * 17 + mode) % 96);
      for (int g = 0; g < 2; g++) {
        left_extended = extend_operand(pair_multiply_left(g),
                                       multiply_control.pmode.pleft_usigned);
        right_extended = extend_operand(pair_multiply_right(g),
                                        multiply_control.pmode.pright_usigned);
        product = left_extended * right_extended;
        expected[g] = multiply_control.phigh_uresult
                          ? bit_slice(product, XLEN, XLEN)
                          : bit_slice(product, 0, XLEN);
        if (multiply_control.pword_uresult)
          expected[g] =
              ((std::int64_t(sign_extend(bit_slice(product, 0, 32), 32))) &
               low_mask(XLEN));
        pair_load_value(g) = varied_operand(sample + g);
        CHECK(pair_multiply_available(g));
      }
      multiply_issue = 1;
      tick();
      falling();
      multiply_issue = 0;
      load_valid = 1;
      completion_ready = 0;
      for (int waited = 0; !pair_multiply_valid(0); waited++) {
        CHECK(waited < XLEN + 4 &&
              pair_multiply_valid(0) == pair_multiply_valid(1));
        tick();
      }
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
        for (int g = 0; g < 2; g++) {
          // Availability now reserves the scalar WB queue, independently of
          // a held execution result. Neither operand can affect that space.
          CHECK(pair_multiply_valid(g) && pair_multiply_available(g) &&
                pair_completion_valid(g) && pair_completion_chosen(g) == 0 &&
                pair_completion_value(g) == pair_load_value(g));
        }
        contended_cycles++;
        tick();
      }
      falling();
      completion_ready = 1;
      for (int repeat_index = 0; repeat_index < (4); ++repeat_index) {
        tick();
        CHECK(pair_multiply_valid(0) && pair_multiply_valid(1) &&
              pair_completion_chosen(0) == 0 && pair_completion_chosen(1) == 0);
        contended_cycles++;
      }
      falling();
      load_valid = 0;
      settle();
      for (int g = 0; g < 2; g++) {
        CHECK(pair_completion_valid(g) && pair_completion_chosen(g) == 1 &&
              pair_completion_value(g) == expected[g]);
      }
      tick();
      CHECK(!pair_multiply_valid(0) && !pair_multiply_valid(1) &&
            !pair_completion_valid(0) && !pair_completion_valid(1));
    }
  }
}

void drive() {
  {
    for (int g = 0; g < 2; g++) {
      pair_ii(g) = {};
      pair_ii(g).prequest.pready =
          run_core && (!instruction_pending || pair_io(g).pflush) &&
          (schedule == 0 || cycle % (schedule == 1 ? 5 : 3) != 0);
      pair_ii(g).presponse.pvalid = instruction_pending;
      pair_ii(g).presponse.pbits.pword = instruction_word;
      pair_di(g) = {};
      pair_di(g).prequest.pready =
          response_delay == 0 &&
          (schedule == 0 || cycle % (schedule == 1 ? 7 : 5) > 1);
      pair_di(g).pdrained = response_delay == 0;
      pair_di(g).presponse.pvalid = response_delay == 1;
      pair_di(g).presponse.pbits.pcontext.pwriteback =
          memory_integer(response_rd);
      pair_di(g).presponse.pbits.pdata =
          response_second ? operand_b[g] : operand_a[g];
    }
  }
  instruction_request_fire =
      pair_io(0).prequest.pvalid && pair_ii(0).prequest.pready;
  instruction_response_fire =
      instruction_pending && pair_io(0).presponse.pready;
}

void observe() {
  {
    if (reset) {
      defer(instruction_pending, 0);
      defer(instruction_word, UINT64_C(19));
      defer(response_delay, 0);
      defer(response_rd, 0);
      defer(response_second, 0);
      defer(cycle, 0);
      defer(stores, 0);
    } else {
      defer(cycle, cycle + 1);
      CHECK((field(pair_io(0).prequest.pvalid, 1, 3) |
             field(pair_io(0).presponse.pready, 1, 2) |
             field(pair_io(0).pflush, 1, 1) |
             field(pair_io(0).pinvalidate_uall, 1, 0)) ==
            (field(pair_io(1).prequest.pvalid, 1, 3) |
             field(pair_io(1).presponse.pready, 1, 2) |
             field(pair_io(1).pflush, 1, 1) |
             field(pair_io(1).pinvalidate_uall, 1, 0)));
      if (pair_io(0).prequest.pvalid) {
        CHECK(pair_io(0).prequest.pbits.paddress ==
              pair_io(1).prequest.pbits.paddress);
        CHECK(pair_io(0).prequest.pbits.paddress >= UINT64_C(0x10000) &&
              pair_io(0).prequest.pbits.paddress < UINT64_C(0x20000));
      }
      CHECK((field(pair_dout(0).prequest.pvalid, 1, 1) |
             field(pair_dout(0).presponse.pready, 1, 0)) ==
            (field(pair_dout(1).prequest.pvalid, 1, 1) |
             field(pair_dout(1).presponse.pready, 1, 0)));
      CHECK(pair_privilege(0) == pair_privilege(1) &&
            pair_mstatus(0) == pair_mstatus(1) &&
            pair_satp(0) == pair_satp(1) &&
            pair_translation_flush(0) == pair_translation_flush(1));
      if (pair_dout(0).prequest.pvalid) {
        CHECK(
            pair_dout(0).prequest.pbits.paddress ==
                pair_dout(1).prequest.pbits.paddress &&
            pair_dout(0).prequest.pbits.paccess ==
                pair_dout(1).prequest.pbits.paccess &&
            pair_dout(0).prequest.pbits.pwidth ==
                pair_dout(1).prequest.pbits.pwidth &&
            bit_slice(pair_dout(0).prequest.pbits.pcontext.pwriteback, 7, 2) ==
                bit_slice(pair_dout(1).prequest.pbits.pcontext.pwriteback, 7,
                          2) &&
            memory_rd(pair_dout(0).prequest.pbits.pcontext.pwriteback) ==
                memory_rd(pair_dout(1).prequest.pbits.pcontext.pwriteback));
      }
      if (pair_io(0).pflush) {
        defer(instruction_pending, instruction_request_fire);
        if (instruction_request_fire) {
          defer(instruction_word, pair_program_word(0));
          defer(flush_restarts, flush_restarts + 1);
        }
      } else {
        if (instruction_response_fire)
          defer(instruction_pending, 0);
        if (instruction_request_fire) {
          defer(instruction_pending, 1);
          defer(instruction_word, pair_program_word(0));
        }
      }
      if (response_delay > 0)
        defer(response_delay, response_delay - 1);
      if (pair_dout(0).prequest.pvalid && pair_di(0).prequest.pready) {
        switch (pair_dout(0).prequest.pbits.paccess) {
        case 1: {
          {
            CHECK(pair_dout(0).prequest.pbits.paddress == 0 ||
                  pair_dout(0).prequest.pbits.paddress == 8);
            defer(response_delay,
                  schedule == 0 ? 2 : (3 + cycle % (schedule == 1 ? 5 : 11)));
            defer(response_rd,
                  memory_rd(pair_dout(0).prequest.pbits.pcontext.pwriteback));
            defer(response_second, pair_dout(0).prequest.pbits.paddress == 8);
          }
        } break;
        case 2: {
          {
            CHECK(pair_dout(0).prequest.pbits.paddress == 16);
            defer(stores, stores + 1);
            defer(total_probes, total_probes + 1);
            if (pair_dout(0).prequest.pbits.pdata !=
                pair_dout(1).prequest.pbits.pdata)
              defer(differing_results, differing_results + 1);
          }
        } break;
        default: {
          fail(1, "unexpected memory operation");
        } break;
        }
      }
    }
  }
}

void falling_update() {}

void stimulus() {
  check_completion_contention();
  run_core = 1;
  for (schedule = 0; schedule < 3; schedule++) {
    for (trial = 0; trial < 96; trial++) {
      falling();
      reset = 1;
      operand_a[0] = 0;
      operand_b[0] = 0;
      operand_a[1] = varied_operand(trial);
      operand_b[1] = varied_operand((trial * 17 + schedule) % 96);
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      reset = 0;
      while (stores < int(pair_probe_count(0)) && cycle < 100000)
        falling();
      CHECK(stores == int(pair_probe_count(0)));
      total_cycles += ((cycle)&low_mask(64));
    }
  }
  CHECK(differing_results > 0);
  CHECK(flush_restarts > 0);

  throw Finished{};
}
int main() {
  return run_test([] {
    eval_extra = [] {
      second.inputs.preset = reset;
      second.inputs.pmultiply_uissue = multiply_issue;
      second.inputs.pload_uvalid = load_valid;
      second.inputs.pcompletion_uready = completion_ready;
      second.inputs.pmultiply_ucontrol = multiply_control;
      second.eval();
    };
    tick_extra = [] { second.tick(); };
    reset = 1;
    multiply_issue = 0;
    load_valid = 0;
    completion_ready = 0;
    set_multiply_control(0);
    cycle_limit = 10000000;
    try {
      stimulus();
    } catch (const Finished &) {
    }
  });
}
