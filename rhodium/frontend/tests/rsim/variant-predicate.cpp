// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = 1;
    tick_model();
    reset = 0;
    bool held_read = false;
    for (unsigned i = 0; i < 512; ++i) {
      const unsigned tag = i % 3;
      const unsigned byte = (i * 37) & 255;
      const bool active = i & 1;
      const bool read = tag == 1;
      request = (tag << 16) | (byte << 8) | byte;
      enable = active;
      capture = (i % 5) != 0;
      explicit_request = ((i & 2 ? 9 : 3) << 8) | byte;
      one_hot_request = ((i & 4 ? 2 : 1) << 8) | byte;
      option = ((i & 8 ? 1 : 0) << 8) | byte;
      message = ((i & 16 ? 1 : 0) << 9) | ((i & 32 ? 1 : 0) << 8) | byte;
      eval();
      const bool expected[] = {read && active, tag != 0, read && active,
        read, read && active, held_read, bool(i & 2), bool(i & 4), bool(i & 8),
        bool(i & 16) && bool(i & 32), read, read};
      for (unsigned index = 0; index < 12; ++index)
        CHECK(predicates[index] == expected[index]);
      CHECK(packed_predicate == read);
      CHECK(payload == (read ? byte : 0));
      if (capture) held_read = read && active;
      tick_model();
      CHECK(predicates[5] == held_read);
    }
    reset = 1;
    tick_model();
    CHECK(predicates[5] == 0);
  });
}
