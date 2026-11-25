// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

using Word = decltype(response_in.pbits.pdata);
constexpr Word full_mask = ~Word(0);

void reserve_slot(unsigned tag) {
  CHECK(allocate_slot == tag && !state[tag].pallocated);
  reserve_in.pvalid = 1;
  reserve_in.pbits.powner = tag;
  tick_model();
  reserve_in = {};
  eval();
  CHECK(state[tag].pallocated && !state[tag].pwrites);
}

void resolve_load(unsigned tag, unsigned row) {
  CHECK(accept_slot == tag);
  memory_attempt_in = {};
  memory_attempt_in.pvalid = decision_in.pvalid = 1;
  memory_attempt_in.pbits.ptag = decision_in.pbits.ptag = tag;
  auto &beat = memory_attempt_in.pbits.pbeat;
  beat.pmemory = 1;
  beat.pwrite.paddress = row;
  beat.pwrite.pmask = full_mask;
  decision_in.pbits.pslow = 1;
}

void clear_resolution() {
  memory_attempt_in = {};
  decision_in = {};
  eval();
}

void check_claim(unsigned tag, unsigned row) {
  const auto &s = state[tag];
  CHECK(s.pallocated && s.paccepted && s.pwrites && s.pwrite_uaddress == row);
}

void offer_response(unsigned tag, Word data) {
  response_in.pvalid = 1;
  response_in.pbits.ptag = tag;
  response_in.pbits.pdata = data;
  eval();
}

void check_write(unsigned row, Word data) {
  CHECK(response_out.pready && write_out.pvalid && write_out.pbits.paddress == row);
  CHECK(write_out.pbits.pmask == full_mask && write_out.pbits.pdata == data);
}

int main() {
  return run_test([] {
    reset = 1;
    tick_model();
    reset = 0;
    eval();
    // Alternate same-edge resolution/return and out-of-order completion.
    // The same two tags are reused for different rows without resetting.
    for (unsigned round = 0; round < 8; ++round) {
      const unsigned a = 3 + round, b = 42 - round;
      const Word x = full_mask - round, y = 0x1234 + round;
      reserve_slot(0);
      reserve_slot(1);
      resolve_load(0, a);
      tick_model();
      clear_resolution();
      check_claim(0, a);

      if (!(round & 1)) {
        reserved_write = 1;
        offer_response(0, x);
        for (unsigned stall = 0; stall < 3; ++stall) {
          check_claim(0, a);
          CHECK(!response_out.pready && !write_out.pvalid);
          tick_model();
        }
        resolve_load(1, b);
        reserved_write = 0;
        eval();
        check_write(a, x);
        tick_model();
        response_in = {};
        clear_resolution();
        CHECK(!state[0].pallocated);
        check_claim(1, b);
        offer_response(1, y);
        check_write(b, y);
        tick_model();
      } else {
        resolve_load(1, b);
        tick_model();
        clear_resolution();
        check_claim(0, a);
        check_claim(1, b);
        offer_response(1, y);
        check_write(b, y);
        tick_model();
        response_in = {};
        eval();
        // Younger row is no longer a hazard, even before in-order reclamation.
        CHECK(state[1].pallocated && !state[1].pwrites);
        check_claim(0, a);
        offer_response(0, x);
        check_write(a, x);
        tick_model();
        response_in = {};
        eval();
        CHECK(!write_out.pvalid);
        tick_model();
      }
      response_in = {};
      eval();
      CHECK(!state[0].pallocated && !state[1].pallocated && !write_out.pvalid);
    }
  });
}
