// Exhaustively checks host/guest CMO controls, trap priority, WARL, Sv39
// permissions, and PMAs.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
bool expected_permitted{};
bool expected_flush{};
bool expected_translation{};
bool privilege_ok{};
bool access_ok{};
std::uint64_t expected_fields{};

int main() {
  return run_test([] {
    operation = 0;
    access = 0;
    user_mode = 0;
    supervisor_mode = 0;
    sum = 0;
    mxr = 0;
    menvcfg = 0;
    senvcfg = 0;
    henvcfg = 0;
    raw_pte = 0;
    virtualized = 0;
    user_mode = 0, supervisor_mode = 0, sum = 0, mxr = 0;
    virtualized = 0;
    physical = {};
    for (int privilege = 0; privilege < 3; privilege++) {
      user_mode = privilege == 0;
      supervisor_mode = privilege == 1;
      for (int m = 0; m < 16; m++) {
        for (int s = 0; s < 16; s++) {

          menvcfg = UINT64_C(18446744073709551375) | (((m)&low_mask(64)) << 4);
          senvcfg = UINT64_C(18446744073709551375) | (((s)&low_mask(64)) << 4);
          expected_fields = ((m)&low_mask(64)) << 4;
          if ((m & 3) == 2)
            expected_fields &= ~UINT64_C(48);
          for (int op = 0; op < 3; op++) {
            operation = ((op)&low_mask(2));
            if (op == 0) {
              expected_permitted = (privilege == 2 || (m & 1) != 0) &&
                                   (privilege != 0 || (s & 1) != 0);
              expected_flush = (privilege != 2 && (m & 3) == 1) ||
                               (privilege == 0 && (s & 3) == 1);
            } else {
              expected_permitted = (privilege == 2 || (m & 4) != 0) &&
                                   (privilege != 0 || (s & 4) != 0);
              expected_flush = 0;
            }
            eval();
            CHECK(permission64.paccess == (expected_permitted ? 0 : 1) &&
                  permission32.paccess == permission64.paccess &&
                  permission32.poperation == permission64.poperation);
            CHECK(permission64.poperation ==
                  ((expected_flush ? 2 : op) & low_mask(2)));
            CHECK(disabled.paccess == 1 && !zero_disabled);
            CHECK(fields64 == expected_fields &&
                  fields32 == ((expected_fields)&low_mask(32)));
            CHECK(fields_management == (expected_fields & UINT64_C(112)) &&
                  fields_zero == (expected_fields & UINT64_C(128)) &&
                  fields_disabled == 0);
            CHECK(zero_permitted == ((privilege == 2 || (m & 8) != 0) &&
                                     (privilege != 0 || (s & 8) != 0)));
          }
        }
      }
      for (int pte = 0; pte < 256; pte++) {
        raw_pte = ((pte)&low_mask(64));
        for (int options = 0; options < 4; options++) {
          sum = ((options >> (0)) & 1);
          mxr = ((options >> (1)) & 1);
          for (int kind = 0; kind < 4; kind++) {
            access = ((kind)&low_mask(2));
            privilege_ok = user_mode
                               ? ((pte >> (4)) & 1)
                               : !((pte >> (4)) & 1) ||
                                     (supervisor_mode && sum && kind != 0);
            switch (kind) {
            case 0:
              access_ok = ((pte >> (3)) & 1);

              break;
            case 1:
              access_ok = ((pte >> (1)) & 1) || (mxr && ((pte >> (3)) & 1));

              break;
            case 2:
              access_ok = ((pte >> (2)) & 1) && ((pte >> (7)) & 1);

              break;
            case 3:
              access_ok = ((pte >> (1)) & 1) || ((pte >> (2)) & 1) ||
                          (mxr && ((pte >> (3)) & 1));

              break;
            }
            expected_translation =
                privilege_ok && access_ok && ((pte >> (6)) & 1);
            eval();
            CHECK(translation_permitted == expected_translation);
          }
        }
      }
    }
    for (int mode = 0; mode < 5; mode++) {
      user_mode = mode == 0 || mode == 3;
      supervisor_mode = mode == 1 || mode == 4;
      virtualized = mode >= 3;
      for (int m = 0; m < 16; m++)
        for (int h = 0; h < 16; h++)
          for (int s = 0; s < 16; s++) {
            menvcfg = ((m)&low_mask(64)) << 4;
            henvcfg = ((h)&low_mask(64)) << 4;
            senvcfg = ((s)&low_mask(64)) << 4;
            for (int op = 0; op < 3; op++) {
              bool me, he, se, illegal, virt, flush;
              operation = ((op)&low_mask(2));
              me = op == 0 ? (m & 1) != 0 : (m & 4) != 0;
              he = op == 0 ? (h & 1) != 0 : (h & 4) != 0;
              se = op == 0 ? (s & 1) != 0 : (s & 4) != 0;
              illegal = (mode != 2 && !me) || (mode == 0 && !se);
              virt = virtualized && (!he || (mode == 3 && !se));
              flush = op == 0 && ((mode != 2 && (m & 3) == 1) ||
                                  (user_mode && (s & 3) == 1) ||
                                  (virtualized && (h & 3) == 1));
              eval();
              CHECK(guest_permission.paccess == (illegal ? 1
                                                 : virt  ? 2
                                                         : 0) &&
                    guest_permission.poperation ==
                        ((flush ? 2 : op) & low_mask(2)));
              illegal =
                  (mode != 2 && (m & 8) == 0) || (mode == 0 && (s & 8) == 0);
              virt =
                  virtualized && ((h & 8) == 0 || (mode == 3 && (s & 8) == 0));
              CHECK(guest_zero_access == (illegal ? 1 : virt ? 2 : 0));
            }
          }
    }

    for (int attrs = 0; attrs < 512; attrs++) {
      physical = {
          std::uint8_t((attrs >> 8) & 1), std::uint8_t((attrs >> 7) & 1),
          std::uint8_t((attrs >> 6) & 1), std::uint8_t((attrs >> 5) & 1),
          std::uint8_t((attrs >> 4) & 1), std::uint8_t((attrs >> 3) & 1),
          std::uint8_t((attrs >> 2) & 1), std::uint8_t((attrs >> 1) & 1),
          std::uint8_t((attrs >> 0) & 1), 0};
      eval();
      CHECK(physical_permitted ==
            (physical.pmapped && (physical.preadable || physical.pwritable)));
    }
  });
}
