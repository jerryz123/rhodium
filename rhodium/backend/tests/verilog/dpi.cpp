// Implements shared DPI ABI fixtures and reports per-edge calls without ordering assumptions.
// SPDX-License-Identifier: Apache-2.0
#include "Vdpi_tb__Dpi.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

// Including the generated header makes both backends check these same C signatures.
// Inputs and out arguments use low-word-first svBitVecVal arrays for packed vectors.
template <unsigned W>
using Words = std::array<svBitVecVal, (W + 31) / 32>;

template <unsigned W, typename T>
Words<W> native_words(T value) {
  const auto bits = static_cast<std::uint64_t>(static_cast<std::make_unsigned_t<T>>(value));
  Words<W> words{};
  words[0] = static_cast<std::uint32_t>(bits);
  if constexpr (W > 32) words[1] = static_cast<std::uint32_t>(bits >> 32);
  return words;
}

template <unsigned W>
Words<W> packed_words(const svBitVecVal* value) {
  Words<W> words{};
  for (unsigned i = 0; i < words.size(); ++i) words[i] = value[i];
  if constexpr (W % 32) words.back() &= (std::uint32_t{1} << (W % 32)) - 1;
  return words;
}

template <unsigned W>
std::uint64_t low_bits(const Words<W>& words) {
  std::uint64_t value = words[0];
  if constexpr (W > 32) value |= std::uint64_t{words[1]} << 32;
  return value;
}

template <unsigned W>
void event(char kind, unsigned id, const Words<W>& words) {
  const char* scope = svGetNameFromScope(svGetScope());
  const char* side = id == 0 ? ".first" : ".second";
  if (!scope || id > 1 || std::strlen(scope) < std::strlen(side) ||
      std::strcmp(scope + std::strlen(scope) - std::strlen(side), side) != 0) {
    std::fprintf(stderr, "wrong DPI instance scope for identity %u\n", id);
    std::abort();
  }
  std::printf("dpi %c %u %u ", kind, id, W);
  for (unsigned i = words.size(); i > 0; --i) std::printf("%08x", words[i - 1]);
  std::printf("\n");
}

template <unsigned W>
constexpr std::uint64_t scalar_mask() {
  if constexpr (W == 64) return UINT64_MAX;
  else return (std::uint64_t{1} << W) - 1;
}

#define NATIVE(W, T) \
extern "C" void rhdl_dpi_proc##W(char id, T value) { \
  event<W>('P', static_cast<unsigned char>(id), native_words<W>(value)); \
} \
extern "C" T rhdl_dpi_pair##W(char id, T value, T* inverted, char* tag) { \
  const auto words = native_words<W>(value); \
  const unsigned identity = static_cast<unsigned char>(id); \
  event<W>('F', identity, words); \
  *inverted = static_cast<T>(~low_bits<W>(words) & scalar_mask<W>()); \
  *tag = static_cast<char>(identity ^ 0x5a); \
  return static_cast<T>((low_bits<W>(words) + identity + 3) & scalar_mask<W>()); \
} \
extern "C" T rhdl_dpi_single##W(char id, T value) { \
  const auto words = native_words<W>(value); \
  event<W>('S', static_cast<unsigned char>(id), words); \
  return static_cast<T>((low_bits<W>(words) ^ 0xa5) & scalar_mask<W>()); \
}

NATIVE(1, svBit)
NATIVE(8, char)
NATIVE(16, short)
NATIVE(32, int)
NATIVE(64, long long)

#define PACKED(W, R, RW) \
extern "C" void rhdl_dpi_proc##W(char id, const svBitVecVal* value) { \
  event<W>('P', static_cast<unsigned char>(id), packed_words<W>(value)); \
} \
extern "C" R rhdl_dpi_pair##W(char id, const svBitVecVal* value, svBitVecVal* inverted, char* tag) { \
  const auto words = packed_words<W>(value); \
  const unsigned identity = static_cast<unsigned char>(id); \
  event<W>('F', identity, words); \
  for (unsigned i = 0; i < words.size(); ++i) inverted[i] = ~words[i]; \
  if constexpr (W % 32) inverted[words.size() - 1] &= (std::uint32_t{1} << (W % 32)) - 1; \
  *tag = static_cast<char>(identity ^ 0x5a); \
  return static_cast<R>((low_bits<W>(words) + identity + 3) & ((std::uint64_t{1} << RW) - 1)); \
} \
extern "C" R rhdl_dpi_single##W(char id, const svBitVecVal* value) { \
  const auto words = packed_words<W>(value); \
  event<W>('S', static_cast<unsigned char>(id), words); \
  return static_cast<R>((low_bits<W>(words) ^ 0xa5) & ((std::uint64_t{1} << RW) - 1)); \
}

PACKED(3, svBitVecVal, 3)
PACKED(33, int, 32)
PACKED(65, int, 32)
PACKED(129, int, 32)
