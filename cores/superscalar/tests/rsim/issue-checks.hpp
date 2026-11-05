// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <array>
#include <cstdint>
#include <tuple>

constexpr unsigned lanes = std::tuple_size_v<std::remove_cvref_t<decltype(instructions)>>;
using Pc = std::remove_cvref_t<decltype(pcs[0])>;
enum ReadSource { Register, OlderConstant, OlderRegister, OlderPc };
std::uint64_t rng = 0xb0837ac0de123;
std::uint64_t random_value() {
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return rng;
}
unsigned addi(unsigned rd, unsigned rs1 = 0, unsigned imm = 0) {
  return (imm << 20) | (rs1 << 15) | (rd << 7) | 0x13;
}
unsigned add(unsigned rd, unsigned rs1, unsigned rs2) {
  return (rs2 << 20) | (rs1 << 15) | (rd << 7) | 0x33;
}
unsigned load(unsigned rd, unsigned rs1 = 10) {
  return (rs1 << 15) | (rd << 7) | 0x2003;
}
unsigned multiply(unsigned rd) { return (11 << 20) | (10 << 15) | (rd << 7) | 0x2000033; }
unsigned fadd(unsigned rd, unsigned rs1, unsigned rs2) { return (rs2 << 20) | (rs1 << 15) | (rd << 7) | 0x53; }
unsigned fmv_x(unsigned rd, unsigned rs1) { return 0xe0000053 | (rs1 << 15) | (rd << 7); }
unsigned vadd(unsigned rd) { return 0x02000057 | (12 << 20) | (10 << 15) | (rd << 7); }

void clear() {
  status = {};
  resources = {};
  resources.pserialization_uready = 1;
  resources.pmemory_uorder_uready = 1;
  resources.pmultiply_uavailable = 1;
  resources.pwrite_uavailable = {1, 1};
  read_blocked = {};
  for (auto &data : read_data) data = random_value();
  faults = {};
  illegal = {};
  ras = {};
  late_gpr = {};
  wb_writes = {};
  wb_available = {1, 1};
  for (unsigned lane = 0; lane < lanes; ++lane) {
    instructions[lane] = addi(0);
    pcs[lane] = 0x1000 + 4 * lane;
    present[lane] = 1;
    wb_coalescible[lane] = 1;
  }
}

struct Allocation {
  std::array<bool, lanes> writes{};
  std::array<unsigned, lanes> ports{};
  std::array<std::array<bool, lanes>, lanes> shadowed_by{};
  std::array<bool, 2> occupied{};
  bool fits = true;
};

// Architectural youngest-writer selection followed by the physical fixed-owner mapping.
Allocation allocate(const std::array<unsigned, lanes> &destinations,
                    const std::array<bool, lanes> &writes,
                    const std::array<bool, lanes> &coalescible,
                    const std::array<bool, 2> &available) {
  Allocation result;
  std::array<bool, 32> shadowed{};
  for (int lane = lanes - 1; lane >= 0; --lane) {
    const auto rd = destinations[lane];
    result.writes[lane] = writes[lane] && rd && !shadowed[rd];
    if (writes[lane] && coalescible[lane]) shadowed[rd] = true;
  }
  for (unsigned lane = 0; lane < lanes; ++lane) {
    for (unsigned younger = lane + 1; younger < lanes; ++younger)
      result.shadowed_by[lane][younger] = writes[younger] && destinations[lane] == destinations[younger];
    const unsigned port = lane < 2 ? lane : (!result.writes[0] && available[0] ? 0 : 1);
    result.ports[lane] = port;
    if (result.writes[lane]) {
      result.fits &= available[port] && !result.occupied[port];
      result.occupied[port] = true;
    }
  }
  return result;
}

template<class Routing>
void check_allocation(const Routing &actual, const Allocation &expected) {
  CHECK(bool(actual.pfits) == expected.fits);
  for (unsigned lane = 0; lane < lanes; ++lane) {
    CHECK(bool(actual.pwrites[lane]) == expected.writes[lane]);
    CHECK(actual.pports[lane] == expected.ports[lane]);
    for (unsigned younger = 0; younger < lanes; ++younger)
      CHECK(bool(actual.pshadowed_uby[lane][younger]) == expected.shadowed_by[lane][younger]);
  }
  for (unsigned port = 0; port < 2; ++port)
    CHECK(bool(actual.poccupied[port]) == expected.occupied[port]);
}

void expect(unsigned count) {
  eval();
  if (decision.pcount != count) {
    std::cerr << "issue got " << unsigned(decision.pcount) << " expected " << count << " instructions";
    for (auto instruction : instructions) std::cerr << ' ' << std::hex << instruction;
    std::cerr << std::dec << '\n';
  }
  CHECK(decision.pcount == count);
  for (unsigned lane = 0; lane < lanes; ++lane) {
    CHECK(bool(decision.padmitted[lane]) == (lane < count));
    CHECK(issued_instructions[lane].pinstruction == instructions[lane]);
  }
}

// Independent oracle for ordinary ALU groups; sources 10/11 never match destinations 0..7.
void ordinary(const std::array<unsigned, lanes> &destinations,
              const std::array<unsigned, lanes> &uses) {
  std::array<std::array<bool, 2>, lanes> needed{};
  std::array<unsigned, 4> addresses{};
  for (unsigned lane = 0; lane < lanes; ++lane) {
    const unsigned rd = destinations[lane];
    instructions[lane] = uses[lane] == 0 ? ((rd << 7) | 0x37) :
      uses[lane] == 1 ? addi(rd, 10) : add(rd, uses[lane] & 1 ? 10 : 0, 11);
    needed[lane] = {bool(present[lane] && (uses[lane] & 1)), bool(present[lane] && (uses[lane] & 2))};
    if (lane < 2) {
      addresses[2 * lane] = uses[lane] & 1 ? 10 : 0;
      addresses[2 * lane + 1] = uses[lane] & 2 ? 11 : 0;
    }
  }
  if constexpr (lanes == 3) {
    if (needed[2][0] && !needed[1][1]) addresses[3] = 10;
    if (needed[2][1] && !needed[0][1]) addresses[1] = 11;
  }
  const std::array<bool, 2> available{bool(resources.pwrite_uavailable[0]), bool(resources.pwrite_uavailable[1])};
  std::array<bool, lanes> coalescible{}, writes{};
  coalescible.fill(true);
  auto expected = allocate(destinations, writes, coalescible, available);
  std::array<bool, 2> reserved{};
  unsigned count = 0;
  if (!resources.pstop && !resources.pretry_uwait && !resources.ppause_ublocked) {
    for (unsigned lane = 0; lane < lanes; ++lane) {
      if (!present[lane]) break;
      if (status[lane].pexecution_ublocked || status[lane].pdestination_ublocked || status[lane].pfp_udata_ublocked ||
          status[lane].pfp_ucapacity_ublocked || status[lane].pvector_ublocked || status[lane].pcsr_uread_uwait) break;
      if (lane && (resources.pyounger_uslot_ureserved || resources.pvector_uwaiting)) break;
      bool blocked = false;
      for (unsigned source = 0; source < 2; ++source) {
        const unsigned port = lane < 2 ? 2 * lane + source : (source ? 1 : 3);
        const bool granted = lane < 2 || !needed[lane][source] || !needed[source ? 0 : 1][1];
        blocked |= needed[lane][source] && (!granted || read_blocked[port]);
      }
      if (blocked) break;
      writes[lane] = destinations[lane] != 0;
      const auto next = allocate(destinations, writes, coalescible, available);
      if (!next.fits) break;
      expected = next;
      for (unsigned port = 0; port < 2; ++port) reserved[port] |= next.occupied[port];
      ++count;
    }
  }
  expect(count);
  check_allocation(decision.pwrites, expected);
  for (unsigned port = 0; port < 4; ++port) CHECK(reads.paddresses[port] == addresses[port]);
  for (unsigned port = 0; port < 2; ++port) CHECK(bool(decision.preserve[port]) == reserved[port]);
  // Prepared ordinary operands follow the four selected paths, independent of admission.
  for (unsigned lane = 0; lane < lanes; ++lane) {
    CHECK(issued_instructions[lane].pcontrol.poperand.puses_urs1 == (uses[lane] != 0));
    CHECK(issued_instructions[lane].pcontrol.poperand.puses_urs2 == bool(uses[lane] & 2));
    CHECK(issued_instructions[lane].ppair.psources[0] == Register);
    CHECK(issued_instructions[lane].ppair.psources[1] == Register);
    for (unsigned source = 0; source < 2; ++source) {
      const unsigned port = lane < 2 ? 2 * lane + source : (source ? 1 : 3);
      const bool granted = lane < 2 || !needed[lane][source] || !needed[source ? 0 : 1][1];
      const Pc expected_data = needed[lane][source] && granted ? read_data[port] : 0;
      CHECK((source ? operands[lane].prs2 : operands[lane].prs1) == expected_data);
    }
    CHECK(!operands[lane].prewrite.pcomposed);
    CHECK(!operands[lane].prewrite.pmemory_uaddress);
    CHECK(!operands[lane].prewrite.pstore_udata);
    CHECK(!operands[lane].prewrite.pvector_uscalar);
    CHECK(!operands[lane].prewrite.pbranch_uoperands[0]);
    CHECK(!operands[lane].prewrite.pbranch_uoperands[1]);
  }
}

void check_prepared_operands() {
  // Constants replace either source even when both ordinary younger paths are blocked.
  for (int value : {-2048, -7, 0, 2047}) {
    clear();
    instructions[0] = addi(1, 0, value);
    instructions[1] = add(2, 1, 1);
    read_blocked = {1, 1, 1, 1};
    expect(lanes);
    CHECK(operands[1].prs1 == Pc(value));
    CHECK(operands[1].prs2 == Pc(value));
    CHECK(issued_instructions[1].ppair.psources[0] == OlderConstant);
    CHECK(issued_instructions[1].ppair.psources[1] == OlderConstant);
    CHECK(!operands[1].prewrite.pcomposed);
  }
  clear();
  instructions[0] = 0x812340b7; // LUI x1,0x81234
  instructions[1] = addi(2, 1, 3);
  read_blocked[2] = 1;
  expect(lanes);
  CHECK(operands[1].prs1 == Pc(std::int64_t(std::int32_t(0x81234000u))));

  // U+I composition retains the negative carry below -2^31 on RV64.
  constexpr unsigned sum_width = sizeof(Pc) == 4 ? 32 : 33;
  constexpr std::uint64_t sum_mask = (std::uint64_t(1) << sum_width) - 1;
  for (auto upper : {0x7ffff000u, 0x80000000u, 0xfffff000u}) {
    for (int lower : {-2048, -1, 0, 2047}) {
      clear();
      pcs[0] = Pc(-8); pcs[1] = Pc(-4);
      if constexpr (lanes == 3) pcs[2] = 0;
      instructions[0] = upper | (1 << 7) | 0x17; // AUIPC
      instructions[1] = addi(2, 1, lower);
      read_blocked[2] = 1;
      expect(lanes);
      const auto sum = std::int64_t(std::int32_t(upper)) + lower;
      CHECK(operands[1].prs1 == pcs[0]);
      CHECK(issued_instructions[1].ppair.psources[0] == OlderPc);
      CHECK(operands[1].prewrite.pcomposed);
      CHECK(operands[1].prewrite.pimmediate == (std::uint64_t(sum) & sum_mask));
      // The same signed sum uses the split S-immediate encoding for stores.
      instructions[1] = ((unsigned(lower) & 0xfe0) << 20) | (11 << 20) |
                        (1 << 15) | 0x2023 | ((unsigned(lower) & 31) << 7);
      expect(lanes);
      CHECK(operands[1].prs1 == pcs[0]);
      CHECK(operands[1].prs2 == read_data[3]);
      CHECK(operands[1].prewrite.pcomposed);
      CHECK(operands[1].prewrite.pimmediate == (std::uint64_t(sum) & sum_mask));
    }
  }
  // Register-based address composition retains the older base and both offsets.
  for (int offset : {-2048, 2047}) {
    clear();
    instructions[0] = addi(1, 10, offset);
    instructions[1] = load(2, 1) | (unsigned(offset) << 20);
    read_blocked[2] = 1;
    expect(lanes);
    CHECK(operands[1].prs1 == read_data[0]);
    CHECK(issued_instructions[1].ppair.psources[0] == OlderRegister);
    CHECK(operands[1].prewrite.pcomposed);
    CHECK(operands[1].prewrite.pimmediate == (std::uint64_t(2 * offset) & sum_mask));
    read_blocked[0] = 1;
    expect(0); // A rewritten consumer does not make its producer ready.
    read_blocked[0] = 0;
    read_blocked[3] = 1;
    instructions[1] = ((unsigned(offset) & 0xfe0) << 20) | (1 << 20) |
                      (1 << 15) | 0x2023 | ((unsigned(offset) & 31) << 7);
    expect(lanes);
    CHECK(operands[1].prs1 == read_data[0]);
    CHECK(operands[1].prewrite.pcomposed);
    CHECK(operands[1].prewrite.pstore_udata);
    CHECK(operands[1].prewrite.pimmediate == (std::uint64_t(2 * offset) & sum_mask));
  }
  clear();
  instructions[0] = add(1, 10, 11);
  instructions[1] = load(2, 1);
  read_blocked[2] = 1;
  expect(lanes);
  CHECK(operands[1].prewrite.pmemory_uaddress);
  CHECK(!operands[1].prewrite.pcomposed);
  instructions[1] |= 4u << 20;
  expect(1); // No general older-result-plus-offset EX chain.
  CHECK(!operands[1].prewrite.pmemory_uaddress);

  clear();
  instructions[0] = add(1, 10, 11);
  instructions[1] = (1 << 20) | (12 << 15) | 0x2023; // SW x1,0(x12)
  read_blocked[3] = 1;
  expect(lanes);
  CHECK(operands[1].prs1 == read_data[2]);
  CHECK(operands[1].prewrite.pstore_udata);

  clear();
  instructions[0] = add(1, 10, 11);
  instructions[1] = (1 << 20) | (1 << 15) | 0x63; // BEQ x1,x1
  read_blocked[2] = read_blocked[3] = 1;
  expect(lanes);
  CHECK(operands[1].prewrite.pbranch_uoperands[0]);
  CHECK(operands[1].prewrite.pbranch_uoperands[1]);

  clear();
  instructions[0] = add(1, 10, 11);
  instructions[1] = 0x02004057 | (12 << 20) | (1 << 15) | (8 << 7); // VADD.VX v8,v12,x1
  read_blocked[2] = 1;
  expect(lanes);
  CHECK(operands[1].prewrite.pvector_uscalar);

  // Deferred, faulting, and x0 producers cannot provide an architectural pair result.
  for (auto older : {load(1), multiply(1), addi(0, 0, 7)}) {
    clear(); instructions[0] = older; instructions[1] = addi(2, 1);
    read_blocked[2] = 1; expect(1);
    CHECK(!operands[1].prewrite.pcomposed);
    CHECK(operands[1].prs1 == read_data[2]);
  }
  clear(); instructions[0] = addi(1, 0, 7); instructions[1] = addi(2, 1);
  faults[0] = 1; expect(1); CHECK(operands[1].prs1 == read_data[2]);

  if constexpr (lanes == 3) {
    // Resolving a dependency does not release the donor's physical read path.
    clear(); instructions[0] = addi(1, 0, 7); instructions[1] = add(2, 10, 1);
    instructions[2] = addi(0, 11); expect(2);
    CHECK(operands[1].prs2 == 7);
    CHECK(!reads.pgranted[2][0]);
  }
}

void check_issue_contract() {
    check_prepared_operands();
    clear();
    expect(lanes);
    // Exhaust destinations, source demand, and both physical write-port reservations.
    for (unsigned dest = 0; dest < (1u << (2 * lanes)); ++dest) {
      for (unsigned use = 0; use < (1u << (2 * lanes)); ++use) {
        for (unsigned free = 0; free < 4; ++free) {
          std::array<unsigned, lanes> destinations{}, uses{};
          clear();
          for (unsigned lane = 0; lane < lanes; ++lane) {
            destinations[lane] = (dest >> (2 * lane)) & 3;
            uses[lane] = (use >> (2 * lane)) & 3;
          }
          resources.pwrite_uavailable = {std::uint8_t(free & 1), std::uint8_t((free >> 1) & 1)};
          ordinary(destinations, uses);
        }
      }
    }
    // Every possible successful WB prefix, including a killed/deferred youngest writer.
    for (unsigned dest = 0; dest < (1u << (2 * lanes)); ++dest) {
      for (unsigned mask = 0; mask < (1u << lanes); ++mask) {
        for (unsigned eligible = 0; eligible < (1u << lanes); ++eligible) {
          for (unsigned free = 0; free < 4; ++free) {
            std::array<unsigned, lanes> destinations{};
            std::array<bool, lanes> writes{}, coalescible{};
            for (unsigned lane = 0; lane < lanes; ++lane) {
              destinations[lane] = (dest >> (2 * lane)) & 3;
              instructions[lane] = addi(destinations[lane]);
              wb_writes[lane] = writes[lane] = (mask >> lane) & 1;
              wb_coalescible[lane] = coalescible[lane] = (eligible >> lane) & 1;
            }
            wb_available = {std::uint8_t(free & 1), std::uint8_t((free >> 1) & 1)};
            eval();
            check_allocation(write_routing, allocate(destinations, writes, coalescible, {bool(free & 1), bool(free & 2)}));
          }
        }
      }
    }
    for (unsigned iteration = 0; iteration < 4000; ++iteration) {
      clear();
      std::array<unsigned, lanes> destinations{}, uses{};
      for (unsigned lane = 0; lane < lanes; ++lane) {
        destinations[lane] = random_value() & 7;
        uses[lane] = random_value() & 3;
        present[lane] = (random_value() & 7) != 0;
        status[lane].pdestination_ublocked = (random_value() & 7) == 0;
        status[lane].pexecution_ublocked = (random_value() & 15) == 0;
        status[lane].pfp_udata_ublocked = (random_value() & 15) == 0;
        status[lane].pfp_ucapacity_ublocked = (random_value() & 15) == 0;
        status[lane].pvector_ublocked = (random_value() & 15) == 0;
        status[lane].pcsr_uread_uwait = (random_value() & 15) == 0;
      }
      for (auto &blocked : read_blocked) blocked = (random_value() & 3) == 0;
      for (auto &free : resources.pwrite_uavailable) free = (random_value() & 3) != 0;
      resources.pstop = (random_value() & 15) == 0;
      resources.pretry_uwait = (random_value() & 15) == 0;
      resources.ppause_ublocked = (random_value() & 15) == 0;
      resources.pyounger_uslot_ureserved = (random_value() & 7) == 0;
      resources.pvector_uwaiting = (random_value() & 7) == 0;
      ordinary(destinations, uses);
    }

    // Existing first-pair constant forwarding waives physical readiness, not ownership.
    clear(); instructions[0] = addi(1, 0, 5); instructions[1] = addi(2, 1, 1);
    read_blocked[2] = 1;
    expect(lanes);
    CHECK(operands[1].prs1 == 5);
    // An ordinary unavailable producer cannot be bypassed by the younger instruction.
    instructions[0] = add(1, 10, 11); expect(1);
    clear(); instructions[0] = load(1); instructions[1] = addi(1); expect(1);
    clear(); instructions[0] = load(1); instructions[1] = load(2); expect(1);
    clear(); instructions[0] = multiply(1); instructions[1] = load(2); expect(1);
    resources.pmultiply_uavailable = 0; expect(0);

    clear(); instructions[0] = fadd(1, 10, 11); instructions[1] = fadd(2, 12, 13); expect(1);
    clear(); instructions[0] = vadd(1); instructions[1] = vadd(2); expect(1);
    clear(); instructions[0] = multiply(1); instructions[1] = fmv_x(2, 12); late_gpr[1] = 1;
    resources.pfixed_ureturns_ucollide = 1; expect(1);
    resources.pfixed_ureturns_ucollide = 0; expect(lanes);

    for (unsigned lane = 0; lane < lanes; ++lane) {
      clear(); illegal[lane] = 1; expect(lane == 0 ? 1 : lane);
      clear(); faults[lane] = 1; expect(lane + 1);
      clear(); instructions[lane] = 0x10500073; expect(lane == 0 ? 1 : lane); // WFI
      if (lane == 0) { resources.pserialization_uready = 0; expect(0); }
      clear(); instructions[lane] = 0x0100000f; expect(lane + 1); // PAUSE
      clear(); instructions[lane] = 0x300020f3; expect(lane == 0 ? 1 : lane); // CSRRS x1,mstatus,x0
      clear(); instructions[lane] = 0x100520af; // LR.W x1,(x10)
      resources.pmemory_uorder_uready = 0; expect(lane);
    }
    clear(); pcs[1] = 0x3000; expect(1);
    clear(); pcs[0] = Pc(-4); for (unsigned lane = 1; lane < lanes; ++lane) pcs[lane] = 4 * (lane - 1);
    expect(lanes); // Modular next-region crossing.
    clear(); instructions[0] = instructions[1] = 0x00000063; resources.ptraining_upending = 2; expect(1);
    resources.ptraining_upending = 1; expect(lanes);
    ras[0] = ras[1] = 1; expect(1);

}
