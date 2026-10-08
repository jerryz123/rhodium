// Provides explicit host-responder phases and bounded waits for RV5Stage scoreboards.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "test.hpp"
#include <functional>
using uint128 = unsigned __int128;
using signed128 = __int128;
constexpr uint128 mask128(unsigned width) {
  return width >= 128 ? ~uint128(0) : (uint128(1) << width) - 1;
}
template <class T> std::uint64_t bits(T value, unsigned hi, unsigned lo) {
  return std::uint64_t(value >> lo) & low_mask(hi - lo + 1);
}
template <class T>
uint128 read_bits(const T &value, unsigned offset, unsigned width) {
  CHECK(width <= 128);
  if constexpr (requires { value.words; }) {
    CHECK(offset + width <= value.words.size() * 32);
    uint128 result = 0;
    for (unsigned bit = 0; bit < width; ++bit)
      result |=
          uint128((value.words[(offset + bit) / 32] >> ((offset + bit) % 32)) &
                  1)
          << bit;
    return result;
  } else {
    return (uint128(value) >> offset) & mask128(width);
  }
}
template <class T, class U>
void write_bits(T &value, unsigned offset, unsigned width, U replacement) {
  CHECK(width <= 128);
  if constexpr (requires { value.words; }) {
    CHECK(offset + width <= value.words.size() * 32);
    for (unsigned bit = 0; bit < width; ++bit) {
      auto &word = value.words[(offset + bit) / 32];
      auto mask = std::uint32_t(1) << ((offset + bit) % 32);
      word = (word & ~mask) | (((uint128(replacement) >> bit) & 1) ? mask : 0);
    }
  } else {
    auto mask = mask128(width) << offset;
    value =
        T((uint128(value) & ~mask) | ((uint128(replacement) << offset) & mask));
  }
}
// Host responses are sampled alongside model inputs. Publish their registered
// updates only after both the scoreboard and the DUT have sampled the edge.
inline std::vector<std::function<void()>> updates;
template <class T, class U> void defer(T &target, U value) {
  static_assert(!std::is_class_v<T> ||
                    std::is_same_v<T, std::remove_cvref_t<U>>,
                "registered aggregates need an explicitly typed value");
  updates.emplace_back([&target, value = T(value)] { target = value; });
}
void drive();
void observe();
void falling_update();
// Paired timing oracles tick two independent DUTs before publishing host state.
inline std::function<void()> eval_extra = [] {};
inline std::function<void()> tick_extra = [] {};
inline bool high = false;
inline std::uint64_t cycle_limit = 1000000;
struct Finished {};
inline void settle() {
  drive();
  eval();
  eval_extra();
  drive();
  eval();
  eval_extra();
}
void falling();
inline void rising() {
  if (high)
    falling();
  settle();
  observe();
  CHECK(test_cycles < cycle_limit);
  tick_model();
  tick_extra();
  for (auto &update : updates)
    update();
  updates.clear();
  high = true;
  settle();
}
inline void falling() {
  if (!high)
    rising();
  high = false;
  falling_update();
  settle();
}
template <class F> void until(F condition) {
  settle();
  while (!condition())
    rising();
}
template <class F> void accept(F condition) {
  if (high)
    falling();
  for (;;) {
    settle();
    bool accepted = condition();
    rising();
    if (accepted)
      return;
    falling();
  }
}
// These helpers implement fixed-width arithmetic in the independent host oracle.
inline std::uint64_t sign_extend(std::uint64_t value, unsigned width) {
  value &= low_mask(width);
  return width < 64 && (value >> (width - 1) & 1) ? value | ~low_mask(width)
                                                  : value;
}
template <class T> struct Slice {
  T &value;
  unsigned offset, width;
  operator uint128() const { return read_bits(value, offset, width); }
  Slice &operator=(const Slice &other) { return *this = uint128(other); }
  template <class U> Slice &operator=(U replacement) {
    write_bits(value, offset, width, replacement);
    return *this;
  }
};
template <class T>
Slice<T> bit_slice(T &value, unsigned offset, unsigned width) {
  return {value, offset, width};
}
template <class T, class U> void defer(Slice<T> target, U value) {
  updates.emplace_back(
      [target, value = uint128(value)]() mutable { target = value; });
}
inline bool one_of(std::uint64_t value,
                   std::initializer_list<std::uint64_t> choices) {
  return std::find(choices.begin(), choices.end(), value) != choices.end();
}
// Insert one explicitly sized field into an instruction or packed oracle value.
inline uint128 field(uint128 value, unsigned width, unsigned offset) {
  return (value & mask128(width)) << offset;
}

inline std::uint16_t memory_integer(unsigned rd) { return 0x80 | (rd & 31); }
inline std::uint16_t memory_fp(unsigned rd, unsigned precision) {
  return 0x100 | ((rd & 31) << 2) | (precision & 3);
}
inline std::uint16_t memory_vector(unsigned slot) { return 0x180 | (slot & 7); }
inline unsigned memory_rd(unsigned wb) {
  return (wb >> 7) == 2 ? (wb >> 2) & 31 : wb & 31;
}

// Compare logical fields rather than C++ padding in typed memory transactions.
template <class A, class B> bool same_memory_context(const A &a, const B &b) {
  return a.pwriteback == b.pwriteback && a.porigin == b.porigin;
}
template <class A, class B> bool same_memory_request(const A &a, const B &b) {
  return a.pbyte_umask == b.pbyte_umask && a.paddress == b.paddress &&
         a.paccess == b.paccess && a.patomic == b.patomic &&
         a.pwidth == b.pwidth && a.punsigned == b.punsigned &&
         a.pdata == b.pdata && a.plocality == b.plocality &&
         same_memory_context(a.pcontext, b.pcontext);
}
template <class A, class B> bool same_memory_response(const A &a, const B &b) {
  return a.paccess_ufault == b.paccess_ufault && a.pdata == b.pdata &&
         same_memory_context(a.pcontext, b.pcontext);
}
