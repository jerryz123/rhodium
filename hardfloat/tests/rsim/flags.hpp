// Packs native exception records only at the independent numerical oracle
// boundary.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <cstdint>
template <class T> std::uint8_t flag_bits(const T &value) {
  if constexpr (requires { value.pinfinite; }) {
    return (value.pinvalid << 4) | (value.pinfinite << 3) |
           (value.poverflow << 2) | (value.punderflow << 1) | value.pinexact;
  } else {
    return (value.pinvalid << 2) | (value.poverflow << 1) | value.pinexact;
  }
}
