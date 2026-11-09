// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    load_data.words = {0x83828180, 0x87868584, 0x8b8a8988, 0x8f8e8d8c};
    store_value = UINT64_C(0x0123456789abcdef);
    for (unsigned size = 0; size <= 3; ++size) {
      const unsigned bytes = 1u << size;
      for (unsigned offset = 0; offset + bytes <= 16; ++offset) {
        address = offset;
        width = size;
        unsigned_load = 1;
        eval();
        CHECK(store_mask == (((1u << bytes) - 1) << offset));
        for (unsigned lane = 0; lane < 16; ++lane) {
          const unsigned expected = lane >= offset && lane < offset + 8
              ? (store_value >> (8 * (lane - offset))) & 255 : 0;
          CHECK(((store_data.words[lane / 4] >> (8 * (lane % 4))) & 255) == expected);
        }
        std::uint64_t value = 0;
        for (unsigned i = 0; i < bytes; ++i)
          value |= std::uint64_t(0x80 + offset + i) << (8 * i);
        CHECK(load_value == value);
        unsigned_load = 0;
        eval();
        CHECK(load_value == (value | ~low_mask(8 * bytes)));
      }
    }
  });
}
