// Converts wide model ports to independent 128-bit test-oracle values without
// dropping high bits.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "test.hpp"
#include "rsim-bits.hpp"
using uint128 = unsigned __int128;
inline rhodium_rsim::WideBits<128> wide(uint128 value) {
  rhodium_rsim::WideBits<128> result;
  for (unsigned i = 0; i < 4; ++i)
    result.words[i] = std::uint32_t(value >> (32 * i));
  return result;
}
inline uint128 wide_value(const rhodium_rsim::WideBits<128> &value) {
  uint128 result = 0;
  for (unsigned i = 0; i < 4; ++i)
    result |= uint128(value.words[i]) << (32 * i);
  return result;
}
template <class T>
  requires std::is_same_v<T, uint128>
inline std::uint64_t slice(T value, unsigned hi, unsigned lo) {
  return std::uint64_t(value >> lo) & low_mask(hi - lo + 1);
}
