// Materializes exact typed dot/delta inputs at compile time and obscures them from kernel folding.
// SPDX-License-Identifier: Apache-2.0
#ifndef RHODIUM_HIGHWAY_CONSTANT_INPUTS_H_
#define RHODIUM_HIGHWAY_CONSTANT_INPUTS_H_
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

// Expand the original exactly representable dot-product pattern into constant storage.
template <class T, size_t Modulus, size_t Divisor, size_t... I>
constexpr std::array<T, sizeof...(I)> DotInputsImpl(std::index_sequence<I...>) {
  return {{T(static_cast<float>(I % Modulus) / Divisor)...}};
}
template <class T, size_t Modulus, size_t Divisor>
constexpr std::array<T, 257> DotInputs() {
  return DotInputsImpl<T, Modulus, Divisor>(std::make_index_sequence<257>{});
}

// Match Mix(index) using defined 32-bit arithmetic, evaluated only at compile time.
constexpr uint32_t InputMix(uint32_t index) {
  uint32_t value = index + 0x9e3779b9u;
  value = (value ^ (value >> 16)) * 0x85ebca6bu;
  value = (value ^ (value >> 13)) * 0xc2b2ae35u;
  return value ^ (value >> 16);
}
template <class T, size_t... I>
constexpr std::array<T, sizeof...(I)> DeltaInputsImpl(std::index_sequence<I...>) {
  return {{T(InputMix(I) % 127)...}};
}
template <class T>
constexpr std::array<T, 257> DeltaInputs() {
  return DeltaInputsImpl<T>(std::make_index_sequence<257>{});
}

// Keep actual target loads and arithmetic despite the read-only constant definitions.
template <class T, size_t N>
const T* OpaqueInput(const std::array<T, N>& values) {
  const T* pointer = values.data();
  asm volatile("" : "+r"(pointer) : : "memory");
  return pointer;
}
#endif
