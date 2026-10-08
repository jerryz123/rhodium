// Exhausts reachable TMDS disparity transitions and tests controls, gaps, and
// reset.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "tmds-reference.hpp"
#include <bit>
#include <deque>
int running = 0, transitions = 0, next_state = 0, cursor = 0;
unsigned expected = 0x354, unused_symbol = 0;
std::vector<int> states;
std::deque<int> path;
bool reached[33]{}, active_codes[1024]{};
int parent[33]{}, parent_byte[33]{};

void sample(bool present, bool active, std::uint8_t value,
            std::uint8_t controls) {
  std::uint8_t recovered, minimized;
  eval();
  valid = present;
  data_enable = active;
  data = value;
  control = controls;
  if (present)
    expected = tmds_reference(value, controls, active, running);
  tick_model();
  CHECK(symbol_valid == present && symbol == expected);
  if (present && active) {
    active_codes[symbol] = 1;
    minimized = (symbol & 512) ? ~slice(symbol, 7, 0) : slice(symbol, 7, 0);
    recovered = minimized & 1;
    for (int i = 1; i < 8; i++)
      recovered |=
          (((minimized >> i) ^ (minimized >> (i - 1)) ^ !(symbol & 256)) & 1)
          << i;
    CHECK(recovered == value);
    CHECK(!(symbol == 852 || symbol == 171 || symbol == 340 || symbol == 683));
  }
}

void restart_encoder() {
  eval();
  reset = 1;
  valid = 1;
  data_enable = 1;
  tick_model();
  CHECK(!symbol_valid && symbol == UINT64_C(852));
  running = 0;
  expected = UINT64_C(852);
  eval();
  reset = 0;
  valid = 0;
}

int main() {
  return run_test([] {
    valid = 0;
    data_enable = 0;

    states.push_back(0);
    reached[16] = 1;
    for (int n = 0; n < states.size(); n++)
      for (int value = 0; value < 256; value++) {
        next_state = states[n];
        unused_symbol = tmds_reference(((value)&low_mask(8)), 0, 1, next_state);
        CHECK(next_state >= -16 && next_state <= 16);
        if (!reached[next_state + 16]) {
          reached[next_state + 16] = 1;
          parent[next_state + 16] = states[n];
          parent_byte[next_state + 16] = value;
          states.push_back(next_state);
        }
      }
    restart_encoder();
    for (int n = 0; n < states.size(); n++) {
      path.clear();
      cursor = states[n];
      while (cursor != 0) {
        path.push_front(parent_byte[cursor + 16]);
        cursor = parent[cursor + 16];
      }
      for (int value = 0; value < 256; value++) {
        sample(1, 0, UINT64_C(255), ((value)&low_mask(2)));
        for (std::size_t i = 0; i < path.size(); ++i)
          sample(1, 1, ((path[i]) & low_mask(8)), 0);
        CHECK(running == states[n]);

        sample(0, 0, ((~value) & low_mask(8)), ((value + 1) & low_mask(2)));
        sample(1, 1, ((value)&low_mask(8)), ((value)&low_mask(2)));
        sample(0, 1, ((~value) & low_mask(8)), 0);
        transitions++;
      }
    }

    for (int i = 0; i < 4096; i++)
      sample(1, 1, ((i * 73 + (i >> 4)) & low_mask(8)), 0);
    sample(1, 1, 0, 0);
    restart_encoder();
    sample(0, 0, UINT64_C(255), 3);
    sample(1, 1, UINT64_C(255), 0);
    for (int i = 0; i < 4; i++)
      sample(1, 0, UINT64_C(165), ((i)&low_mask(2)));

    {
      int count;
      count = 0;
      for (unsigned i = 0; i < 1024; ++i)
        if (active_codes[i])
          count++;
      CHECK(count == 460);
    }
  });
}
