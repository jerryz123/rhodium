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
  });
}
