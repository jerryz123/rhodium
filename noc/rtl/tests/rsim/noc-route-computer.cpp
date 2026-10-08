// Exhaustively checks every local router lookup in the validated two-hop
// fixture.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <random>
std::mt19937 rng(0x1234);

int main() {
  return run_test([] {
    for (int key = 0; key < 4; key++) {
      std::uint8_t expected_source;
      std::uint8_t expected_middle;
      std::uint8_t expected_destination;

      source_route_key = ((key >> 1) & 1);
      source_origin_key = ((key >> 0) & 1);
      middle_route_key = ((key >> 1) & 1);
      middle_origin_key = ((key >> 0) & 1);
      destination_route_key = ((key >> 1) & 1);
      destination_origin_key = ((key >> 0) & 1);

      switch (key) {
      case 0: {
        expected_source = UINT64_C(7);
        expected_middle = UINT64_C(3);
        expected_destination = UINT64_C(3);
      }

      break;
      case 1: {
        expected_source = UINT64_C(0);
        expected_middle = UINT64_C(5);
        expected_destination = UINT64_C(3);
      }

      break;
      default: {
        expected_source = UINT64_C(0);
        expected_middle = UINT64_C(0);
        expected_destination = UINT64_C(0);
      }

      break;
      }

      eval();
      CHECK(((source_target_mask << 1) | source_valid) == expected_source &&
            source_fallback_mask == source_target_mask);
      CHECK(((middle_target_mask << 1) | middle_valid) == expected_middle &&
            middle_fallback_mask == middle_target_mask);
      CHECK(((destination_target_mask << 1) | destination_valid) ==
                expected_destination &&
            destination_fallback_mask == destination_target_mask);
    }
  });
}
