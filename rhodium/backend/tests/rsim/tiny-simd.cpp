// Checks tiny-simd behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void write_instruction(std::uint8_t address, std::uint8_t opcode,
                       std::uint8_t operand) {
  loader_in = {.paddress = address,
               .pdata = std::uint16_t((opcode << 8) | operand),
               .pwrite = UINT64_C(1)};
  tick_model();
}

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    load_seed = UINT64_C(0);
    step = UINT64_C(0);
    seeds[0] = UINT64_C(3);
    seeds[1] = UINT64_C(5);
    loader_in = {};

    // The program is loaded while synchronous reset holds the architectural
    // registers at zero. Memory itself deliberately has no reset semantics.
    write_instruction(UINT64_C(0), UINT64_C(1), UINT64_C(2));  // Add
    write_instruction(UINT64_C(1), UINT64_C(8), UINT64_C(3));  // Multiply
    write_instruction(UINT64_C(2), UINT64_C(2), UINT64_C(15)); // Xor
    write_instruction(UINT64_C(3), UINT64_C(3), UINT64_C(1));  // ShiftLeft
    loader_in.pwrite = UINT64_C(0);

    CHECK(loader_out.pready == UINT64_C(1));

    reset = UINT64_C(0);
    load_seed = UINT64_C(1);
    tick_model();
    CHECK(results[0] == UINT64_C(3) && results[1] == UINT64_C(5));

    load_seed = UINT64_C(0);
    step = UINT64_C(1);
    tick_model();
    CHECK(results[0] == UINT64_C(5) && results[1] == UINT64_C(7) &&
          pc_out == UINT64_C(1));

    tick_model();
    CHECK(results[0] == UINT64_C(15) && results[1] == UINT64_C(21) &&
          pc_out == UINT64_C(2));

    tick_model();
    CHECK(results[0] == UINT64_C(0) && results[1] == UINT64_C(26) &&
          pc_out == UINT64_C(3));

    tick_model();
    CHECK(results[0] == UINT64_C(0) && results[1] == UINT64_C(52) &&
          pc_out == UINT64_C(0));
  });
}
