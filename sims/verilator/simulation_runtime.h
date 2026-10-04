// Declares the feature-independent simulator lifecycle and reserved argument ownership.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string_view>
#include <svdpi.h>

namespace rhodium::simulation {
inline bool runtime_argument(std::string_view argument) {
  return argument.starts_with("+rheg-trace=") || argument.starts_with("+cosim-corrupt-order=") || argument.starts_with("+max-cycles=");
}
}

extern "C" int rhodium_sim_open() noexcept;
extern "C" int rhodium_sim_begin(svBit reset_active) noexcept;
extern "C" int rhodium_sim_end() noexcept;
extern "C" int rhodium_sim_finish() noexcept;
