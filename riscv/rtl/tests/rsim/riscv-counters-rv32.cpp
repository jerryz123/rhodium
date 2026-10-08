// Verifies RV32 counter halves, retirement increments, writes, and overflow.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
constexpr std::uint16_t CSR_MCYCLE = UINT64_C(2816);
constexpr std::uint16_t CSR_MINSTRET = UINT64_C(2818);
constexpr std::uint16_t CSR_MCYCLEH = UINT64_C(2944);
constexpr std::uint16_t CSR_MINSTRETH = UINT64_C(2946);

void step(std::uint8_t retire_value, bool write_valid,
          std::uint16_t write_address, std::uint32_t write_value) {
  eval();
  retire_count = retire_value;
  machine_write_in.pvalid = write_valid;
  machine_write_in.pbits.paddress = write_address;
  machine_write_in.pbits.pvalue = write_value;
  tick_model();
}

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    reset = UINT64_C(1);
    retire_count = UINT64_C(0);
    machine_write_in = {};
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    reset = UINT64_C(0);
    CHECK(cycle == 0 && instret == 0);

    step(UINT64_C(0), UINT64_C(0), {}, {});
    CHECK(cycle == 1 && instret == 0);
    step(UINT64_C(1), UINT64_C(0), {}, {});
    CHECK(cycle == 2 && instret == 1);

    step(UINT64_C(1), UINT64_C(1), CSR_MINSTRET, UINT64_C(4294967295));
    CHECK(cycle == 3 && instret == UINT64_C(4294967295));
    step(UINT64_C(1), UINT64_C(1), CSR_MINSTRETH, UINT64_C(4294967295));
    CHECK(cycle == 4 && instret == UINT64_C(18446744073709551615));
    step(UINT64_C(1), UINT64_C(0), {}, {});
    CHECK(cycle == 5 && instret == 0);

    step(UINT64_C(0), UINT64_C(1), CSR_MCYCLE, UINT64_C(4294967295));
    CHECK(cycle == UINT64_C(4294967295) && instret == 0);
    step(UINT64_C(0), UINT64_C(1), CSR_MCYCLEH, UINT64_C(305419896));
    CHECK(cycle == UINT64_C(1311768469162688511));
    step(UINT64_C(0), UINT64_C(0), {}, {});
    CHECK(cycle == UINT64_C(1311768469162688512));

    step(2, 0, {}, {});
    CHECK(instret == 2);
    step(2, 1, CSR_MINSTRET, UINT64_C(4294967294));
    step(2, 1, CSR_MINSTRETH, UINT64_C(4294967295));
    CHECK(instret == UINT64_C(18446744073709551614));
    step(2, 0, {}, {});
    CHECK(instret == 0);
  });
}
