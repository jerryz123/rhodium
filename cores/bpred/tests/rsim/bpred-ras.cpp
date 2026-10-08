// Checks speculative RAS updates, resolved recovery, wraparound, underflow, and
// coroutine replacement.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
constexpr std::uint8_t NONE = 0, PUSH = 1, POP = 2, POP_PUSH = 3;

std::uint32_t jal(std::uint8_t rd) {
  std::uint32_t return_value{};

  return (std::uint32_t(rd) << 7) | 0x6f;

  return return_value;
}
std::uint32_t jalr(std::uint8_t rd, std::uint8_t rs1) {
  std::uint32_t return_value{};

  return (std::uint32_t(rs1) << 15) | (std::uint32_t(rd) << 7) | 0x67;

  return return_value;
}
void check_classification(std::uint32_t encoded, std::uint8_t expected) {
  instruction = encoded;
  eval();
  CHECK(classification == expected);
}
void check_compressed_classification(std::uint16_t encoded,
                                     std::uint8_t expected) {
  compressed_instruction = encoded;
  eval();
  CHECK(compressed_classification == expected);
}
void check_rv32_compressed_classification(std::uint16_t encoded,
                                          std::uint8_t expected) {
  compressed_instruction = encoded;
  eval();
  CHECK(compressed_classification_rv32 == expected);
}

void speculate(std::uint8_t action, std::uint64_t address = 0) {
  tick_model();
  speculate_in = {.pvalid = UINT64_C(1),
                  .pbits = {.paction = action, .preturn_uaddress = address}};
  tick_model();
  speculate_in = {};
}
void resolve(std::uint8_t actual, std::uint8_t predicted,
             std::uint64_t address = 0) {
  tick_model();
  resolve_in = {
      .pvalid = UINT64_C(1),
      .pbits = {.pactual = {.paction = actual, .preturn_uaddress = address},
                .ppredicted_uaction = predicted}};
  tick_model();
  resolve_in = {};
}
void pulse_restore() {
  tick_model();
  restore_in.pvalid = 1;
  tick_model();
  restore_in.pvalid = 0;
}
void pulse_clear() {
  tick_model();
  clear_in.pvalid = 1;
  tick_model();
  clear_in.pvalid = 0;
}
void check(bool valid, std::uint64_t address = 0) {
  eval();
  CHECK(head_valid == valid && (!valid || head == address));
}
void push_and_resolve(std::uint64_t address) {
  speculate(PUSH, address);
  resolve(PUSH, PUSH, address);
}

int main() {
  return run_test([] {
    reset = 1;
    instruction = 0;
    compressed_instruction = 0;
    speculate_in = {};
    resolve_in = {};
    restore_in = {};
    clear_in = {};

    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    reset = 0;
    check(0);
    check_classification(jal(1), PUSH);
    check_classification(jal(5), PUSH);
    check_classification(jal(2), NONE);
    check_classification(jalr(1, 2), PUSH);
    check_classification(jalr(0, 1), POP);
    check_classification(jalr(0, 5), POP);
    check_classification(jalr(1, 1), PUSH);
    check_classification(jalr(5, 5), PUSH);
    check_classification(jalr(1, 5), POP_PUSH);
    check_classification(jalr(5, 1), POP_PUSH);
    check_classification(jalr(0, 2), NONE);
    check_compressed_classification(UINT64_C(32898), POP);
    check_compressed_classification(UINT64_C(33410), POP);
    check_compressed_classification(UINT64_C(36994), PUSH);
    check_compressed_classification(UINT64_C(37506), POP_PUSH);
    check_compressed_classification(UINT64_C(33026), NONE);
    check_compressed_classification(UINT64_C(8193), NONE);
    check_rv32_compressed_classification(UINT64_C(8193), PUSH);

    speculate(PUSH, UINT64_C(256));
    speculate(PUSH, UINT64_C(512));
    check(1, UINT64_C(512));
    resolve(PUSH, PUSH, UINT64_C(256));
    resolve(PUSH, PUSH, UINT64_C(512));
    pulse_restore();
    check(1, UINT64_C(512));

    speculate(POP);
    check(1, UINT64_C(256));
    resolve(POP, POP);
    check(1, UINT64_C(256));
    speculate(PUSH, UINT64_C(768));
    check(1, UINT64_C(768));
    resolve(NONE, PUSH);
    check(1, UINT64_C(256));

    speculate(PUSH, UINT64_C(1024));
    pulse_restore();
    check(1, UINT64_C(256));
    speculate(POP_PUSH, UINT64_C(1280));
    resolve(POP_PUSH, POP_PUSH, UINT64_C(1280));
    check(1, UINT64_C(1280));

    pulse_clear();
    push_and_resolve(UINT64_C(16));
    push_and_resolve(UINT64_C(32));
    push_and_resolve(UINT64_C(48));
    push_and_resolve(UINT64_C(64));
    check(1, UINT64_C(64));
    speculate(POP);
    check(1, UINT64_C(48));
    resolve(POP, POP);
    speculate(POP);
    check(1, UINT64_C(32));
    resolve(POP, POP);
    speculate(POP);
    resolve(POP, POP);
    check(0);
    speculate(POP);
    resolve(POP, POP);
    check(0);
    speculate(POP_PUSH, UINT64_C(1536));
    resolve(POP_PUSH, POP_PUSH, UINT64_C(1536));
    check(1, UINT64_C(1536));

    pulse_clear();
    check(0);
  });
}
