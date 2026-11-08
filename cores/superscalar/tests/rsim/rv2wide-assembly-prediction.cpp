// Checks assembly credits, cross-block prediction ownership, repairs, and compressed expansion.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
#include "fetch-fault.hpp"
using Direction = std::remove_cvref_t<decltype(blocks_in.pbits.pdirections[0])>;
using Packet = std::remove_cvref_t<decltype(instructions_out.pbits)>;
using Fault = std::remove_cvref_t<decltype(blocks_in.pbits.pfault)>;
#ifndef TEST_HYPERVISOR
#define TEST_HYPERVISOR 0
#endif
Queue<std::uint64_t> expected_pc;
int repairs = 0, speculations = 0, retired = 0, return_speculations = 0;
void drive() {}
void falling_update() {}
bool same_packet(const Packet &a, const Packet &b) {
  if (a.pcount != b.pcount)
    return false;
  for (unsigned i = 0; i < a.pcount; ++i) {
    const auto &x = a.pentries[i];
    const auto &y = b.pentries[i];
    if (x.ppc != y.ppc || x.pinstruction != y.pinstruction ||
        x.praw_uinstruction != y.praw_uinstruction ||
        x.psequential_upc != y.psequential_upc ||
        x.pcompressed_uillegal != y.pcompressed_uillegal ||
        x.pfault.pvalid != y.pfault.pvalid || x.pfault.pcause != y.pfault.pcause ||
        x.pfault.pcontinuation != y.pfault.pcontinuation || !same_fetch_guest(x.pfault, y.pfault) ||
        x.pprediction.pvalid != y.pprediction.pvalid ||
        x.pprediction.ppc != y.pprediction.ppc ||
        x.pprediction.ptarget != y.pprediction.ptarget ||
        x.pprediction.pcompressed != y.pprediction.pcompressed ||
        x.pprediction.pras_uaction != y.pprediction.pras_uaction ||
        x.pspeculated_uras_uaction != y.pspeculated_uras_uaction ||
        x.pdirection.pvalid != y.pdirection.pvalid ||
        x.pdirection.pindex != y.pdirection.pindex ||
        x.pdirection.phistory != y.pdirection.phistory ||
        x.pdirection.ptaken != y.pdirection.ptaken ||
        x.pdirection.pbase_uindex != y.pdirection.pbase_uindex ||
        x.pdirection.pbase_utaken != y.pdirection.pbase_utaken ||
        x.pdirection.pgshare_utaken != y.pdirection.pgshare_utaken)
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
        CHECK((instructions_out.pbits.pentries[lane].pfault.pvalid != 0) &&
              parcel_fault_address(instructions_out.pbits.pentries[lane]) ==
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
                {std::uint8_t(fault != 0), 1, pc},
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

void check_fault(const Fault &expected, std::uint64_t pc) {
  settle();
  CHECK(instructions_out.pvalid && instructions_out.pbits.pcount == 1);
  const auto &instruction = instructions_out.pbits.pentries[0];
  const auto &fault = instruction.pfault;
  CHECK(instruction.ppc == pc && fault.pvalid && fault.pcause == expected.pcause && parcel_fault_address(instruction) == expected.pvalue &&
        same_fetch_guest(fault, expected) &&
        !instruction.pprediction.pvalid && !repair_out.pvalid &&
        !discover_out.pvalid && !speculate_out.pvalid);
}

void offer_fault(std::uint64_t pc, const Fault &fault) {
  falling();
  blocks_in = {};
  blocks_in.pvalid = true;
  blocks_in.pbits.ppc = pc;
  blocks_in.pbits.pfault = fault;
  // A cache/translation fault can arrive alongside a speculative BTB prediction.
  blocks_in.pbits.pprediction = {true, pc, ~pc, true, 0};
  accept([&] { return blocks_out.pready; });
  falling();
  blocks_in = {};
}

void compact_storage_tests() {
  for (auto base : {UINT64_C(0x0000003ffffff000), UINT64_C(0xffffffc012345000),
                    UINT64_C(0xfffffffffffff000)}) {
    clear();
    instructions_in.pready = false;
    const auto first = base + 6;
    const auto last = base + 0x104;
    const auto exceptional = UINT64_C(0x0000004000000000);
    const auto target = UINT64_C(0xffffffef12345678);
    Fault fault{};
    fault.pvalid = 1;
    fault.pcause = TEST_HYPERVISOR ? 20 : 1;
    fault.pvalue = exceptional;
    if (TEST_HYPERVISOR) set_fetch_guest(fault, true, UINT64_C(0x123456789abc), 1);
    expected_pc.push_back(first);
    expected_pc.push_back(exceptional);
    expected_pc.push_back(last);

    // Observe the same normal owner before and after empty-window bypass capture.
    falling();
    blocks_in = {};
    blocks_in.pvalid = true;
    blocks_in.pbits.ppc = first;
    blocks_in.pbits.pdata = UINT64_C(1) << 48; // C.NOP at the last parcel.
    blocks_in.pbits.pfault.pvalue = ~first; // Absent fault payload canonicalizes to zero.
    blocks_in.pbits.pprediction = {false, ~first, target, false, 0};
    blocks_in.pbits.pdirections[0].pindex = 0xabc;
    blocks_in.pbits.pdirections[3] = {true, 0xabf, 0x125, false, std::uint16_t((first >> 1) & 0xfff), true, false};
    settle();
    CHECK(blocks_out.pready && instructions_out.pvalid);
    const Packet bypass = instructions_out.pbits;
    CHECK(bypass.pcount == 1 && bypass.pentries[0].ppc == first &&
          !bypass.pentries[0].pfault.pcontinuation);
    rising();
    falling();
    blocks_in = {};
    settle();
    CHECK(same_packet(instructions_out.pbits, bypass));
    offer_fault(exceptional, fault);
    offer(last, UINT64_C(0x00010067) << 32, last, target); // JALR x0,x2,0: no RAS action.
    CHECK(available == 0 && !blocks_out.pready);
    for (unsigned cycle = 0; cycle < 5; ++cycle) {
      blocks_in.pbits.ppc = ~first;
      settle();
      CHECK(same_packet(instructions_out.pbits, bypass));
      rising();
      falling();
    }
    instructions_in.pready = true;
    rising();
    falling();
    instructions_in.pready = false;
    check_fault(fault, exceptional);
    instructions_in.pready = true;
    rising();
    falling();
    instructions_in.pready = false;
    settle();
    CHECK(instructions_out.pvalid && instructions_out.pbits.pcount == 1);
    const auto &predicted = instructions_out.pbits.pentries[0];
    CHECK(predicted.ppc == last && predicted.pprediction.pvalid &&
          predicted.pprediction.ppc == last && predicted.pprediction.ptarget == target);
    instructions_in.pready = true;
    drain();
    CHECK(available == 3);
  }
}

void prediction_owner_tests() {
  const auto target = UINT64_C(0xffffffef12345678);
  for (auto base : {UINT64_C(0x0000003ffffff000), UINT64_C(0xffffffc012345000),
                    UINT64_C(0xfffffffffffff000)}) {
    // Every parcel owner survives bypass capture and backpressure, without
    // treating a high/negative PC as an offset or losing an admitted compact target.
    for (unsigned parcel = 0; parcel < 4; ++parcel) {
      clear();
      instructions_in.pready = false;
      const auto pc = base + 2 * parcel;
      expected_pc.push_back(pc);
      falling();
      blocks_in = {};
      blocks_in.pvalid = true;
      blocks_in.pbits.ppc = pc;
      blocks_in.pbits.pdata = UINT64_C(0x8102) << (16 * parcel); // C.JR x2.
      blocks_in.pbits.pprediction = {true, pc, target, true, 0};
      blocks_in.pbits.pdirections[0].pindex = 0x554;
      blocks_in.pbits.pdirections[parcel] = {true, std::uint16_t(0x554 + parcel), std::uint16_t(0x299 + parcel),
                                           false, std::uint16_t((pc >> 1) & 0xfff), true, false};
      settle();
      CHECK(instructions_out.pvalid && instructions_out.pbits.pcount == 1);
      const Packet bypass = instructions_out.pbits;
      const auto &prediction = bypass.pentries[0].pprediction;
      CHECK(prediction.pvalid && prediction.ppc == pc && prediction.ptarget == target);
      CHECK(bypass.pentries[0].pdirection.pindex == 0x554 + parcel &&
            bypass.pentries[0].pdirection.pbase_uindex == ((pc >> 1) & 0xfff) &&
            bypass.pentries[0].pdirection.phistory == 0x299 + parcel);
      rising();
      falling();
      blocks_in = {};
      for (unsigned stall = 0; stall < 3; ++stall) {
        blocks_in.pbits.pprediction.ppc = ~pc;
        settle();
        CHECK(same_packet(instructions_out.pbits, bypass) && !repair_out.pvalid);
        rising();
        falling();
      }
      instructions_in.pready = true;
      drain();
      CHECK(available == 3);
    }

    // Invalidating a stale entry must still name its full original address.
    clear();
    instructions_in.pready = false;
    const auto pc = base + 6;
    expected_pc.push_back(pc);
    offer(pc, UINT64_C(1) << 48, pc, target, true); // C.NOP is not control flow.
    instructions_in.pready = true;
    settle();
    CHECK(repair_out.pvalid && repair_out.pbits.pinvalidate &&
          repair_out.pbits.pentry == pc && repair_out.pbits.ptarget == pc + 2);
    drain();
  }

  // Only the continuation predicts this JALR. Previous must reconstruct the
  // prefix across pages, negative addresses, and unsigned XLEN wraparound.
  for (auto base : {UINT64_C(0x123ff8), UINT64_C(0xffffffc012345ff8),
                    UINT64_C(0xfffffffffffffff8)}) {
    clear();
    instructions_in.pready = false;
    const auto pc = base + 6;
    expected_pc.push_back(pc);
    offer(pc, UINT64_C(0x0067) << 48);
    CHECK(!instructions_out.pvalid);
    falling();
    blocks_in = {};
    blocks_in.pvalid = true;
    blocks_in.pbits.ppc = base + 8;
    blocks_in.pbits.pdata = 1; // Upper halfword of JALR x0,x2,0.
    blocks_in.pbits.pprediction = {true, pc, target, false, 0};
    blocks_in.pbits.pprefix_udirection = {true, 0xe97, 0x2ac, false, std::uint16_t((pc >> 1) & 0xfff), false, true};
    settle();
    CHECK(instructions_out.pvalid && instructions_out.pbits.pcount == 1);
    const Packet bypass = instructions_out.pbits;
    CHECK(bypass.pentries[0].ppc == pc && bypass.pentries[0].pprediction.pvalid &&
          bypass.pentries[0].pprediction.ppc == pc &&
          bypass.pentries[0].pprediction.ptarget == target);
    CHECK(bypass.pentries[0].pdirection.pindex == 0xe97 &&
          bypass.pentries[0].pdirection.pbase_uindex == ((pc >> 1) & 0xfff) &&
          bypass.pentries[0].pdirection.phistory == 0x2ac);
    rising();
    falling();
    blocks_in = {};
    settle();
    CHECK(same_packet(instructions_out.pbits, bypass) && !repair_out.pvalid);
    instructions_in.pready = true;
    drain();
    CHECK(available == 3);
  }

  clear();
  instructions_in.pready = false;
  for (auto invalid_pc : {UINT64_C(0x123001), UINT64_C(0x124000)}) {
    expect_failure("superscalar_assembly_prediction_owner", [&] {
      offer(UINT64_C(0x123000), 1, invalid_pc, target, true);
    });
    blocks_in = {};
    clear();
  }
}

void direction_contract_tests() {
  for (unsigned mutation = 0; mutation < 5; ++mutation) {
    clear();
    instructions_in.pready = false;
    std::array<Direction, 4> directions{};
    directions[0].pindex = 0x554;
    directions[2] = {true, 0x556, 0x321, false, 0x802, true, false};
    Direction prefix{true, 0xe97, 0x125, false, 0x7ff, false, true};
    if (mutation == 0) directions[2].pindex ^= 4; // Row differs from the saved main row.
    if (mutation == 1) directions[2].pindex ^= 1; // Wrong parcel.
    if (mutation == 2) directions[2].pbase_uindex ^= 0x100; // Wrong PC-derived base.
    if (mutation == 3) prefix.pindex ^= 1;
    if (mutation == 4) prefix.pbase_uindex ^= 0x100;
    expect_failure(mutation < 3 ? "superscalar_assembly_direction_indices" : "superscalar_assembly_prefix_indices", [&] {
      offer(0x123000, 1, 0, 0, 0, 0, directions, prefix);
    });
    blocks_in = {};
  }
  clear();
}

void fault_storage_tests() {
  const std::array<std::uint64_t, 3> pcs{
      UINT64_C(0xffffffc012345001), UINT64_C(0x0000004000000000),
      UINT64_C(0x0000004000000008)};
  std::array<Fault, 3> faults{};
  for (unsigned i = 0; i < 3; ++i) {
    constexpr unsigned causes[] = {0, 1, 12};
    faults[i].pvalid = 1;
    faults[i].pcause = causes[i];
    faults[i].pvalue = pcs[i];
    if (TEST_HYPERVISOR) {
      set_fetch_guest(faults[i], true, UINT64_C(0x123456780000) + i * 8, i & 1);
    }
  }

  // Independent fault owners fill every slot while the oldest is stalled.
  clear();
  instructions_in.pready = false;
  for (unsigned i = 0; i < 3; ++i) {
    expected_pc.push_back(pcs[i]);
    offer_fault(pcs[i], faults[i]);
  }
  CHECK(available == 0 && !blocks_out.pready);
  for (unsigned i = 0; i < 3; ++i) {
    for (unsigned stall = 0; stall < 3; ++stall) {
      blocks_in.pbits.pfault = faults[(i + 1) % 3]; // Invalid live noise must not replace an owner.
      check_fault(faults[i], pcs[i]);
      rising();
      falling();
    }
    instructions_in.pready = true;
    rising();
    falling();
    instructions_in.pready = false;
  }
  blocks_in = {};
  CHECK(available == 3 && expected_pc.size() == 0);

  for (unsigned guest_walk = 0; guest_walk < (TEST_HYPERVISOR ? 2U : 1U); ++guest_walk) {
    auto fault = faults[2];
    if (TEST_HYPERVISOR) {
      fault.pvalid = 1;
      fault.pcause = 20;
      set_fetch_guest(fault, true, UINT64_C(0x123456780010), guest_walk);
    }

    // Empty-window bypass and retained paths preserve the same exact record.
    clear();
    instructions_in.pready = true;
    expected_pc.push_back(pcs[2]);
    falling();
    blocks_in = {};
    blocks_in.pvalid = true;
    blocks_in.pbits.ppc = pcs[2];
    blocks_in.pbits.pfault = fault;
    check_fault(fault, pcs[2]);
    rising();
    falling();
    blocks_in = {};

    // The failed parcel is PC+2 even when XLEN wraps; its GPA is independent.
    for (auto pc : {UINT64_C(0x123fe), UINT64_MAX - 1}) {
      clear();
      instructions_in.pready = false;
      expected_pc.push_back(pc);
      offer(pc, UINT64_C(0x0013000000000000));
      fault.pvalue = pc + 2;
      offer_fault(pc + 2, fault);
      for (unsigned stall = 0; stall < 3; ++stall) {
        check_fault(fault, pc);
        rising();
        falling();
      }
      instructions_in.pready = true;
      rising();
      falling();
      CHECK(available == 3 && expected_pc.size() == 0);
    }
  }

  // Recovery discards resident fault payloads together with their validity.
  clear();
  instructions_in.pready = false;
  offer_fault(pcs[0], faults[0]);
  clear();
  settle();
  CHECK(!instructions_out.pvalid && available == 3);
  // A data-access exception is not a supported fetch exception.
  auto unsupported = faults[1];
  unsupported.pcause = 5;
  expect_failure("superscalar_assembly_supported_fetch_fault", [&] {
    offer_fault(pcs[1], unsupported);
  });
  blocks_in = {};
  clear();
  auto mismatched = faults[1];
  mismatched.pvalue ^= UINT64_C(0x1234000000000000);
  expect_failure("superscalar_assembly_fault_address", [&] {
    offer_fault(pcs[1], mismatched);
  });
  blocks_in = {};
  clear();
  if (TEST_HYPERVISOR) {
    // The H fixture selects compact PCs; only faulting blocks may escape that range.
    expect_failure("superscalar_assembly_success_pc_fits", [&] {
      offer_fault(pcs[1], Fault{});
    });
  }
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
            first_directions[parcel] = {UINT64_C(1), std::uint16_t(0x554 + parcel),
                                        std::uint16_t(17 + parcel), UINT64_C(0), std::uint16_t(((base >> 1) + parcel) & 0xfff), 1, 0};
            second_directions[parcel] = {UINT64_C(1), std::uint16_t(0xaa8 + parcel),
                                         std::uint16_t(29 + parcel), UINT64_C(0), std::uint16_t(((base >> 1) + 4 + parcel) & 0xfff), 0, 1};
          }
          prefix = {UINT64_C(1), UINT64_C(0xe97), UINT64_C(0x355), UINT64_C(0), std::uint16_t(((base >> 1) + 3) & 0xfff), 1, 0};
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
            const bool straddles = parcel == 3 && size == 4;
            CHECK(instructions_out.pbits.pentries[lane].pdirection.phistory ==
                  (straddles ? 0x355 : parcel < 4 ? 17 + parcel : 29 + parcel - 4));
            CHECK(instructions_out.pbits.pentries[lane].pdirection.pindex ==
                  (straddles ? 0xe97 : parcel < 4 ? 0x554 + parcel : 0xaa8 + parcel - 4));
            CHECK(instructions_out.pbits.pentries[lane].pdirection.pbase_uindex == (((base >> 1) + parcel) & 0xfff) &&
                  instructions_out.pbits.pentries[lane].pdirection.pbase_utaken == (parcel < 4) &&
                  instructions_out.pbits.pentries[lane].pdirection.pgshare_utaken == (parcel >= 4));
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
          instructions_out.pbits.pentries[0].pfault.pvalid == 0 &&
          (instructions_out.pbits.pentries[1].pfault.pvalid != 0) &&
          parcel_fault_address(instructions_out.pbits.pentries[1]) ==
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
        {UINT64_C(1), UINT64_C(0x123), UINT64_C(0x29), UINT64_C(0), 0x83, 1, 0});
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
    compact_storage_tests();
    prediction_owner_tests();
    direction_contract_tests();
    fault_storage_tests();
  });
}
