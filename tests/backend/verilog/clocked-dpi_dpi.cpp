// Implements the external C symbol linked into the clocked DPI simulation.
// SPDX-License-Identifier: Apache-2.0
#include <cstdint>

extern "C" void rhodium_trace(std::uint8_t value) {
  (void)value;
}

extern "C" std::uint8_t rhodium_step(std::uint8_t value) {
  return static_cast<std::uint8_t>(value + 1);
}

extern "C" std::uint8_t rhodium_step_pair(std::uint8_t value,
                                         std::uint8_t* doubled) {
  *doubled = static_cast<std::uint8_t>(value * 2);
  return static_cast<std::uint8_t>(value + 1);
}
