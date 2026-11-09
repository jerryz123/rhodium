// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <random>

static std::array<std::uint64_t, 32> contents{};
static std::array<bool, 32> written{};
static std::array<std::uint64_t, 32> three_contents{};
static std::array<bool, 32> three_written{};
using word_t = std::remove_cvref_t<decltype(data[0])>;
static constexpr unsigned width = sizeof(word_t) * 8;

static void check_reads() {
  eval();
  for (unsigned port = 0; port < 4; ++port)
    if (!addresses[port] || written[addresses[port]])
      CHECK(data[port] == contents[addresses[port]]);
  for (unsigned port = 0; port < 4; ++port)
    if (!addresses[port] || three_written[addresses[port]])
      CHECK(three_write_data[port] == three_contents[addresses[port]]);
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
  for (unsigned port = 0; port < 3; ++port)
    if (write_valid[port] && write_values[port].paddress) {
      three_contents[write_values[port].paddress] = write_values[port].pdata;
      three_written[write_values[port].paddress] = true;
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
    write_valid = {1, 1, 1};
    write_values[0] = {1, 11};
    write_values[1] = {2, 22};
    write_values[2] = {3, 33};
    addresses = {1, 2, 3, 0};
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

    // The first row of each eight-register group has the same low address bits.
    addresses = {0, 8, 16, 24};
    write_valid = {1, 1, 1};
    write_values[0] = {8, 81};
    write_values[1] = {16, 161};
    write_values[2] = {24, 241};
    step();
    write_values[0] = {0, word_t(low_mask(width))};
    write_values[1] = {24, 242};
    write_values[2] = {0, 1};
    step();
    write_valid = {};
    for (unsigned rotation = 0; rotation < 4; ++rotation) {
      std::rotate(addresses.begin(), addresses.begin() + 1, addresses.end());
      check_reads();
    }

    std::mt19937_64 random(0x4a2b);
    for (unsigned cycle = 0; cycle < 3000; ++cycle) {
      for (auto &address : addresses) address = random() % 32;
      for (unsigned port = 0; port < 3; ++port) {
        write_valid[port] = random() & 1;
        write_values[port].paddress = random() % 32;
        write_values[port].pdata = random() & low_mask(width);
      }
      for (unsigned port = 0; port < 3; ++port)
        for (unsigned other = port + 1; other < 3; ++other)
          if (write_values[port].paddress == write_values[other].paddress)
            write_valid[other] = 0;
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
    for (unsigned port = 0; port < 3; ++port)
      for (unsigned other = port + 1; other < 3; ++other) {
        write_valid = {};
        write_valid[port] = write_valid[other] = 1;
        write_values[port] = {9, 99};
        write_values[other] = {9, 100};
        const auto label = "register_file_distinct_writes_" + std::to_string(port) + "_" + std::to_string(other);
        expect_failure(label.c_str(), [] { tick_model(); });
      }
  });
}
