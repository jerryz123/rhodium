// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#ifndef PREDICTED_TARGET_WIDTH
#define PREDICTED_TARGET_WIDTH 64
#endif
inline std::uint64_t expected_target(std::uint64_t address) {
#if PREDICTED_TARGET_WIDTH < 64
  constexpr auto sign = UINT64_C(1) << (PREDICTED_TARGET_WIDTH - 1);
  return ((address & (2 * sign - 1)) ^ sign) - sign;
#else
  return address;
#endif
}
