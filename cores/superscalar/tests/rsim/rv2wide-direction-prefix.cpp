// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#ifndef PREFIX_WIDTH
#define PREFIX_WIDTH 39
#endif
using Address = std::remove_cvref_t<decltype(prediction.ppc)>;
constexpr unsigned width = PREFIX_WIDTH;
constexpr std::uint32_t indirect = 0x00010067; // JALR x0,x2,0: no RAS action.
constexpr std::uint32_t conditional = 0x00000463; // BEQ x0,x0,+8.

Address normalized(std::uint64_t value) {
  if constexpr (width < 64) {
    const auto sign = UINT64_C(1) << (width - 1);
    value &= (UINT64_C(1) << width) - 1;
    value = (value ^ sign) - sign;
  }
  return Address(value);
}

void cancel() {
  valid = false;
  clear = true;
  tick_model();
  clear = false;
  fault = false;
  prediction = {};
}

void offer_prefix(Address address, std::uint32_t instruction, Address target, unsigned stale = 0) {
  pc = address;
  data = (std::uint64_t(instruction & 0xffff) << 48) | UINT64_C(0x000100010001);
  row = 0x155;
  history = 0x2aa;
  base_taken[3] = true;
  prediction = {true, Address(address - (stale == 1 ? 2 : 0)), target, stale == 2, 0};
  valid = true;
  eval();
  CHECK(bool(corrected.pvalid) == (stale == 0));
}

void continuation(Address address, std::uint32_t instruction) {
  valid = false;
  pc = Address(address + 2);
  data = UINT64_C(0x0001000100010000) | (instruction >> 16);
  prediction = {};
  row = 0x2a6; // The continuation's row must not replace the prefix's saved lookup.
  history = 0x123;
  prefix_gshare_taken = false;
  prefix_use_gshare = false;
  eval();
}

void check_indirect(Address address, Address target, bool predicted = true) {
  eval();
  CHECK(bool(corrected.pvalid) == predicted);
  CHECK(!invalidate);
  if (predicted) {
    CHECK(normalized(corrected.ppc) == address && normalized(corrected.ptarget) == target &&
          !corrected.pcompressed && corrected.pras_uaction == 0);
    CHECK(next_pc == target);
  }
}

int main() {
  return run_test([] {
    valid = clear = fault = false;
    prediction = {};
    reset = true;
    tick_model();
    reset = false;
    const std::uint64_t addresses[] = {
      0x1006, 0x1ffe, UINT64_C(0x0000003ffffffffe),
      UINT64_C(0xffffffc012345ffe), UINT64_MAX - 1,
      UINT64_C(0x123456789abc0006)
    };
    for (auto raw_address : addresses) {
      const Address address = normalized(raw_address);
      const Address target = normalized(UINT64_C(0xfedcba9876543210));
      for (unsigned stale = 0; stale < 3; ++stale) {
        cancel();
        offer_prefix(address, indirect, target, stale);
        tick_model();
        // Missing/replayed responses do not replace the retained owner.
        valid = false;
        for (unsigned bubble = 0; bubble < 3; ++bubble) {
          pc = Address(~address);
          prediction = {};
          data = 0;
          row = bubble;
          tick_model();
        }
        continuation(address, indirect);
        CHECK(prefix_row == 0x155 && prefix_choice_row == ((address >> 3) & 1023));
        CHECK(prefix_direction.pindex == 0x557 && prefix_direction.phistory == 0x123 &&
              prefix_direction.pbase_uindex == ((address >> 1) & 4095));
        check_indirect(address, target, stale == 0);
        constexpr unsigned alias_bit = width == 32 ? 16 : width < 64 ? width : 20;
        pc = std::uint64_t(Address(address + 2)) ^ (UINT64_C(1) << alias_bit);
        eval();
        CHECK(!corrected.pvalid); // Full-width continuity, not a truncated-PC comparison.
        continuation(address, indirect);
        check_indirect(address, target, stale == 0);

        fault = true;
        valid = true;
        eval();
        CHECK(!corrected.pvalid && !prefix_direction.pvalid);
        tick_model();
        fault = false;
        valid = false;
        eval();
        CHECK(!corrected.pvalid); // The fault consumed and canceled this prefix.
      }

      cancel();
      const Address actual = normalized(std::uint64_t(address) + 8);
      offer_prefix(address, conditional, Address(actual ^ 0x100));
      tick_model();
      continuation(address, conditional);
      CHECK(corrected.pvalid && normalized(corrected.ppc) == address && normalized(corrected.ptarget) == actual);
      CHECK(invalidate && prefix_direction.pvalid && prefix_direction.pbase_utaken &&
            !prefix_direction.pgshare_utaken && prefix_direction.ptaken);
      CHECK(prefix_direction.pindex == 0x557 && prefix_direction.phistory == 0x123 &&
            history_after == 0x247);

      // Clear beats a simultaneous new prefix capture.
      offer_prefix(address, indirect, target);
      clear = true;
      tick_model();
      clear = false;
      continuation(address, indirect);
      CHECK(!corrected.pvalid);
    }

    if constexpr (width == 39) {
      cancel();
      expect_failure("rv2wide_direction_prefix_pc_fits", [] {
        pc = UINT64_C(0x123456789abc0006);
        data = (std::uint64_t(indirect & 0xffff) << 48) | UINT64_C(0x000100010001);
        prediction = {};
        valid = true;
        tick_model();
      });
      cancel();

      cancel();
    }
  });
}
