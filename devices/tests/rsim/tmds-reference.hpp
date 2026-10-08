// Computes independent TMDS symbols and running disparity for encoder and
// scanout tests.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <bit>
#include <cstdint>
inline unsigned tmds_reference(std::uint8_t value, unsigned controls,
                               bool active, int &disparity) {
  if (!active) {
    disparity = 0;
    constexpr unsigned codes[] = {0x354, 0xab, 0x154, 0x2ab};
    return codes[controls];
  }
  const unsigned population = std::popcount(value);
  bool xnor_step = population > 4 || (population == 4 && !(value & 1));
  unsigned q = value & 1;
  for (unsigned i = 1; i < 8; ++i)
    q |= ((((q >> (i - 1)) ^ (value >> i)) & 1) ^ unsigned(xnor_step)) << i;
  const unsigned flag = !xnor_step;
  const unsigned ones = std::popcount(q);
  unsigned result;
  if (disparity == 0 || ones == 4)
    result = ((!flag) << 9) | (flag << 8) | (flag ? q : (q ^ 255));
  else if ((disparity > 0 && ones > 4) || (disparity < 0 && ones < 4))
    result = 512 | (flag << 8) | (q ^ 255);
  else
    result = (flag << 8) | q;
  disparity += 2 * int(std::popcount(result)) - 10;
  return result;
}
