// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

static auto &retirement(unsigned lane) {
  return *std::array{&retiring_0_in, &retiring_1_in, &retiring_2_in}[lane];
}

static void clear_writes() {
  for (unsigned i = 0; i < 3; ++i) retirement(i).pvalid = 0;
  completed_in.pvalid = 0;
}

static void readers() {
  candidates = {};
  for (unsigned lane = 0; lane < 3; ++lane) {
    auto &c = candidates[lane];
    c.pvalid = c.pready = c.puses_urs1 = 1;
    c.prs1 = lane + 1;
  }
}

int main() {
  return run_test([] {
    reset = 1;
    tick_model();
    reset = 0;
    normal_write_ports = 2;
    drained = 1;
    // RF initialization is software-visible writes, never a reset assumption.
    for (unsigned address = 1; address < 32; ++address) {
      retirement(0).pvalid = 1;
      retirement(0).pbits.paddress = address;
      retirement(0).pbits.pdata = 100 + address;
      tick_model();
    }
    clear_writes();
    readers();
    eval();
    CHECK(count == 3);
    for (unsigned lane = 0; lane < 3; ++lane) CHECK(operands[lane][0] == 101 + lane);

    // The floating read follows each possible two-source instruction.
    for (unsigned lane = 0; lane < 3; ++lane) {
      readers();
      candidates[lane].puses_urs2 = 1;
      candidates[lane].prs2 = 17;
      eval();
      CHECK(count == 3);
      CHECK(operands[lane][1] == 117);
    }
    readers();
    candidates[0].puses_urs2 = candidates[1].puses_urs2 = 1;
    candidates[0].prs2 = 17;
    candidates[1].prs2 = 18;
    eval();
    CHECK(count == 1); // No borrowing the unused dedicated third port.

    // Every stage/lane producer reaches every operand mux. Youngest wins;
    // an unavailable youngest match blocks rather than selecting an older result.
    for (unsigned lane = 0; lane < 3; ++lane)
      for (unsigned source = 0; source < 2; ++source)
        for (unsigned producer = 0; producer < 9; ++producer) {
          readers();
          producers = {};
          const unsigned address = source ? 17 : lane + 1;
          if (source) {
            candidates[lane].puses_urs2 = 1;
            candidates[lane].prs2 = address;
          }
          for (unsigned i = 0; i <= producer; ++i)
            producers[i] = {1, std::uint8_t(address), 1000 + i, 1};
          eval();
          CHECK(count == 3);
          CHECK(operands[lane][source] == 1000 + producer);
          producers[producer].pready = 0;
          eval();
          CHECK(count == lane);
        }

    producers = {};
    readers();
    pending = 1u << 2;
    eval();
    CHECK(count == 1);
    completed_in.pvalid = 1;
    completed_in.pbits = {2, 222};
    normal_write_ports = 1;
    eval();
    CHECK(count == 3);
    CHECK(operands[1][0] == 222);
    // Same-edge completion bypasses sources, but WAW waits for the scoreboard clear.
    candidates[0].pwrite = 1;
    candidates[0].pnormal_uwrite = 1;
    candidates[0].prd = 2;
    eval();
    CHECK(count == 0);
    tick_model();
    clear_writes();
    pending = 0;
    normal_write_ports = 2;

    // Retiring x5,x6,x5 coalesces only the storage write, never the input records.
    for (unsigned successful = 1; successful <= 3; ++successful) {
      candidates = {};
      for (unsigned i = 0; i < 3; ++i) {
        retirement(i).pvalid = i < successful;
        retirement(i).pbits = {std::uint8_t(i == 1 ? 6 : 5), 500 + i};
      }
      tick_model();
      clear_writes();
      readers();
      candidates[0].prs1 = 5;
      candidates[1].prs1 = 6;
      eval();
      CHECK(operands[0][0] == (successful == 3 ? 502 : 500));
      if (successful >= 2) CHECK(operands[1][0] == 501);
    }
    // x0 never consumes a read or stalls on a bogus zero owner.
    readers();
    candidates[0].prs1 = 0;
    producers[8] = {1, 0, 1234, 0};
    pending = 1;
    eval();
    CHECK(count == 3);
    CHECK(operands[0][0] == 0);
  });
}
