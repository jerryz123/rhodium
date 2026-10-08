// Sweeps PMM, effective privilege, MXR, translation, tags, and width
// specialization.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <random>
std::mt19937_64 rng(0x719bad);

int main() {
  return run_test([] {
    virtualized = 0;
    hstatus = 0;
    vsstatus = 0;
    vsatp = 0;
    henvcfg = 0;
    access = 0;
    for (int mode = 0; mode < 4; mode++)
      for (int priv = 0; priv < 4; priv++)
        for (int mpp = 0; mpp < 4; mpp++)
          for (int mprv = 0; mprv < 2; mprv++)
            for (int mxr = 0; mxr < 2; mxr++)
              for (int translated = 0; translated < 2; translated++)
                for (int sample_index = 0; sample_index < 12; sample_index++) {
                  int ep, length;
                  std::uint8_t expected_mode;
                  std::uint64_t expected;
                  privilege = ((priv)&low_mask(2));
                  mstatus = (((mpp)&low_mask(64)) << 11) |
                            (((mprv)&low_mask(64)) << 17) |
                            (((mxr)&low_mask(64)) << 19);
                  satp = translated != 0 ? UINT64_C(9223372036854775808) : 0;
                  senvcfg =
                      (((mode)&low_mask(64)) << 32) | UINT64_C(4294901760);

                  switch (sample_index) {
                  case 0:
                    address = UINT64_C(18302628887781179683);

                    break;
                  case 1:
                    address = UINT64_C(12321989317974032675);

                    break;
                  case 2:
                    address = UINT64_C(12357877377504641315);

                    break;
                  case 3:
                    address = UINT64_C(12321849130241491235);

                    break;
                  case 4:
                    address = UINT64_C(18446744073709551615);

                    break;
                  case 5:
                    address = 0;

                    break;
                  default:
                    address = rng();

                    break;
                  }
                  ep = priv == 3 && mprv != 0 ? (mpp == 0 || mpp == 1 ? mpp : 3)
                                              : priv;
                  expected_mode = ep == 0 && mxr == 0 && mode != 1
                                      ? ((mode)&low_mask(2))
                                      : 0;
                  length = expected_mode == 2 ? 7 : expected_mode == 3 ? 16 : 0;
                  expected = address;
                  if (length != 0) {
                    expected =
                        address & (UINT64_C(18446744073709551615) >> length);
                    if (translated != 0 && ((address >> (63 - length)) & 1))
                      expected |= UINT64_C(18446744073709551615)
                                  << (64 - length);
                  }
                  eval();
                  CHECK(effective == ((ep)&low_mask(2)) &&
                        policy.pmode == expected_mode &&
                        policy.pvirtual_uaddress ==
                            (translated != 0 && ep != 3));
                  CHECK(masked == expected &&
                        slice(masked, 47, 0) == slice(address, 47, 0));
                  CHECK(warl == (mode == 1 ? 0 : ((mode)&low_mask(64)) << 32));
                  CHECK(disabled_warl == 0 && disabled_address == address &&
                        rv32_warl == 0 &&
                        rv32_address == slice(address, 31, 0));
                }

    for (int modes = 0; modes < 64; modes++)
      for (int a = 0; a < 3; a++)
        for (int ctx = 0; ctx < 64; ctx++)
          for (int translation = 0; translation < 4; translation++)
            for (int mxrs = 0; mxrs < 4; mxrs++) {
              int ep, pm, length;
              bool guest, translated;
              std::uint64_t expected;
              privilege = slice(ctx, 1, 0) == 2 ? 3 : ((ctx)&low_mask(2));
              virtualized = ((ctx >> (2)) & 1) && privilege != 3;
              mstatus = (((((ctx >> (3)) & 1)) & low_mask(64)) << 17) |
                        (((((ctx >> (4)) & 1)) & low_mask(64)) << 39) |
                        (((((ctx >> (5)) & 1) ? 1 : 0) & low_mask(64)) << 11) |
                        (((mxrs & 1) & low_mask(64)) << 19);
              hstatus = (((slice(modes, 5, 4)) & low_mask(64)) << 48) |
                        (((((ctx >> (5)) & 1)) & low_mask(64)) << 8);
              senvcfg = ((slice(modes, 1, 0)) & low_mask(64)) << 32;
              henvcfg = ((slice(modes, 3, 2)) & low_mask(64)) << 32;
              vsstatus = ((mxrs >> 1) & low_mask(64)) << 19;
              satp = ((translation & 1) & low_mask(64)) << 63;
              vsatp = ((translation >> 1) & low_mask(64)) << 63;
              access = ((a)&low_mask(2));
              address = ((ctx >> (0)) & 1) ? UINT64_C(12393765438108995875)
                                           : UINT64_C(18302628886707441955);
              ep = a != 0 ? (((ctx >> (5)) & 1) ? 1 : 0)
                   : privilege == 3 && ((ctx >> (3)) & 1)
                       ? (((ctx >> (5)) & 1) ? 1 : 0)
                       : int(privilege);
              guest = a != 0 || (privilege == 3 && ((ctx >> (3)) & 1)
                                     ? ((ctx >> (4)) & 1) && ep != 3
                                     : virtualized);
              pm = ep == 0 ? (a != 0 && privilege == 0 ? (modes >> 4) & 3
                                                       : modes & 3)
                   : guest && ep == 1 ? (modes >> 2) & 3
                                      : 0;
              if (pm == 1 || ((mxrs >> (0)) & 1) ||
                  (guest && ((mxrs >> (1)) & 1)) || a == 2)
                pm = 0;
              translated = ep != 3 && (guest ? (vsatp >> 63) : (satp >> 63));
              length = pm == 2 ? 7 : pm == 3 ? 16 : 0;
              expected = address & (~UINT64_C(0) >> length);
              if (length != 0 && translated && ((address >> (63 - length)) & 1))
                expected |= ~UINT64_C(0) << (64 - length);
              eval();
              CHECK(guest_policy.pmode == pm &&
                    guest_policy.pvirtual_uaddress == translated &&
                    guest_masked == expected);
              CHECK(hstatus_warl ==
                    (slice(modes, 5, 4) == 1
                         ? 0
                         : ((slice(modes, 5, 4)) & low_mask(64)) << 48));
            }
  });
}
