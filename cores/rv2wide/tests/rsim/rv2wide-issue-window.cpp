// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
using Instruction = std::remove_cvref_t<decltype(ingress_in.pbits.pentries[0])>;
using Fault = decltype(Instruction{}.pfault);
template <class T> constexpr bool has_guest = requires(T t) { t.pguest; };
constexpr unsigned xlen = sizeof(Instruction{}.ppc) * 8;
std::deque<Instruction> expected;
unsigned sequence = 0;

Instruction token(unsigned id) {
  Instruction result{};
  const std::uint64_t pcs[] = {0xffe, UINT64_C(0xffffffc000001ffc),
                              UINT64_C(0x00000ffffffffffc), UINT64_MAX - 1};
  const bool fault = id % 3 == 1;
  result.ppc = fault ? UINT64_C(0x123456789abc0000) + id * 2 : pcs[id % 4];
  if (fault && id % 4 >= 2) result.ppc = UINT64_MAX - 1;
  result.pinstruction = 0x13 + (id << 7);
  result.praw_uinstruction = id & 1 ? 1 : result.pinstruction;
  result.psequential_upc = result.ppc + (id & 1 ? 2 : 4);
  result.pcompressed_uillegal = id & 1;
  result.pfault.pkind = fault ? 1 + (id / 3) % (has_guest<Fault> ? 4 : 3) : 0;
  // Fault addresses need not be the PC or PC+2, including a straddling wrap.
  result.pfault.pvalue = id & 1 ? result.ppc + 2 : UINT64_C(0xfedcba9876543210) + id;
  set_fetch_guest(result.pfault, true, UINT64_C(0x123456780) + id, id % 3);
  result.pprediction.pvalid = !fault && id % 4 != 0;
  result.pprediction.ppc = result.pprediction.pvalid ? result.ppc : 0xdead;
  result.pprediction.ptarget = UINT64_C(0x1234fedcba987654) ^ id;
  result.pprediction.pcompressed = id & 1;
  result.pprediction.pras_uaction = id % 4;
  result.pspeculated_uras_uaction = (id + 1) % 4;
  result.pdirection.pvalid = id & 1;
  result.pdirection.pindex = (((id * 37 + 0x155) & 1023) << 2) | ((result.ppc >> 1) & 3);
  result.pdirection.phistory = (id * 13 + 0x2aa) & 1023;
  result.pdirection.ptaken = id & 1;
  result.pdirection.pbase_uindex = (result.ppc >> 1) & 4095;
  result.pdirection.pbase_utaken = (id >> 1) & 1;
  result.pdirection.pgshare_utaken = (id >> 2) & 1;
  if (!result.pdirection.pvalid) {
    result.pdirection.pindex ^= 3;
    result.pdirection.pbase_uindex ^= 0x5a5;
  }
  return result;
}

void check_entry(const Instruction &actual, const Instruction &want) {
  CHECK(same_instruction(actual, want));
  CHECK(actual.pprediction.ptarget == (want.pprediction.pvalid ? want.pprediction.ptarget : 0));
  CHECK(!want.pdirection.pvalid || actual.pdirection.pbase_uindex == want.pdirection.pbase_uindex);
  CHECK(actual.pdirection.pbase_utaken == want.pdirection.pbase_utaken);
  CHECK(actual.pdirection.pgshare_utaken == want.pdirection.pgshare_utaken);
}

void cycle(unsigned remove, unsigned append, bool clear = false) {
  consume = remove;
  clear_in.pvalid = clear;
  ingress_in.pvalid = append != 0;
  ingress_in.pbits.pcount = append ? append : 1;
  for (unsigned lane = 0; lane < 2; ++lane)
    ingress_in.pbits.pentries[lane] = token(sequence + lane);
  eval();
  CHECK(available == 8 - expected.size());
  CHECK(head.pcount == std::min<std::size_t>(2, expected.size()));
  CHECK(bool(ingress_out.pready) == (!clear && expected.size() - remove <= 6));
  for (unsigned lane = 0; lane < 2; ++lane) {
    const auto &candidate = lane ? candidates_1_out : candidates_0_out;
    CHECK(bool(candidate.pvalid) == (!clear && lane < expected.size()));
    if (lane < expected.size()) {
      check_entry(head.pentries[lane], expected[lane]);
      check_entry(candidate.pbits, expected[lane]);
    }
  }
  if (clear) {
    expected.clear();
  } else {
    for (unsigned lane = 0; lane < remove; ++lane)
      expected.pop_front();
    if (append && ingress_out.pready) {
      for (unsigned lane = 0; lane < append; ++lane)
        expected.push_back(ingress_in.pbits.pentries[lane]);
      sequence += append;
    }
  }
  tick_model();
}

int main() {
  return run_test([] {
    reset = 1;
    ingress_in = {};
    clear_in = {};
    consume = 0;
    eval();
    tick_model();
    reset = 0;
    // Fill all eight entries, hold the heads, then append while consuming a prefix.
    for (unsigned i = 0; i < 4; ++i) cycle(0, 2);
    for (unsigned i = 0; i < 12; ++i) cycle(0, 2);
    cycle(1, 2);
    cycle(2, 2);
    while (!expected.empty()) cycle(std::min<std::size_t>(2, expected.size()), 0);
    unsigned random = 0x43a09b1;
    for (unsigned i = 0; i < 1500; ++i) {
      random = random * 1664525 + 1013904223;
      const unsigned remove = std::min<std::size_t>((random >> 16) % 3, expected.size());
      cycle(remove, (random >> 24) % 3, i % 97 == 96);
    }
    while (!expected.empty()) cycle(1, 0);
    cycle(0, 0);
    // Reject inconsistent valid indices; arbitrary invalid index bits remain irrelevant.
    for (unsigned mutation = 0; mutation < 2; ++mutation) {
      auto invalid = token(3);
      if (mutation == 0) invalid.pdirection.pindex ^= 1;
      else invalid.pdirection.pbase_uindex ^= 0x100;
      ingress_in.pbits.pentries[0] = invalid;
      ingress_in.pbits.pcount = 1;
      ingress_in.pvalid = 1;
      expect_failure("rv2wide_window_direction_indices", [] { eval(); tick_model(); });
      ingress_in = {};
      reset = 1;
      tick_model();
      reset = 0;
    }

    auto invalid_prediction = token(1);
    invalid_prediction.pprediction.pvalid = 1;
    invalid_prediction.pprediction.ppc = invalid_prediction.ppc;
    ingress_in.pbits.pentries[0] = invalid_prediction;
    ingress_in.pbits.pcount = 1;
    ingress_in.pvalid = 1;
    expect_failure("rv2wide_fault_does_not_predict", [] { eval(); tick_model(); });
    if constexpr (xlen == 64) {
      // A compact successful PC must be representable; faults use the escape bits.
      auto invalid = token(0);
      invalid.ppc = UINT64_C(0x123456789abc0000);
      invalid.psequential_upc = invalid.ppc + 4;
      ingress_in.pbits.pentries[0] = invalid;
      ingress_in.pbits.pcount = 1;
      ingress_in.pvalid = 1;
      expect_failure("rv2wide_window_success_pc_fits", [] { eval(); tick_model(); });
    }
  });
}
