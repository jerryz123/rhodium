// SPDX-License-Identifier: Apache-2.0
#include "../../../superscalar/tests/rsim/issue-checks.hpp"

int main() {
  return run_test([] {
    check_issue_contract();
    clear(); instructions[0] = addi(1); instructions[1] = addi(2); instructions[2] = addi(3); expect(2);
    instructions[0] = addi(0); expect(3); CHECK(decision.pwrites.pports[2] == 0);
    instructions[0] = addi(1); instructions[1] = addi(0); expect(3); CHECK(decision.pwrites.pports[2] == 1);
    instructions[1] = addi(2); instructions[2] = addi(1); expect(3); CHECK(!decision.pwrites.pwrites[0]);
    // The successful prefix ending at lane 2 still needs port 1, even if all three write x1.
    instructions[1] = addi(1); expect(3); CHECK(decision.preserve[0] && decision.preserve[1]);
    resources.pwrite_uavailable[1] = 0; expect(1);
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
    instructions[2] = add(3, 0, 12); expect(3);
    CHECK(reads.pports[2][1] == 3 && operands[2].prs2 == read_data[3]);
    read_blocked[3] = 1; expect(2);
    CHECK(decision.pdata_uhazard[2] && !decision.pstructural_uhazard[2]);
    // Readiness never reallocates an operand when both donors are free.
    clear(); instructions[2] = load(15, 15); read_blocked[3] = 1; expect(2);
    CHECK(reads.pports[2][0] == 3 && decision.pdata_uhazard[2] && !decision.pstructural_uhazard[2]);
  });
}
