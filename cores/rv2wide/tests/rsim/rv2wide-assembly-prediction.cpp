// Checks assembly credits, cross-block prediction ownership, repairs, and compressed expansion.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
using Direction = std::remove_cvref_t<decltype(blocks_in.pbits.pdirections[0])>;
using Packet = std::remove_cvref_t<decltype(instructions_out.pbits)>;
Queue<std::uint64_t> expected_pc;
int repairs = 0, speculations = 0, retired = 0, return_speculations = 0;
void drive() {}
void falling_update() {}
bool same_packet(const Packet &a, const Packet &b) {
  if (a.pcount != b.pcount)
    return false;
  for (unsigned i = 0; i < 2; ++i) {
    const auto &x = a.pentries[i];
    const auto &y = b.pentries[i];
    if (x.ppc != y.ppc || x.pinstruction != y.pinstruction ||
        x.praw_uinstruction != y.praw_uinstruction ||
        x.psequential_upc != y.psequential_upc ||
        x.pcompressed_uillegal != y.pcompressed_uillegal ||
        x.pfault.pvalid != y.pfault.pvalid ||
        x.pfault.pcause != y.pfault.pcause ||
        x.pfault.pvalue != y.pfault.pvalue ||
        x.pprediction.pvalid != y.pprediction.pvalid ||
        x.pprediction.ppc != y.pprediction.ppc ||
        x.pprediction.ptarget != y.pprediction.ptarget ||
        x.pprediction.pcompressed != y.pprediction.pcompressed ||
        x.pprediction.pras_uaction != y.pprediction.pras_uaction ||
        x.pspeculated_uras_uaction != y.pspeculated_uras_uaction ||
        x.pdirection.pvalid != y.pdirection.pvalid ||
        x.pdirection.pindex != y.pdirection.pindex ||
        x.pdirection.phistory != y.pdirection.phistory ||
        x.pdirection.ptaken != y.pdirection.ptaken)
      return false;
  }
  return true;
}
void observe() {
  if (reset)
    return;
  if (instructions_out.pvalid && instructions_in.pready)
    for (int lane = 0; lane < int(instructions_out.pbits.pcount); lane++) {
      std::uint64_t want;
      CHECK(expected_pc.size() > 0);
      want = expected_pc.take();
      CHECK(instructions_out.pbits.pentries[lane].ppc == want);
      if (want == UINT64_C(0x706))
        CHECK(instructions_out.pbits.pentries[lane].pfault.pvalid &&
              instructions_out.pbits.pentries[lane].pfault.pvalue ==
                  UINT64_C(0x708));
      if (want == UINT64_C(0x406))
        CHECK(instructions_out.pbits.pentries[lane].pprediction.pvalid &&
              instructions_out.pbits.pentries[lane].pprediction.ptarget ==
                  UINT64_C(0x500) &&
              instructions_out.pbits.pcount == 1);
      if (want == UINT64_C(0x806))
        CHECK(instructions_out.pbits.pentries[lane].pinstruction ==
                  UINT64_C(0xfa0006f) &&
              instructions_out.pbits.pentries[lane].pprediction.pvalid &&
              instructions_out.pbits.pentries[lane].pprediction.ptarget ==
                  UINT64_C(0x900) &&
              instructions_out.pbits.pcount == 1);
      if (want == UINT64_C(0xb00) || want == UINT64_C(0xb80) ||
          want == UINT64_C(0xc00) || want == UINT64_C(0xc40) ||
          want == UINT64_C(0xc80)) {
        std::uint64_t target;
        switch (want) {
        case UINT64_C(0xb00): {
          target = UINT64_C(0xb40);
        } break;
        case UINT64_C(0xb80): {
          target = UINT64_C(0xba0);
        } break;
        case UINT64_C(0xc00): {
          target = UINT64_C(0xc20);
        } break;
        default: {
          target = want;
        } break;
        }
        CHECK(instructions_out.pbits.pentries[lane].pprediction.pvalid &&
              instructions_out.pbits.pentries[lane].pprediction.ptarget ==
                  target &&
              instructions_out.pbits.pcount == 1);
        CHECK(repair_out.pvalid && repair_out.pbits.ptarget == target &&
              repair_out.pbits.pinvalidate == (want != UINT64_C(0xb00)));
      }
      if (want == UINT64_C(0xd00))
        CHECK(!instructions_out.pbits.pentries[lane].pprediction.pvalid &&
              !repair_out.pvalid);
      if (want == UINT64_C(0xe00) || want == UINT64_C(0xe40) ||
          want == UINT64_C(0xe86) || want == UINT64_C(0xec0) ||
          want == UINT64_C(0xf00) || want == UINT64_C(0xf40)) {
        std::uint64_t target;
        bool correction;
        target = want == UINT64_C(0xec0) ? UINT64_C(0xdead) : UINT64_C(0x1234);
        correction = want != UINT64_C(0xec0) && want != UINT64_C(0xf00);
        CHECK(instructions_out.pbits.pentries[lane].pprediction.pvalid &&
              instructions_out.pbits.pentries[lane].pprediction.ptarget ==
                  target &&
              instructions_out.pbits.pcount == 1);
        CHECK(repair_out.pvalid == correction);
        if (correction)
          CHECK(repair_out.pbits.ptarget == target &&
                !repair_out.pbits.pinvalidate);
      }
      if (want >= UINT64_C(0xa00) && want < UINT64_C(0xa08)) {
        std::uint32_t canonical;
        std::uint16_t raw;
        switch (want) {
        case UINT64_C(0xa00): {
          {
            raw = UINT64_C(0x2000);
            canonical = UINT64_C(0x43407);
          } // C.FLD f8,0(x8)
        } break;
        case UINT64_C(0xa02): {
          {
            raw = UINT64_C(0xa004);
            canonical = UINT64_C(0x943027);
          } // C.FSD f9,0(x8)
        } break;
        case UINT64_C(0xa04): {
          {
            raw = UINT64_C(0x2002);
            canonical = UINT64_C(0x13007);
          } // C.FLDSP f0,0(sp)
        } break;
        default: {
          {
            raw = UINT64_C(0xa006);
            canonical = UINT64_C(0x113027);
          } // C.FSDSP f1,0(sp)
        } break;
        }
        CHECK(!instructions_out.pbits.pentries[lane].pcompressed_uillegal &&
              instructions_out.pbits.pentries[lane].pinstruction == canonical &&
              instructions_out.pbits.pentries[lane].praw_uinstruction ==
                  ((raw)&low_mask(32)) &&
              instructions_out.pbits.pentries[lane].psequential_upc ==
                  want + 2);
      }
      retired++;
    }
  if (repair_out.pvalid) {
    repairs++;
    if (repairs == 1)
      CHECK(repair_out.pbits.pinvalidate &&
            repair_out.pbits.pentry == UINT64_C(0x104) &&
            repair_out.pbits.ptarget == UINT64_C(0x106));
    if (repairs == 2)
      CHECK(repair_out.pbits.pinvalidate &&
            repair_out.pbits.pentry == UINT64_C(0x200) &&
            repair_out.pbits.ptarget == UINT64_C(0x240));
  }
  if (speculate_out.pvalid) {
    if (speculate_out.pbits.paction == 1) {
      speculations++;
      CHECK(speculate_out.pbits.preturn_uaddress ==
            ((UINT64_C(0x600) + speculations * 4) & low_mask(64)));
    } else {
      return_speculations++;
      CHECK(speculate_out.pbits.paction == (return_speculations == 6 ? 3 : 2));
    }
  }
}
void offer(std::uint64_t pc, std::uint64_t data, std::uint64_t pred_pc = 0,
           std::uint64_t pred_target = 0, std::uint8_t compressed = 0,
           std::uint8_t fault = 0, std::array<Direction, 4> directions = {},
           Direction prefix = {}) {
  falling();
  blocks_in = {UINT64_C(1),
               {pc,
                data,
                {fault, UINT64_C(1), pc, {}},
                {pred_pc != 0, pred_pc, pred_target, compressed, UINT64_C(0)},
                directions,
                prefix}};
  accept([&] { return blocks_out.pready; });
  falling();
  blocks_in = {};
}
void drain() {
  until([&] { return expected_pc.size() == 0; });
  for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
    falling();
}
void clear() {
  falling();
  clear_in.pvalid = 1;
  falling();
  clear_in.pvalid = 0;
}
int main() {
  return run_test([] {
    reset = 1;
    instructions_in.pready = 1;
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    expected_pc.push_back(UINT64_C(0x100));
    expected_pc.push_back(UINT64_C(0x102));
    offer(UINT64_C(0x100),
          (field(UINT64_C(1), 16, 48) | field(UINT64_C(0x700513), 32, 16) |
           field(UINT64_C(1), 16, 0)),
          UINT64_C(0x104), UINT64_C(0x800), 1);
    drain();
    expected_pc.push_back(UINT64_C(0x200));
    offer(
        UINT64_C(0x200),
        (field(UINT64_C(0x100013), 32, 32) | field(UINT64_C(0x400006f), 32, 0)),
        UINT64_C(0x200), UINT64_C(0x900), 1);
    drain();
    expected_pc.push_back(UINT64_C(0x300));
    expected_pc.push_back(UINT64_C(0x302));
    expected_pc.push_back(UINT64_C(0x304));
    offer(UINT64_C(0x300),
          (field(UINT64_C(1), 16, 48) | field(UINT64_C(0xa001), 16, 32) |
           field(UINT64_C(1), 16, 16) | field(UINT64_C(1), 16, 0)),
          UINT64_C(0x304), UINT64_C(0x304), 1);
    drain();
    CHECK(repairs == 2);
    expected_pc.push_back(UINT64_C(0x400));
    expected_pc.push_back(UINT64_C(0x402));
    expected_pc.push_back(UINT64_C(0x404));
    expected_pc.push_back(UINT64_C(0x406));
    // JAL x0,+250 = 0x0fa0006f, split at the last parcel.
    offer(UINT64_C(0x400),
          (field(UINT64_C(0x6f), 16, 48) | field(UINT64_C(1), 16, 32) |
           field(UINT64_C(1), 16, 16) | field(UINT64_C(1), 16, 0)),
          UINT64_C(0x406), UINT64_C(0x500));
    offer(UINT64_C(0x408),
          (field(UINT64_C(1), 16, 48) | field(UINT64_C(1), 16, 32) |
           field(UINT64_C(1), 16, 16) | field(UINT64_C(0xfa0), 16, 0)),
          UINT64_C(0x406), UINT64_C(0x500));
    drain();
    CHECK(repairs == 2);
    // Restart directly at the incomplete prefix: prediction must not cut it
    // before the continuation makes the instruction complete.
    expected_pc.push_back(UINT64_C(0x806));
    offer(UINT64_C(0x806),
          (field(UINT64_C(0x6f), 16, 48) | field(UINT64_C(1), 16, 32) |
           field(UINT64_C(1), 16, 16) | field(UINT64_C(1), 16, 0)),
          UINT64_C(0x806), UINT64_C(0x900));
    offer(UINT64_C(0x808),
          (field(UINT64_C(1), 16, 48) | field(UINT64_C(1), 16, 32) |
           field(UINT64_C(1), 16, 16) | field(UINT64_C(0xfa0), 16, 0)),
          UINT64_C(0x806), UINT64_C(0x900));
    drain();
    CHECK(repairs == 2);
    expected_pc.push_back(UINT64_C(0x600));
    expected_pc.push_back(UINT64_C(0x604));
    instructions_in.pready = 0;
    offer(UINT64_C(0x600),
          (field(UINT64_C(0x180e7), 32, 32) | field(UINT64_C(0x180e7), 32, 0)));
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index)
      falling();
    CHECK(speculations == 0);
    instructions_in.pready = 1;
    drain();
    CHECK(speculations == 2);
    clear();
    expected_pc.push_back(UINT64_C(0x706));
    offer(UINT64_C(0x706), UINT64_C(0x613000000000000));
    offer(UINT64_C(0x708), 0, 0, 0, 0, 1);
    drain();
    clear();
    for (int p = UINT64_C(0xa00); p < UINT64_C(0xa08); p += 2)
      expected_pc.push_back(((p)&low_mask(64)));
    offer(UINT64_C(0xa00),
          (field(UINT64_C(0xa006), 16, 48) | field(UINT64_C(0x2002), 16, 32) |
           field(UINT64_C(0xa004), 16, 16) | field(UINT64_C(0x2000), 16, 0)));
    drain();
    expected_pc.push_back(UINT64_C(0xb00));
    offer(UINT64_C(0xb00), (field(UINT64_C(0x100013), 32, 32) |
                            field(UINT64_C(0x400006f), 32, 0)));
    drain();
    expected_pc.push_back(UINT64_C(0xb80));
    instructions_in.pready = 0;
    offer(
        UINT64_C(0xb80),
        (field(UINT64_C(0x100013), 32, 32) | field(UINT64_C(0x200006f), 32, 0)),
        UINT64_C(0xb80), UINT64_C(0xd00));
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index)
      falling();
    CHECK(!repair_out.pvalid);
    instructions_in.pready = 1;
    drain();
    expected_pc.push_back(UINT64_C(0xc00));
    offer(
        UINT64_C(0xc00),
        (field(UINT64_C(0x100013), 32, 32) | field(UINT64_C(0x2000063), 32, 0)),
        UINT64_C(0xc00), UINT64_C(0xd00));
    drain();
    expected_pc.push_back(UINT64_C(0xc40));
    offer(
        UINT64_C(0xc40),
        (field(UINT64_C(0x100010001), 48, 16) | field(UINT64_C(0xc001), 16, 0)),
        UINT64_C(0xc40), UINT64_C(0xd00), 1);
    drain();
    expected_pc.push_back(UINT64_C(0xc80));
    offer(
        UINT64_C(0xc80),
        (field(UINT64_C(0x100010001), 48, 16) | field(UINT64_C(0xa001), 16, 0)),
        UINT64_C(0xc80), UINT64_C(0xd00), 1);
    drain();
    expected_pc.push_back(UINT64_C(0xd00));
    expected_pc.push_back(UINT64_C(0xd04));
    offer(UINT64_C(0xd00),
          (field(UINT64_C(0x100013), 32, 32) | field(UINT64_C(0x63), 32, 0)));
    drain();
    CHECK(repairs == 7 && speculations == 2);
    // Warm BTB targets were frozen before an older call updated the RAS.
    ras_head_valid = 1;
    ras_head = UINT64_C(0x1234);
    expected_pc.push_back(UINT64_C(0xe00));
    offer(UINT64_C(0xe00),
          (field(UINT64_C(0x100013), 32, 32) | field(UINT64_C(0x8067), 32, 0)),
          UINT64_C(0xe00), UINT64_C(0xdead));
    drain();
    expected_pc.push_back(UINT64_C(0xe40));
    instructions_in.pready = 0;
    ras_head = UINT64_C(0x1111);
    offer(
        UINT64_C(0xe40),
        (field(UINT64_C(0x100010001), 48, 16) | field(UINT64_C(0x8082), 16, 0)),
        UINT64_C(0xe40), UINT64_C(0xdead), 1);
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index)
      falling();
    CHECK(return_speculations == 1 && !repair_out.pvalid);
    ras_head = UINT64_C(0x1234);
    instructions_in.pready = 1;
    drain();
    expected_pc.push_back(UINT64_C(0xe86));
    offer(
        UINT64_C(0xe86),
        (field(UINT64_C(0x8067), 16, 48) | field(UINT64_C(0x100010001), 48, 0)),
        UINT64_C(0xe86), UINT64_C(0xdead));
    offer(UINT64_C(0xe88),
          (field(UINT64_C(0x100010001), 48, 16) | field(UINT64_C(0), 16, 0)));
    drain();
    ras_head_valid = 0;
    expected_pc.push_back(UINT64_C(0xec0));
    offer(UINT64_C(0xec0),
          (field(UINT64_C(0x100013), 32, 32) | field(UINT64_C(0x8067), 32, 0)),
          UINT64_C(0xec0), UINT64_C(0xdead));
    drain();
    ras_head_valid = 1;
    expected_pc.push_back(UINT64_C(0xf00));
    offer(UINT64_C(0xf00),
          (field(UINT64_C(0x100013), 32, 32) | field(UINT64_C(0x8067), 32, 0)),
          UINT64_C(0xf00), UINT64_C(0x1234));
    drain();
    expected_pc.push_back(UINT64_C(0xf40));
    offer(UINT64_C(0xf40),
          (field(UINT64_C(0x100013), 32, 32) | field(UINT64_C(0x82e7), 32, 0)),
          UINT64_C(0xf40), UINT64_C(0xdead));
    drain(); // JALR x5,x1: pop-push.
    CHECK(repairs == 11 && return_speculations == 6);
    // A buffered next block fills either output slot, including a straddling
    // first or second instruction. Direction ownership follows each start PC.
    for (int offset = 0; offset < 4; offset++)
      for (int first_c = 0; first_c < 2; first_c++)
        for (int second_c = 0; second_c < 2; second_c++) {
          uint128 bytes;
          std::uint64_t base, pc, second_pc;
          std::uint32_t first_raw, second_raw;
          std::array<Direction, 4> first_directions, second_directions;
          Direction prefix;
          Packet held;
          int first_bytes, second_bytes, second_parcel;
          clear();
          instructions_in.pready = 0;
          base = UINT64_C(0x10000) +
                 ((int((offset * 4 + first_c * 2 + second_c) * 32)) &
                  low_mask(64));
          pc = base + ((offset * 2) & low_mask(64));
          first_bytes = first_c != 0 ? 2 : 4;
          second_bytes = second_c != 0 ? 2 : 4;
          second_pc = pc + ((first_bytes)&low_mask(64));
          first_raw = first_c != 0 ? UINT64_C(1) : UINT64_C(0x100013);
          second_raw = second_c != 0 ? UINT64_C(1) : UINT64_C(0x200013);
          for (int parcel = 0; parcel < 8; parcel++)
            write_bits(bytes, parcel * 16, 16, UINT64_C(1));
          write_bits(bytes, offset * 16, 32, first_raw);
          if (first_c != 0)
            write_bits(bytes, (offset + 1) * 16, 16, UINT64_C(1));
          second_parcel = offset + first_bytes / 2;
          write_bits(bytes, second_parcel * 16, 32, second_raw);
          if (second_c != 0)
            write_bits(bytes, (second_parcel + 1) * 16, 16, UINT64_C(1));
          for (int parcel = 0; parcel < 4; parcel++) {
            first_directions[parcel] = {UINT64_C(1), std::uint16_t(parcel),
                                        UINT64_C(17), UINT64_C(0)};
            second_directions[parcel] = {UINT64_C(1), std::uint16_t(4 + parcel),
                                         UINT64_C(29), UINT64_C(0)};
          }
          prefix = {UINT64_C(1), UINT64_C(3), UINT64_C(17), UINT64_C(0)};
          expected_pc.push_back(pc);
          expected_pc.push_back(second_pc);
          offer(pc, bits(bytes, 63, 0), 0, 0, 0, 0, first_directions);
          offer(base + 8, bits(bytes, 127, 64), 0, 0, 0, 0, second_directions,
                prefix);
          CHECK(instructions_out.pvalid && instructions_out.pbits.pcount == 2);
          CHECK(instructions_out.pbits.pentries[0].praw_uinstruction ==
                    first_raw &&
                instructions_out.pbits.pentries[1].praw_uinstruction ==
                    second_raw);
          for (int lane = 0; lane < 2; lane++) {
            int parcel, size;
            parcel = lane == 0 ? offset : second_parcel;
            size = lane == 0 ? first_bytes : second_bytes;
            CHECK(instructions_out.pbits.pentries[lane].pdirection.phistory ==
                  (parcel < 4 ? UINT64_C(17) : UINT64_C(29)));
            CHECK(instructions_out.pbits.pentries[lane].pdirection.pindex ==
                  std::uint16_t(parcel));
            CHECK(instructions_out.pbits.pentries[lane].psequential_upc ==
                  instructions_out.pbits.pentries[lane].ppc +
                      ((size)&low_mask(64)));
          }
          held = instructions_out.pbits;
          for (int repeat_index = 0; repeat_index < (4); ++repeat_index) {
            falling();
            CHECK(instructions_out.pvalid &&
                  same_packet(instructions_out.pbits, held) && available == 1);
          }
          instructions_in.pready = 1;
          settle();
          CHECK(available ==
                ((1 + (offset * 2 + first_bytes + second_bytes) / 8) &
                 low_mask(2)));
          rising();
          falling();
          instructions_in.pready = 0;
          CHECK(expected_pc.size() == 0);
        }
    // An unavailable second block never delays a complete first instruction.
    clear();
    instructions_in.pready = 0;
    expected_pc.push_back(UINT64_C(0x11006));
    offer(UINT64_C(0x11006), UINT64_C(0x1000100010001));
    CHECK(instructions_out.pvalid && instructions_out.pbits.pcount == 1);
    instructions_in.pready = 1;
    rising();
    falling();
    instructions_in.pready = 0;
    // A fault in a second instruction's continuation preserves the first
    // instruction and reports the straddling PC with the continuation fault VA.
    clear();
    expected_pc.push_back(UINT64_C(0x11104));
    expected_pc.push_back(UINT64_C(0x11106));
    offer(UINT64_C(0x11104),
          (field(UINT64_C(19), 16, 48) | field(UINT64_C(1), 16, 32) |
           field(UINT64_C(0x10001), 32, 0)));
    offer(UINT64_C(0x11108), 0, 0, 0, 0, 1);
    CHECK(instructions_out.pvalid && instructions_out.pbits.pcount == 2 &&
          !instructions_out.pbits.pentries[0].pfault.pvalid &&
          instructions_out.pbits.pentries[1].pfault.pvalid &&
          instructions_out.pbits.pentries[1].pfault.pvalue ==
              UINT64_C(0x11108));
    instructions_in.pready = 1;
    rising();
    falling();
    instructions_in.pready = 0;
    // Drain all three resident blocks as a continuous parcel stream, rather
    // than emitting a singleton at every eight-byte boundary.
    clear();
    for (int parcel = 0; parcel < 9; parcel++)
      expected_pc.push_back(UINT64_C(0x11206) + ((parcel * 2) & low_mask(64)));
    offer(UINT64_C(0x11206), UINT64_C(0x1000100010001));
    offer(UINT64_C(0x11208), UINT64_C(0x1000100010001));
    offer(UINT64_C(0x11210), UINT64_C(0x1000100010001));
    CHECK(available == 0 && !blocks_out.pready);
    instructions_in.pready = 1;
    settle();
    CHECK(available == 1);
    while (expected_pc.size() > 0) {
      CHECK(instructions_out.pvalid &&
            instructions_out.pbits.pcount ==
                (expected_pc.size() > 1 ? UINT64_C(2) : UINT64_C(1)));
      rising();
      falling();
    }
    instructions_in.pready = 0;
    // A taken branch in slot one can release both contributing blocks. Neither
    // fallthrough bytes nor a following noncontiguous target enter the packet.
    clear();
    expected_pc.push_back(UINT64_C(0x11306));
    expected_pc.push_back(UINT64_C(0x11308));
    offer(UINT64_C(0x11306), UINT64_C(0x1000100010001));
    offer(
        UINT64_C(0x11308),
        (field(UINT64_C(0x100013), 32, 32) | field(UINT64_C(0x400006f), 32, 0)),
        UINT64_C(0x11308), UINT64_C(0x11348));
    CHECK(instructions_out.pvalid && instructions_out.pbits.pcount == 2 &&
          instructions_out.pbits.pentries[1].pprediction.pvalid &&
          !repair_out.pvalid);
    instructions_in.pready = 1;
    settle();
    CHECK(available == 3);
    rising();
    falling();
    instructions_in.pready = 0;
    expected_pc.push_back(UINT64_C(0x1134e));
    offer(UINT64_C(0x1134e), UINT64_C(0x1000100010001));
    offer(UINT64_C(0x12000), UINT64_C(0x1000100010001));
    CHECK(instructions_out.pvalid && instructions_out.pbits.pcount == 1);
    instructions_in.pready = 1;
    rising();
    falling();
    instructions_in.pready = 0;
    // Fresh continuation direction can override the prefix's provisional BTB
    // taken prediction without inventing an assembler repair.
    clear();
    expected_pc.push_back(UINT64_C(0x12106));
    expected_pc.push_back(UINT64_C(0x1210a));
    offer(UINT64_C(0x12106),
          (field(UINT64_C(0x63), 16, 48) | field(UINT64_C(0x100010001), 48, 0)),
          UINT64_C(0x12106), UINT64_C(0x12306));
    offer(
        UINT64_C(0x12108),
        (field(UINT64_C(0x100010001), 48, 16) | field(UINT64_C(0x2000), 16, 0)),
        0, 0, 0, 0, {},
        {UINT64_C(1), UINT64_C(0x123), UINT64_C(0x29), UINT64_C(0)});
    CHECK(instructions_out.pvalid && instructions_out.pbits.pcount == 2 &&
          !instructions_out.pbits.pentries[0].pprediction.pvalid &&
          instructions_out.pbits.pentries[0].pdirection.pvalid &&
          !repair_out.pvalid);
    instructions_in.pready = 1;
    rising();
    falling();
    instructions_in.pready = 0;
    clear();
    instructions_in.pready = 1;
  });
}
