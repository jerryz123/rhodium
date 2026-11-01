// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <string_view>
namespace rhodium::statistics {
inline constexpr std::array<std::string_view, 2> descriptors = {
  "\"rtl/cycles\":",
  "\"rtl/never\":"
};
}
