// SPDX-License-Identifier: Apache-2.0
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
constexpr unsigned codes[] = {11, 3, 7, 9, 1, 5, 13};
int checks{};

void check_interrupt() {
  bool found, global_enable;
  int expected_cause, expected_target;
  found = 0;
  expected_cause = 0;
  expected_target = 0;

  for (int dest = 0; dest < 2; dest++) {
    global_enable = dest == 0 ? (privilege != 3 || ((mstatus >> 3) & 1))
                              : (privilege == 0 ||
                                 (privilege == 1 && ((mstatus >> 1) & 1)));
    for (int index = 0; index < 7; index++) {
      if (!found && ((pending >> codes[index]) & 1) &&
          ((enabled >> codes[index]) & 1) &&
          ((delegated >> codes[index]) & 1) == ((dest)&low_mask(1)) &&
          global_enable) {
        found = 1;
        expected_cause = codes[index];
        expected_target = dest;
      }
    }
  }
  eval();
  CHECK(valid32 == found && valid64 == found);
  if (found) {
    CHECK(cause32 == ((expected_cause)&low_mask(4)) &&
          cause64 == ((expected_cause)&low_mask(4)) &&
          target32 == ((expected_target)&low_mask(2)) &&
          target64 == ((expected_target)&low_mask(2)));
  }
  checks++;
}

int main() {
  return run_test([] {
    pending = 0;
    enabled = 0;
    delegated = 0;
    mstatus = 0;
    privilege = 0;
    vtype = 0;

    for (int first = 0; first < 7; first++) {
      for (int second = first; second < 7; second++) {
        pending =
            (UINT64_C(1) << codes[first]) | (UINT64_C(1) << codes[second]);
        for (int route = 0; route < 4; route++) {
          delegated = (((route >> (0)) & 1) ? UINT64_C(1) << codes[first] : 0) |
                      (((route >> (1)) & 1) ? UINT64_C(1) << codes[second] : 0);
          for (int mask = 0; mask < 4; mask++) {
            enabled = (((mask >> (0)) & 1) ? UINT64_C(1) << codes[first] : 0) |
                      (((mask >> (1)) & 1) ? UINT64_C(1) << codes[second] : 0);
            for (int mode = 0; mode < 3; mode++) {
              privilege = mode == 2 ? UINT64_C(3) : ((mode)&low_mask(2));
              for (int gates = 0; gates < 4; gates++) {
                mstatus = (((gates >> (0)) & 1) ? UINT64_C(8) : 0) |
                          (((gates >> (1)) & 1) ? UINT64_C(2) : 0);
                check_interrupt();
              }
            }
          }
        }
      }
    }
    pending = UINT64_MAX;
    enabled = UINT64_MAX;
    delegated = 0;
    privilege = 0;
    check_interrupt();
    pending = UINT64_C(1) << 63;
    check_interrupt();
    for (int raw = 0; raw < 256; raw++) {
      int sew, lm, maximum32, maximum64;
      bool reserved;
      vtype = ((raw)&low_mask(32));
      sew = (raw >> 3) & 7;
      lm = raw & 7;
      reserved = lm == 4 || sew > 3;
      if (lm >= 4)
        lm -= 8;
      maximum32 = reserved || sew > 2 || sew > lm + 2
                      ? 0
                      : (lm >= 0 ? (128 / (8 << sew)) << lm
                                 : (128 / (8 << sew)) >> -lm);
      maximum64 = reserved || sew > lm + 3
                      ? 0
                      : (lm >= 0 ? (128 / (8 << sew)) << lm
                                 : (128 / (8 << sew)) >> -lm);
      eval();
      CHECK(vill32 == (maximum32 == 0) &&
            vlmax32 == ((maximum32)&low_mask(8)) &&
            vill64 == (maximum64 == 0) && vlmax64 == ((maximum64)&low_mask(8)));
      checks++;
    }
  });
}
