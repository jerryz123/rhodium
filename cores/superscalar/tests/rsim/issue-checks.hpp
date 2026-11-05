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
unsigned normalize_move(unsigned instruction) {
  const auto rd = (instruction >> 7) & 31, a = (instruction >> 15) & 31, b = (instruction >> 20) & 31;
  if (rd && (instruction & 0xfe00707f) == 0x33 && (!a || !b)) return addi(rd, a ? a : b);
  return instruction;
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
  read_blocked = {};
  for (auto &data : read_data) data = random_value();
  faults = {};
  illegal = {};
  ras = {};
  late_gpr = {};
  for (unsigned lane = 0; lane < lanes; ++lane) {
    instructions[lane] = addi(0);
    pcs[lane] = 0x1000 + 4 * lane;
    present[lane] = 1;
  }
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
    CHECK(issued_instructions[lane].pinstruction == normalize_move(instructions[lane]));
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
    const auto normalized = normalize_move(instructions[lane]);
    needed[lane] = {bool(present[lane] && ((normalized >> 15) & 31)), bool(present[lane] && (normalized & 127) == 0x33 && ((normalized >> 20) & 31))};
    if (lane < 2) {
      addresses[2 * lane] = (normalize_move(instructions[lane]) >> 15) & 31;
      addresses[2 * lane + 1] = (normalize_move(instructions[lane]) >> 20) & 31;
    }
  }
  std::array<unsigned, 2> third_ports{3, 1};
  if constexpr (lanes == 3) {
    for (unsigned source = 0; source < 2; ++source) {
      if (!needed[2][source]) continue;
      auto &port = third_ports[source];
      if (!needed[2][1 - source] && needed[port / 2][1] && !needed[(4 - port) / 2][1]) port = 4 - port;
      if (!needed[port / 2][1]) addresses[port] = (normalize_move(instructions[2]) >> (source ? 20 : 15)) & 31;
    }
  }
  unsigned count = 0;
  if (!resources.pstop && !resources.pretry_uwait && !resources.ppause_ublocked) {
    for (unsigned lane = 0; lane < lanes; ++lane) {
      if (!present[lane]) break;
      if (status[lane].pexecution_ublocked || status[lane].pdestination_ublocked || status[lane].pfp_udata_ublocked ||
          status[lane].pfp_ucapacity_ublocked || status[lane].pvector_ublocked || status[lane].pcsr_uread_uwait) break;
      if (lane == lanes - 1 && (resources.plast_uslot_ureserved || resources.pvector_uwaiting)) break;
      bool blocked = false;
      for (unsigned source = 0; source < 2; ++source) {
        const unsigned port = lane < 2 ? 2 * lane + source : third_ports[source];
        const bool granted = lane < 2 || !needed[lane][source] || !needed[port / 2][1];
        blocked |= needed[lane][source] && (!granted || read_blocked[port]);
      }
      if (blocked) break;
      ++count;
    }
  }
  expect(count);
  for (unsigned port = 0; port < 4; ++port) CHECK(reads.paddresses[port] == addresses[port]);
  // Prepared ordinary operands follow the four selected paths, independent of admission.
  for (unsigned lane = 0; lane < lanes; ++lane) {
    CHECK(issued_instructions[lane].pcontrol.poperand.puses_urs1 == (uses[lane] != 0));
    CHECK(issued_instructions[lane].pcontrol.poperand.puses_urs2 == (uses[lane] == 3 || (uses[lane] == 2 && !destinations[lane])));
    CHECK(issued_instructions[lane].ppair.psources[0] == Register);
    CHECK(issued_instructions[lane].ppair.psources[1] == Register);
    for (unsigned source = 0; source < 2; ++source) {
      const unsigned port = lane < 2 ? 2 * lane + source : third_ports[source];
      const bool granted = lane < 2 || !needed[lane][source] || !needed[port / 2][1];
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
    CHECK(issued_instructions[1].ppair.psources[0] == (value ? OlderConstant : Register));
    CHECK(issued_instructions[1].ppair.psources[1] == (value ? OlderConstant : Register));
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
    instructions[2] = addi(0, 11); expect(3);
    CHECK(operands[1].prs2 == 7);
    CHECK(reads.pgranted[2][0] && reads.pports[2][0] == 1);
    CHECK(reads.paddresses[3] == 1); // Pair rewriting did not free its path.
  }
}

void check_move_sources() {
  // Exhaust source/destination fields, including x0; only exact XLEN copies normalize.
  for (unsigned rd = 0; rd < 32; ++rd) for (unsigned source = 0; source < 32; ++source) {
    for (auto instruction : {add(rd, 0, source), add(rd, source, 0), addi(rd, source)}) {
      clear(); instructions[0] = instruction; expect(lanes);
      const auto normalized = normalize_move(instruction);
      CHECK(issued_instructions[0].pinstruction == normalized);
      CHECK(reads.paddresses[0] == ((normalized >> 15) & 31));
      if (rd) CHECK(!reads.pneeded[0][1]);
      CHECK(operands[0].prs1 == (((normalized >> 15) & 31) ? read_data[0] : 0));
    }
  }
  for (unsigned form = 0; form < 3; ++form) {
    const auto move = form == 0 ? addi(1, 10) : form == 1 ? add(1, 0, 10) : add(1, 10, 0);
    const unsigned port = 0;
    clear(); instructions[0] = move; instructions[1] = add(2, 1, 1);
    read_blocked[2] = read_blocked[3] = 1;
    expect(lanes);
    CHECK(operands[1].prs1 == read_data[port]);
    CHECK(operands[1].prs2 == read_data[port]);
    CHECK(reads.pforwarded[1][0] && reads.pforwarded[1][1]);
    read_blocked[port] = 1; expect(0);
    read_blocked[port] = 0;
    instructions[1] = load(2, 1) | (20 << 20); expect(lanes);
    CHECK(operands[1].prs1 == read_data[port]);
    CHECK(!operands[1].prewrite.pmemory_uaddress && !operands[1].prewrite.pcomposed);
    instructions[1] = (1 << 20) | (1 << 15) | 0x2023; expect(lanes); // SW x1,0(x1)
    CHECK(operands[1].prs1 == read_data[port] && operands[1].prs2 == read_data[port]);
    CHECK(!operands[1].prewrite.pstore_udata);

    // A malformed or faulted producer cannot authorize a move route.
    illegal[0] = 1; expect(1); CHECK(!reads.pforwarded[1][0]);
    illegal[0] = 0; faults[0] = 1; expect(1); CHECK(!reads.pforwarded[1][0]);
    if constexpr (lanes == 3) {
      clear(); instructions[0] = move; instructions[2] = add(2, 1, 1);
      expect(3);
      CHECK(operands[2].prs1 == read_data[port] && operands[2].prs2 == read_data[port]);
      CHECK(reads.pforwarded[2][0] && reads.pforwarded[2][1]);
      read_blocked[port] = 1; expect(0);

      clear(); instructions[1] = move; instructions[2] = add(2, 1, 1);
      expect(3);
      CHECK(operands[2].prs1 == read_data[port + 2] && operands[2].prs2 == read_data[port + 2]);
      read_blocked[port + 2] = 1; expect(1);
    }
  }
  if constexpr (sizeof(Pc) == 8) {
    clear(); instructions[0] = addi(1, 10) | 8; // ADDIW is not a full-width copy.
    instructions[1] = add(2, 1, 1); expect(1); CHECK(!reads.pforwarded[1][0]);
    clear(); instructions[0] = add(1, 0, 10) | 8; expect(lanes); // ADDW must sign extend.
    CHECK(issued_instructions[0].pinstruction == instructions[0] && reads.pneeded[0][1]);
  }
  clear(); instructions[0] = add(1, 0, 10) | 0x40000000; expect(lanes); // SUB is negation, not a copy.
  CHECK(issued_instructions[0].pinstruction == instructions[0] && reads.pneeded[0][1]);
  clear(); instructions[0] = addi(0, 10); instructions[1] = add(2, 0, 0);
  expect(lanes); CHECK(!reads.pforwarded[1][0]); CHECK(operands[1].prs1 == 0);
  if constexpr (lanes == 3) {
    // Chains share the original captured operand but retain all architectural writers.
    clear(); instructions[0] = addi(1, 10); instructions[1] = add(2, 0, 1); instructions[2] = addi(1, 2, 5);
    expect(3); CHECK(operands[1].prs1 == read_data[0]); CHECK(operands[2].prs1 == read_data[0]);
    instructions[2] = addi(3, 2, 5); expect(3); // Each lane has its own physical write port.
    clear(); instructions[0] = addi(1, 10); instructions[1] = addi(10, 11, 1); instructions[2] = addi(1, 1, 2);
    expect(3); CHECK(operands[2].prs1 == read_data[0]); // Not the intervening replacement of x10.
    clear(); instructions[0] = addi(1, 10); instructions[1] = add(1, 11, 12); instructions[2] = addi(0, 1, 1);
    expect(2); CHECK(!reads.pforwarded[2][0]); // Nearest non-move writer shadows the move.
    clear(); instructions[0] = addi(1, 10); instructions[1] = addi(1, 11); instructions[2] = addi(0, 1, 1);
    expect(3); CHECK(operands[2].prs1 == read_data[2]); // Nearest move wins.
    clear(); instructions[0] = load(1); instructions[1] = addi(2, 1); instructions[2] = addi(0, 2, 1);
    expect(1); CHECK(!reads.pforwarded[2][0]); // No laundering an unavailable load through a move.
    clear(); instructions[0] = addi(1); instructions[1] = add(2, 0, 1); instructions[2] = addi(1, 2, 1);
    read_blocked = {1, 1, 1, 1}; expect(3); CHECK(operands[2].prs1 == 0);
  }
}

void check_issue_contract() {
    check_move_sources();
    check_prepared_operands();
    clear();
    expect(lanes);
    // Exhaust destinations, source demand, and the shared last-port reservation.
    for (unsigned dest = 0; dest < (1u << (2 * lanes)); ++dest) {
      for (unsigned use = 0; use < (1u << (2 * lanes)); ++use) {
        for (unsigned reserved = 0; reserved < 2; ++reserved) {
          std::array<unsigned, lanes> destinations{}, uses{};
          clear();
          for (unsigned lane = 0; lane < lanes; ++lane) {
            destinations[lane] = (dest >> (2 * lane)) & 3;
            uses[lane] = (use >> (2 * lane)) & 3;
          }
          resources.plast_uslot_ureserved = reserved;
          ordinary(destinations, uses);
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
      resources.pstop = (random_value() & 15) == 0;
      resources.pretry_uwait = (random_value() & 15) == 0;
      resources.ppause_ublocked = (random_value() & 15) == 0;
      resources.plast_uslot_ureserved = (random_value() & 7) == 0;
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
