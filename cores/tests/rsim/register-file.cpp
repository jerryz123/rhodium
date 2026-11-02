// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <random>

static std::array<std::uint64_t, 32> contents{};
static std::array<bool, 32> written{};
using word_t = std::remove_cvref_t<decltype(data[0])>;
static constexpr unsigned width = sizeof(word_t) * 8;

static void check_reads() {
  eval();
  for (unsigned port = 0; port < 4; ++port)
    if (!addresses[port] || written[addresses[port]])
      CHECK(data[port] == contents[addresses[port]]);
  for (unsigned port = 0; port < 2; ++port)
    if (!addresses[port] || written[addresses[port]])
      CHECK(two_read_data[port] == contents[addresses[port]]);
}

static void step() {
  check_reads(); // Live writes must not bypass stored values inside the RF.
  for (unsigned port = 0; port < 2; ++port)
    if (write_valid[port] && write_values[port].paddress) {
      contents[write_values[port].paddress] = write_values[port].pdata;
      written[write_values[port].paddress] = true;
    }
  tick_model();
  check_reads();
}

int main() {
  return run_test([] {
    reset = 1;
    write_valid = {};
    addresses = {0, 1, 2, 3};
    tick_model();
    reset = 0;
    check_reads();

    // Independent writes, read-during-write, repeated reads, and x0.
    write_valid = {1, 1};
    write_values[0] = {1, 11};
    write_values[1] = {2, 22};
    addresses = {1, 2, 1, 2};
    step();
    write_values[0] = {1, 33};
    write_values[1] = {2, 44};
    step();
    write_valid = {0, 1};
    write_values[0] = {1, 55};
    write_values[1] = {2, 66};
    step();
    write_valid = {1, 0};
    step();
    write_valid = {1, 1};
    write_values[0] = {0, word_t(low_mask(width))};
    write_values[1] = {0, word_t(low_mask(width))};
    addresses.fill(0);
    step();

    std::mt19937_64 random(0x4a2b);
    for (unsigned cycle = 0; cycle < 3000; ++cycle) {
      for (auto &address : addresses) address = random() % 32;
      for (unsigned port = 0; port < 2; ++port) {
        write_valid[port] = random() & 1;
        write_values[port].paddress = random() % 32;
        write_values[port].pdata = random() & low_mask(width);
      }
      if (write_values[0].paddress == write_values[1].paddress)
        write_valid[1] = 0;
      step();
    }

    // Seed known values, then check retention across reset with writes idle.
    write_valid = {1, 1};
    write_values[0] = {1, 101};
    write_values[1] = {2, 202};
    step();
    write_valid = {};
    reset = 1;
    tick_model();
    check_reads();
    reset = 0;
    for (unsigned address = 0; address < 32; ++address) {
      addresses.fill(address);
      check_reads();
    }

    // Nonzero write collisions are invalid, not an implicit priority.
    write_valid = {1, 1};
    write_values[0] = {9, 99};
    write_values[1] = {9, 100};
    expect_failure("register_file_distinct_writes", [] { tick_model(); });
  });
}
