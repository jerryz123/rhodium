// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "statistics.h"
#include "collector.h"
#include <array>

static rhodium::statistics::Collector collector(rhodium::statistics::descriptors);
static std::array<unsigned, 6> counts{};
extern "C" void rhodium_stat_count(int id) {
  CHECK(id >= 0 && id < 6);
  collector.count(id);
  ++counts[id];
}

int main() {
  return run_test([] {
    std::array<unsigned, 6> expected{};
    for (unsigned resetting = 0; resetting < 2; ++resetting) {
      reset = resetting;
      for (unsigned bits = 0; bits < 256; ++bits) {
        flags = bits;
        eval();
        // Ordered independent oracle: stale entry, conditional direction,
        // return/indirect/direct target, then a stack-action-only repair.
        unsigned kind = 0;
        if (!(bits & 1) && (bits & (4 | 8 | 64))) kind = 5;
        else if ((bits & 7) == 7) kind = 1;
        else if (bits & 8) kind = (bits & 32) ? 4 : (bits & 16) ? 3 : 2;
        else if (bits & 64) kind = 6;
        CHECK(failure == kind);
        if (!resetting && (bits & 128) && kind) ++expected[kind - 1];
        collector.begin(reset);
        tick_model();
        collector.end();
        CHECK(counts == expected);
      }
    }
    collector.freeze();
    CHECK(!collector.json().empty());
  });
}
