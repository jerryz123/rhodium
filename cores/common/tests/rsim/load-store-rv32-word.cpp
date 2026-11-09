// Checks RV32 scalar load/store lane generation within one 32-bit cache word.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
constexpr std::uint8_t BYTE = UINT64_C(0);
constexpr std::uint8_t HALF = UINT64_C(1);
constexpr std::uint8_t WORD = UINT64_C(2);

void check_access(std::uint8_t access_width, std::uint8_t offset,
                  std::uint32_t expected_signed,
                  std::uint32_t expected_unsigned, std::uint8_t expected_mask) {
  {
    address = UINT64_C(4096) + offset;
    width = access_width;
    unsigned_load = UINT64_C(0);
    eval();
    CHECK(aligned && load_value == expected_signed &&
          store_data == std::uint32_t(store_value << (offset * 8)) &&
          store_mask == expected_mask);
    unsigned_load = UINT64_C(1);
    eval();
    CHECK(load_value == expected_unsigned);
  }
}

int main() {
  return run_test([] {
    load_data = UINT64_C(2164227841);
    store_value = UINT64_C(1144201745);
    check_access(BYTE, UINT64_C(0), UINT64_C(1), UINT64_C(1), UINT64_C(1));
    check_access(BYTE, UINT64_C(3), UINT64_C(4294967168), UINT64_C(128),
                 UINT64_C(8));
    check_access(HALF, UINT64_C(0), UINT64_C(32513), UINT64_C(32513),
                 UINT64_C(3));
    check_access(HALF, UINT64_C(2), UINT64_C(4294934783), UINT64_C(33023),
                 UINT64_C(12));
    check_access(WORD, UINT64_C(0), UINT64_C(2164227841), UINT64_C(2164227841),
                 UINT64_C(15));

    address = UINT64_C(4097);
    width = HALF;
    eval();
    CHECK(!aligned);
  });
}
