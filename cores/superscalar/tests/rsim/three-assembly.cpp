// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"

using Block = std::remove_cvref_t<decltype(blocks_in.pbits)>;
using Instruction = std::remove_cvref_t<decltype(instructions_out.pbits.pentries[0])>;
struct Expected { std::uint64_t pc; unsigned bytes; std::uint32_t raw; };
unsigned triples = 0;

void clear_assembly() {
  blocks_in = {};
  instructions_in.pready = false;
  clear_in.pvalid = true;
  eval();
  CHECK(!instructions_out.pvalid && !speculate_out.pvalid && !repair_out.pvalid);
  tick_model();
  clear_in.pvalid = false;
  eval();
  CHECK(available == 3);
}

unsigned seed(std::uint64_t pc) { return ((pc >> 3) ^ 0x155) & 1023; }

Block block(std::uint64_t pc, std::uint64_t data) {
  Block result{};
  result.ppc = pc;
  result.pdata = data;
  unsigned history = ((seed(pc) << 1) | 1) & 1023;
  for (unsigned i = 0; i < 4; ++i) {
    auto &direction = result.pdirections[i];
    direction.pvalid = i == 1 || i == 2;
    direction.ptaken = i != 1;
    direction.pindex = (0x180 | i);
    direction.pbase_uindex = ((pc & ~UINT64_C(7)) / 2 + i) & 4095;
    direction.phistory = history;
    if (direction.pvalid) history = ((history << 1) | direction.ptaken) & 1023;
    direction.pbase_utaken = i & 1;
    direction.pgshare_utaken = !(i & 1);
  }
  result.pprefix_udirection = result.pdirections[3];
  result.pprefix_udirection.pvalid = true;
  result.pprefix_udirection.pindex = 0x2af;
  result.pprefix_udirection.pbase_uindex = ((pc & ~UINT64_C(7)) / 2 - 1) & 4095;
  result.pprefix_udirection.phistory = seed(pc);
  return result;
}

void enqueue(const Block &value) {
  blocks_in.pvalid = true;
  blocks_in.pbits = value;
  eval();
  CHECK(blocks_out.pready);
  tick_model();
  blocks_in = {};
  eval();
}

void check_token(const Instruction &actual, const Expected &expected) {
  CHECK(actual.ppc == expected.pc);
  CHECK(actual.praw_uinstruction == expected.raw);
  CHECK(actual.pinstruction == 0x13); // C.NOP and ADDI x0,x0,0.
  CHECK(actual.psequential_upc == expected.pc + expected.bytes);
  CHECK(!actual.pcompressed_uillegal && !actual.pfault.pvalid);
  CHECK(!actual.pprediction.pvalid);
  CHECK(actual.pdirection.pindex % 4 == (expected.pc / 2) % 4);
  CHECK(actual.pdirection.pbase_uindex == (expected.pc / 2) % 4096);
  const bool straddles = expected.pc % 8 == 6 && expected.bytes == 4;
  const auto parcel = (expected.pc / 2) % 4;
  const auto initial = seed(expected.pc);
  const unsigned checkpoints[] = {(initial << 1) | 1, (initial << 1) | 1,
                                  (initial << 2) | 2, (initial << 3) | 5};
  CHECK(bool(actual.pdirection.pvalid) == (straddles || parcel == 1 || parcel == 2));
  CHECK(actual.pdirection.phistory == (straddles ? seed(expected.pc + 2) : checkpoints[parcel] & 1023));
}

void parcel_streams() {
  for (auto base : {UINT64_C(0x1000), UINT64_C(0xffffffc000001ff8), UINT64_MAX - 15}) {
    for (unsigned start = 0; start < 4; ++start) {
      for (unsigned lengths = 0; lengths < 8; ++lengths) {
        clear_assembly();
        std::array<std::uint16_t, 12> parcels;
        parcels.fill(1);
        std::deque<Expected> expected;
        unsigned position = start;
        for (unsigned lane = 0; lane < 3; ++lane) {
          const unsigned size = lengths & (1 << lane) ? 2 : 1;
          parcels[position] = size == 2 ? 0x13 : 1;
          if (size == 2) parcels[position + 1] = 0;
          expected.push_back({base + 2 * position, size * 2, parcels[position]});
          position += size;
        }
        while (position < parcels.size()) {
          expected.push_back({base + 2 * position, 2, 1});
          ++position;
        }
        for (unsigned b = 0; b < 3; ++b) {
          std::uint64_t data = 0;
          for (unsigned p = 0; p < 4; ++p) data |= std::uint64_t(parcels[b * 4 + p]) << (16 * p);
          enqueue(block(base + (b ? b * 8 : start * 2), data));
        }
        CHECK(!blocks_out.pready && available == 0);
        while (!expected.empty()) {
          eval();
          CHECK(instructions_out.pvalid);
          unsigned count = 0, bytes = expected.front().pc % 8;
          while (count < 3 && count < expected.size() && bytes + expected[count].bytes <= 16) {
            bytes += expected[count].bytes;
            ++count;
          }
          CHECK(instructions_out.pbits.pcount == count);
          triples += count == 3;
          for (unsigned held = 0; held < 3; ++held) {
            CHECK(!repair_out.pvalid && !discover_out.pvalid && !speculate_out.pvalid);
            for (unsigned lane = 0; lane < count; ++lane) check_token(instructions_out.pbits.pentries[lane], expected[lane]);
            tick_model();
          }
          instructions_in.pready = true;
          tick_model();
          instructions_in.pready = false;
          for (unsigned lane = 0; lane < count; ++lane) expected.pop_front();
        }
        eval();
        CHECK(!instructions_out.pvalid && available == 3);
      }
    }
  }
  CHECK(triples > 100);
}

void third_lane_control() {
  // A cold or stale prediction on the third instruction repairs only after acceptance.
  for (unsigned prediction = 0; prediction < 3; ++prediction) {
    clear_assembly();
    const std::uint64_t base = 0x2000 + prediction * 0x100;
    const std::uint32_t raw = prediction == 2 ? 0x13 : 0x040000ef; // JAL x1,+64.
    auto value = block(base, (std::uint64_t(raw) << 32) | 0x00010001);
    if (prediction) value.pprediction = {true, base + 4, base + 0x80, false, 0};
    enqueue(value);
    CHECK(instructions_out.pvalid && instructions_out.pbits.pcount == 3);
    const auto &third = instructions_out.pbits.pentries[2];
    CHECK(third.ppc == base + 4 && third.praw_uinstruction == raw);
    for (unsigned hold = 0; hold < 4; ++hold) {
      CHECK(!repair_out.pvalid && !discover_out.pvalid && !speculate_out.pvalid);
      tick_model();
    }
    instructions_in.pready = true;
    eval();
    CHECK(repair_out.pvalid && repair_out.pbits.pinvalidate == bool(prediction));
    CHECK(repair_out.pbits.ptarget == base + (prediction == 2 ? 8 : 68));
    CHECK(discover_out.pvalid == (prediction == 0));
    CHECK(speculate_out.pvalid == (prediction != 2));
    if (prediction != 2) CHECK(speculate_out.pbits.preturn_uaddress == base + 8);
    tick_model();
    CHECK(!instructions_out.pvalid); // Repair discards only the following suffix.
    tick_model();
    CHECK(available == 3);
  }

  // A third-position branch cuts the suffix without changing its valid prediction.
  clear_assembly();
  auto value = block(0x3000, (UINT64_C(0x0400006f) << 32) | 0x00010001);
  value.pprediction = {true, 0x3004, 0x3044, false, 0};
  enqueue(value);
  enqueue(block(0x3008, 0x0001000100010001));
  instructions_in.pready = true;
  eval();
  CHECK(instructions_out.pbits.pcount == 3 && !repair_out.pvalid);
  CHECK(instructions_out.pbits.pentries[2].pprediction.ptarget == 0x3044);
  tick_model();

  // Two unknown indirect calls cannot both update the single RAS port.
  clear_assembly();
  enqueue(block(0x4000, (UINT64_C(0x00e7) << 48) | (UINT64_C(1) << 32) | 0x000100e7));
  enqueue(block(0x4008, 0x0001000100010001));
  CHECK(instructions_out.pbits.pcount == 2); // JALR x1,x2,0; C.NOP; straddling JALR.
  instructions_in.pready = true;
  eval();
  CHECK(speculate_out.pvalid && speculate_out.pbits.preturn_uaddress == 0x4004);
  tick_model();
  instructions_in.pready = false;
  eval();
  CHECK(instructions_out.pvalid && instructions_out.pbits.pentries[0].ppc == 0x4006);
}

void stalled_history_bypass() {
  clear_assembly();
  constexpr std::uint64_t pc = 0x735a0;
  blocks_in.pbits = block(pc, UINT64_C(0x0001000100010001));
  blocks_in.pvalid = true;
  eval();
  CHECK(blocks_out.pready && instructions_out.pvalid && instructions_out.pbits.pcount == 3);
  const auto bypass = instructions_out.pbits;
  tick_model();
  blocks_in.pvalid = false;
  for (unsigned stall = 0; stall < 3; ++stall) {
    blocks_in.pbits = block(pc + (stall + 1) * 8, 0);
    eval();
    CHECK(instructions_out.pvalid && instructions_out.pbits.pcount == bypass.pcount);
    for (unsigned lane = 0; lane < 3; ++lane) {
      const auto &entry = instructions_out.pbits.pentries[lane];
      check_token(entry, {pc + lane * 2, 2, 1});
      CHECK(entry.pdirection.phistory == bypass.pentries[lane].pdirection.phistory);
    }
    tick_model();
  }
}

void continuation_faults() {
  for (unsigned cause : {0u, 1u, 12u, 20u}) {
    clear_assembly();
    enqueue(block(0x5ff8, (UINT64_C(0x0013) << 48) | (UINT64_C(1) << 32) | 0x13));
    CHECK(instructions_out.pvalid && instructions_out.pbits.pcount == 2);
    auto continuation = block(0x6000, 0);
    continuation.pfault.pvalid = 1;
    continuation.pfault.pcause = cause;
    continuation.pfault.pvalue = 0x6000;
    set_fetch_guest(continuation.pfault, true, 0x12345678, 1);
    enqueue(continuation);
    CHECK(instructions_out.pbits.pcount == 3);
    const auto &faulting = instructions_out.pbits.pentries[2];
    CHECK(faulting.ppc == 0x5ffe && parcel_fault_address(faulting) == 0x6000);
    CHECK(faulting.pfault.pvalid && faulting.pfault.pcause == cause && same_fetch_guest(faulting.pfault, continuation.pfault));
    CHECK(!instructions_out.pbits.pentries[0].pfault.pvalid && !instructions_out.pbits.pentries[1].pfault.pvalid);
    instructions_in.pready = true;
    eval();
    CHECK(!repair_out.pvalid && !discover_out.pvalid && !speculate_out.pvalid);
    tick_model();
    CHECK(!instructions_out.pvalid && available == 3);
  }
}

int main() {
  return run_test([] {
    reset = true;
    tick_model();
    reset = false;
    parcel_streams();
    stalled_history_bypass();
    third_lane_control();
    continuation_faults();
    // Clear must suppress both resident and live-bypass offers.
    clear_assembly();
    blocks_in.pvalid = true;
    blocks_in.pbits = block(0x7000, 0x0001000100010001);
    eval();
    CHECK(instructions_out.pvalid && instructions_out.pbits.pcount == 3);
    clear_in.pvalid = true;
    eval();
    CHECK(!instructions_out.pvalid);
    tick_model();
    clear_in.pvalid = false;
    blocks_in = {};
    eval();
    CHECK(!instructions_out.pvalid && available == 3);
  });
}
