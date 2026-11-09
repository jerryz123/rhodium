// Exhaustively checks legal RV64 load/store lanes and size-based alignment.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
constexpr std::uint8_t BYTE = UINT64_C(0);
constexpr std::uint8_t HALF = UINT64_C(1);
constexpr std::uint8_t WORD = UINT64_C(2);
constexpr std::uint8_t DOUBLE = UINT64_C(3);

std::uint8_t base_mask(std::uint8_t size) {
  std::uint8_t return_value{};

  switch (size) {
  case BYTE:
    return_value = UINT64_C(1);

    break;
  case HALF:
    return_value = UINT64_C(3);

    break;
  case WORD:
    return_value = UINT64_C(15);

    break;
  default:
    return_value = UINT64_C(255);

    break;
  }

  return return_value;
}

std::uint64_t expected_load(std::uint8_t size, bool is_unsigned,
                            std::uint8_t offset) {
  const unsigned bits = 8u << size;
  auto value = (load_data >> (offset * 8)) & low_mask(bits);
  if (!is_unsigned && (value & (UINT64_C(1) << (bits - 1))))
    value |= ~low_mask(bits);
  return value;
}

void check_legal_lane(std::uint8_t size, std::uint8_t offset) {
  address = UINT64_C(4096) + offset;
  width = size;
  unsigned_load = UINT64_C(0);
  eval();
  CHECK(aligned);
  CHECK(load_value == expected_load(size, UINT64_C(0), offset));
  CHECK(store_data == (store_value << (offset * 8)));
  CHECK(store_mask == (base_mask(size) << offset));

  unsigned_load = UINT64_C(1);
  eval();
  CHECK(load_value == expected_load(size, UINT64_C(1), offset));
}

void check_unaligned(std::uint8_t size, std::uint8_t offset) {
  address = UINT64_C(4096) + offset;
  width = size;
  eval();
  CHECK(!aligned);
}

int main() {
  return run_test([] {
    load_data = UINT64_C(9295287800335236992);
    store_value = UINT64_C(9833440827789222417);

    for (int offset = 0; offset < 8; offset++)
      check_legal_lane(BYTE, slice(offset, 2, 0));
    for (int offset = 0; offset < 8; offset += 2)
      check_legal_lane(HALF, slice(offset, 2, 0));
    for (int offset = 0; offset < 8; offset += 4)
      check_legal_lane(WORD, slice(offset, 2, 0));
    check_legal_lane(DOUBLE, UINT64_C(0));

    check_unaligned(HALF, UINT64_C(1));
    check_unaligned(WORD, UINT64_C(2));
    check_unaligned(DOUBLE, UINT64_C(4));
  });
}
