// SPDX-License-Identifier: Apache-2.0
#include "../../../superscalar/tests/rsim/issue-checks.hpp"

int main() {
  return run_test([] {
    check_issue_contract();
    // CoreMark's sh1add.uw / zero-offset load / sh2add.uw group shares a5
    // through lane 1 rs1 and gives s3 the unused lane 2 rs2 path.
    clear();
    instructions[0] = (0x10u << 25) | (17u << 20) | (15u << 15) | (2u << 12) | (14u << 7) | (sizeof(Pc) == 8 ? 0x3b : 0x33);
    instructions[1] = load(14, 14);
    instructions[2] = (0x10u << 25) | (19u << 20) | (15u << 15) | (4u << 12) | (13u << 7) | (sizeof(Pc) == 8 ? 0x3b : 0x33);
    expect(3);
    CHECK(reads.pports[2][0] == 0 && reads.pports[2][1] == 3);
    CHECK(operands[2].prs1 == read_data[0] && operands[2].prs2 == read_data[3]);
    CHECK(operands[1].prewrite.pmemory_uaddress);
    CHECK(!decision.pdata_uhazard[2] && !decision.pstructural_uhazard[2]);
    read_blocked[3] = 1; expect(2);
    CHECK(decision.pdata_uhazard[2] && !decision.pstructural_uhazard[2]);
    read_blocked[3] = 0; read_blocked[0] = 1; expect(0);
    // A matching older source is not permission to ignore an intervening write.
    clear(); instructions[0] = add(1, 10, 11); instructions[1] = addi(10, 12, 1);
    instructions[2] = add(3, 10, 13); expect(2);
    CHECK(reads.pports[2][0] == 0 && !reads.pforwarded[2][0] && decision.pdata_uhazard[2]);
    // Both younger operands may share one newly allocated donor as well.
    clear(); instructions[0] = add(1, 10, 11); instructions[1] = addi(2, 12, 1);
    instructions[2] = add(3, 13, 13); expect(3);
    CHECK(reads.pports[2][0] == 3 && reads.pports[2][1] == 3);
    clear(); instructions[0] = addi(1); instructions[1] = addi(2); instructions[2] = addi(3); expect(3);
    // A return owns only the last port, leaving two independent writers available.
    resources.plast_uslot_ureserved = 1; expect(2);
    CHECK(!decision.pstructural_uhazard[1] && decision.pstructural_uhazard[2]);
    resources.plast_uslot_ureserved = 0; resources.pvector_uwaiting = 1; expect(2);
    resources.pvector_uwaiting = 0;
    instructions[1] = instructions[2] = addi(1); expect(3);
    clear(); instructions[0] = addi(1, 10, 1); instructions[2] = addi(2, 1); expect(2);
    clear(); instructions[1] = addi(1, 10, 1); instructions[2] = addi(2, 1); expect(2);
    clear(); instructions[0] = load(1); instructions[2] = load(2); expect(2);
    clear(); instructions[0] = fadd(1, 10, 11); instructions[2] = fmv_x(2, 1); expect(2);
    // CoreMark's li / bge / load group borrows the unused first-lane rs2 path.
    clear(); instructions[0] = addi(18); instructions[1] = (21 << 20) | (18 << 15) | 0x5063;
    instructions[2] = load(15, 15); expect(3);
    CHECK(reads.pports[2][0] == 1 && operands[2].prs1 == read_data[1]);
    CHECK(!decision.pdata_uhazard[2] && !decision.pstructural_uhazard[2]);
    read_blocked[1] = 1; expect(2);
    CHECK(decision.pdata_uhazard[2] && !decision.pstructural_uhazard[2]);
    // With both donors occupied, allocation denial is structural, not data.
    clear(); instructions[0] = add(0, 10, 11); instructions[1] = add(2, 12, 13);
    instructions[2] = load(15, 15); expect(2);
    CHECK(!decision.pdata_uhazard[2] && decision.pstructural_uhazard[2]);
    // The symmetric rs2 fallback preserves its value and readiness.
    clear(); instructions[0] = add(0, 10, 11); instructions[1] = addi(2);
    instructions[2] = (12 << 20) | 0x2023; expect(3); // SW x12,0(x0) has only an rs2 demand.
    CHECK(reads.pports[2][1] == 3 && operands[2].prs2 == read_data[3]);
    read_blocked[3] = 1; expect(2);
    CHECK(decision.pdata_uhazard[2] && !decision.pstructural_uhazard[2]);
    // Readiness never reallocates an operand when both donors are free.
    clear(); instructions[2] = load(15, 15); read_blocked[3] = 1; expect(2);
    CHECK(reads.pports[2][0] == 3 && decision.pdata_uhazard[2] && !decision.pstructural_uhazard[2]);
    // Expanded C.MV / C.J / ADDW now leaves both donors available.
    clear(); instructions[0] = add(12, 0, 13); instructions[1] = 0x0080006f;
    instructions[2] = add(13, 14, 17) | (sizeof(Pc) == 8 ? 8 : 0); expect(3);
    CHECK(issued_instructions[0].pinstruction == addi(12, 13));
    CHECK(reads.paddresses[0] == 13 && reads.paddresses[1] == 17 && reads.paddresses[3] == 14);
    CHECK(operands[0].prs1 == read_data[0] && operands[2].prs1 == read_data[3] && operands[2].prs2 == read_data[1]);
    for (unsigned port : {0u, 1u, 3u}) {
      read_blocked[port] = 1; expect(port == 0 ? 0 : 2); read_blocked[port] = 0;
    }
    // Adjacent ALU -> zero-offset load bypasses address capture even when its
    // destination aliases either older writer; WB resolves actual writes.
    for (unsigned destination : {13u, 14u}) {
      clear(); instructions[0] = addi(13, 14); instructions[1] = add(14, 15, 19);
      instructions[2] = load(destination, 14); read_blocked[1] = 1; expect(3);
      CHECK(operands[2].prewrite.pmemory_uaddress);
      CHECK(!decision.pdata_uhazard[2] && !decision.pstructural_uhazard[2]);
      read_blocked[2] = 1; expect(1); // Producer's own operands still interlock.
      read_blocked[2] = 0; instructions[2] |= 4u << 20; expect(2);
      CHECK(!operands[2].prewrite.pmemory_uaddress); // Nonzero displacement adds a dependency.
    }
    // No free address read path is necessary when the adjacent ALU supplies it.
    clear(); instructions[0] = add(0, 10, 11); instructions[1] = add(14, 15, 19);
    instructions[2] = load(14, 14); expect(3);
    CHECK(!reads.pgranted[2][0] && operands[2].prewrite.pmemory_uaddress);
    // The nearest writer supersedes an older same-register producer for rs1.
    clear(); instructions[0] = addi(14, 10); instructions[1] = add(14, 15, 19);
    instructions[2] = load(14, 14); expect(3);
    // Load destinations do not impose physical write capacity constraints.
    clear(); instructions[0] = addi(13); instructions[1] = addi(14);
    instructions[2] = load(14, 15); expect(3); CHECK(!operands[2].prewrite.pmemory_uaddress);
    instructions[2] = load(16, 15); expect(3);
    // A middle load can defer while both independent fixed ports still write.
    clear(); instructions[0] = addi(13); instructions[1] = load(13, 15);
    instructions[2] = addi(14); expect(3);
    CHECK(!decision.pstructural_uhazard[2]);
    instructions[2] = addi(13); expect(2); // Deferred WAW still orders a younger overwrite.
    // Store data remains independently captured; address bypass cannot waive rs2.
    clear(); instructions[0] = addi(13, 10); instructions[1] = add(14, 15, 19);
    instructions[2] = (12u << 20) | (14u << 15) | 0x2023; expect(3);
    CHECK(operands[2].prewrite.pmemory_uaddress && operands[2].prs2 == read_data[1]);
    read_blocked[1] = 1; expect(2);
    read_blocked[1] = 0; instructions[2] = (14u << 20) | (14u << 15) | 0x2023; expect(2);
    // Deferred ALU substitutes are not eligible address producers.
    clear(); instructions[1] = multiply(14); instructions[2] = load(14, 14); expect(2);
    CHECK(!operands[2].prewrite.pmemory_uaddress);
  });
}
