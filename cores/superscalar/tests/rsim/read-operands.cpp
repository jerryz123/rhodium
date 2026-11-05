// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <array>
#include <cstdint>
#include <tuple>

using RequestArray = std::remove_cvref_t<decltype(requests)>;
using Word = std::remove_cvref_t<decltype(writes_0_in.pbits.pdata)>;
constexpr unsigned lanes = std::tuple_size_v<RequestArray>;
std::array<Word, 32> values{};
std::uint64_t rng = 0xb6d1843f219ac507;

std::uint64_t random_value() {
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return rng;
}

bool needed(unsigned lane, unsigned operand) {
  return requests[lane].pvalid && requests[lane].pused[operand] &&
         requests[lane].paddresses[operand] != 0;
}

void check() {
  eval();
  std::array<unsigned, 4> addresses{requests[0].paddresses[0], requests[0].paddresses[1],
                                     requests[1].paddresses[0], requests[1].paddresses[1]};
  if constexpr (lanes == 3) {
    if (!needed(1, 1) && needed(2, 0)) addresses[3] = requests[2].paddresses[0];
    if (!needed(0, 1) && needed(2, 1)) addresses[1] = requests[2].paddresses[1];
  }
  for (unsigned port = 0; port < 4; ++port) CHECK(routing.paddresses[port] == addresses[port]);
  for (unsigned lane = 0; lane < lanes; ++lane) {
    for (unsigned operand = 0; operand < 2; ++operand) {
      const bool demand = needed(lane, operand);
      const bool grant = lane < 2 || !demand || !needed(operand == 0 ? 1 : 0, 1);
      CHECK(bool(routing.pneeded[lane][operand]) == demand);
      CHECK(bool(routing.pgranted[lane][operand]) == grant);
      Word want = 0;
      bool blocked = demand && !grant;
      if (demand && grant) {
        const unsigned address = requests[lane].paddresses[operand];
        want = values[address];
        // Search youngest first, independently of the RTL's overwrite fold.
        for (int index = int(producers.size()) - 1; index >= 0; --index) {
          const auto &producer = producers[index];
          if (producer.pvalid && producer.pwrite && producer.paddress == address) {
            want = producer.pdata;
            blocked = !producer.pready || !producer.pcontinuing;
            break;
          }
        }
        blocked |= (pending >> address) & 1;
      }
      CHECK(operands[lane][operand].pdata == want);
      CHECK(bool(operands[lane][operand].pblocked) == blocked);
    }
  }
}

void tick() {
  check(); // RF reads remain pre-edge, even with a write on this edge.
  tick_model();
  for (const auto &write : {writes_0_in, writes_1_in})
    if (write.pvalid && write.pbits.paddress) values[write.pbits.paddress] = write.pbits.pdata;
  check();
}

void clear_sources() {
  pending = 0;
  producers = {};
  writes_0_in = {};
  writes_1_in = {};
}

int main() {
  return run_test([] {
    reset = 1;
    tick_model();
    reset = 0;
    // Initialize real, unreset storage before any nonzero architectural read.
    for (unsigned address = 1; address < 32; address += 2) {
      writes_0_in = {1, {std::uint8_t(address), Word(0xfedcba9876543210ULL * address)}};
      writes_1_in = {1, {std::uint8_t((address + 1) % 32), Word(0x123456789abcdef0ULL * address)}};
      tick();
    }
    clear_sources();

    // Exhaust demand, x0, and validity combinations; third-lane ownership must
    // not depend on data availability, producer matches, or the other spare path.
    for (unsigned valid = 0; valid < (1u << lanes); ++valid) {
      for (unsigned uses = 0; uses < (1u << (2 * lanes)); ++uses) {
        for (unsigned zeros = 0; zeros < (1u << (2 * lanes)); ++zeros) {
          for (unsigned lane = 0; lane < lanes; ++lane) {
            requests[lane].pvalid = (valid >> lane) & 1;
            for (unsigned operand = 0; operand < 2; ++operand) {
              const unsigned bit = 2 * lane + operand;
              requests[lane].pused[operand] = (uses >> bit) & 1;
              requests[lane].paddresses[operand] = zeros & (1u << bit) ? 0 : bit + 1;
            }
          }
          check();
        }
      }
    }

    for (auto &request : requests) {
      request.pvalid = true;
      request.pused = {true, true};
      request.paddresses = {7, 11};
    }
    if constexpr (lanes == 3) {
      requests[2].paddresses = {19, 23};
      // An available older bypass still owns its donor path.
      producers[0] = {1, 1, 11, 0x111, 1, 1};
      check();
      CHECK(operands[2][0].pblocked && operands[2][1].pblocked);
      // An unrelated spare rs1 path cannot satisfy either borrower.
      requests[0].pused[0] = false;
      requests[1].pused[0] = false;
      check();
      CHECK(operands[2][0].pblocked && operands[2][1].pblocked);
      // Even matching addresses do not co-read an occupied donor in this cut.
      requests[2].paddresses = {11, 11};
      check();
      CHECK(operands[2][0].pblocked && operands[2][1].pblocked);
      requests[2].paddresses = {19, 23};
      requests[0].pused[1] = false;
      check();
      CHECK(operands[2][0].pblocked && !operands[2][1].pblocked);
      requests[0].pused[1] = true;
      requests[1].pused[1] = false;
      check();
      CHECK(!operands[2][0].pblocked && operands[2][1].pblocked);
      requests[0].paddresses[1] = 0;
    }

    // Each physical producer position can supply or block a borrowed operand.
    const unsigned consumer = lanes - 1;
    for (unsigned operand = 0; operand < 2; ++operand) {
      const unsigned address = requests[consumer].paddresses[operand];
      for (unsigned owner = 0; owner < producers.size(); ++owner) {
        for (unsigned state = 0; state < 4; ++state) {
          clear_sources();
          producers[owner] = {1, 1, std::uint8_t(address), Word(0xffff000000000000ULL | owner),
                              std::uint8_t(state & 1), std::uint8_t((state >> 1) & 1)};
          check();
        }
      }
      // Youngest unavailable/replaying writes block older ready values.
      for (unsigned older = 0; older < producers.size(); ++older) {
        for (unsigned younger = older + 1; younger < producers.size(); ++younger) {
          clear_sources();
          producers[older] = {1, 1, std::uint8_t(address), 0x111, 1, 1};
          producers[younger] = {1, 1, std::uint8_t(address), 0x222, 0, 1};
          check();
          CHECK(operands[consumer][operand].pblocked);
          producers[younger].pready = 1;
          check();
          CHECK(!operands[consumer][operand].pblocked && operands[consumer][operand].pdata == 0x222);
          producers[younger].pcontinuing = 0;
          check();
          producers[younger].pwrite = 0;
          check();
          CHECK(!operands[consumer][operand].pblocked && operands[consumer][operand].pdata == 0x111);
        }
      }
    }

    // Mix writes, scoreboards, producer priority, and changing path ownership.
    for (unsigned iteration = 0; iteration < 4000; ++iteration) {
      clear_sources();
      for (auto &request : requests) {
        request.pvalid = (random_value() & 3) != 0;
        for (unsigned operand = 0; operand < 2; ++operand) {
          request.pused[operand] = random_value() & 1;
          request.paddresses[operand] = random_value() % 32;
        }
      }
      for (auto &producer : producers) {
        const auto flags = random_value();
        producer = {std::uint8_t(flags & 1), std::uint8_t((flags >> 1) & 1),
                    std::uint8_t(random_value() % 32), Word(random_value()),
                    std::uint8_t((flags >> 2) & 1), std::uint8_t((flags >> 3) & 1)};
      }
      pending = random_value();
      const unsigned address = random_value() % 32;
      writes_0_in = {1, {std::uint8_t(address), Word(random_value())}};
      writes_1_in = {1, {std::uint8_t((address + 1) % 32), Word(random_value())}};
      tick();
    }
    clear_sources();
    check();
    reset = 1;
    tick_model();
    reset = 0;
    check(); // Operand routing adds no resettable RF storage.
  });
}
