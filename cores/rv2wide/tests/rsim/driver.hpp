// Provides explicit host-responder phases and bounded waits for RV2Wide scoreboards.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "test.hpp"
#include <bit>
#include <deque>
#include <functional>
#include <map>
using uint128 = unsigned __int128;
using signed128 = __int128;
constexpr uint128 mask128(unsigned width) {
  return width >= 128 ? ~uint128(0) : (uint128(1) << width) - 1;
}
template <class T> std::uint64_t bits(T value, unsigned hi, unsigned lo) {
  return std::uint64_t(value >> lo) & low_mask(hi - lo + 1);
}
template <class T, class U>
void write_bits(T &value, unsigned offset, unsigned width, U replacement) {
  auto mask = mask128(width) << offset;
  value =
      T((uint128(value) & ~mask) | ((uint128(replacement) << offset) & mask));
}
template <class T> struct Queue : std::deque<T> {
  T take() {
    CHECK(!this->empty());
    T value = this->front();
    this->pop_front();
    return value;
  }
};
// Host responses are sampled alongside model inputs. Publish their registered
// updates only after both the scoreboard and the DUT have sampled the edge.
inline std::vector<std::function<void()>> updates;
template <class T, class U> void defer(T &target, U value) {
  updates.emplace_back([&target, value = T(value)] { target = value; });
}
void drive();
void observe();
void falling_update();
inline bool high = false;
inline void settle() {
  drive();
  eval();
  drive();
  eval();
}
void falling();
inline void rising() {
  if (high)
    falling();
  settle();
  observe();
  tick_model();
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
  operator uint128() const {
    return (uint128(value) >> offset) & mask128(width);
  }
  Slice &operator=(const Slice &other) { return *this = uint128(other); }
  template <class U> Slice &operator=(U replacement) {
    write_bits(value, offset, width, replacement);
    return *this;
  }
};
template <class T>
Slice<T> sv_slice(T &value, unsigned offset, unsigned width) {
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
template <class T> bool same_instruction(const T &x, const T &y) {
  return x.ppc == y.ppc && x.pinstruction == y.pinstruction &&
         x.praw_uinstruction == y.praw_uinstruction &&
         x.psequential_upc == y.psequential_upc &&
         x.pcompressed_uillegal == y.pcompressed_uillegal &&
         x.pfault.pvalid == y.pfault.pvalid &&
         x.pfault.pcause == y.pfault.pcause &&
         x.pfault.pvalue == y.pfault.pvalue &&
         x.pfault.pguest.pguest_uvirtual_uaddress ==
             y.pfault.pguest.pguest_uvirtual_uaddress &&
         x.pfault.pguest.pguest_uphysical_uaddress ==
             y.pfault.pguest.pguest_uphysical_uaddress &&
         x.pfault.pguest.paccess == y.pfault.pguest.paccess &&
         x.pprediction.pvalid == y.pprediction.pvalid &&
         x.pprediction.ppc == y.pprediction.ppc &&
         x.pprediction.ptarget == y.pprediction.ptarget &&
         x.pprediction.pcompressed == y.pprediction.pcompressed &&
         x.pprediction.pras_uaction == y.pprediction.pras_uaction &&
         x.pspeculated_uras_uaction == y.pspeculated_uras_uaction &&
         x.pdirection.pvalid == y.pdirection.pvalid &&
         x.pdirection.pindex == y.pdirection.pindex &&
         x.pdirection.phistory == y.pdirection.phistory &&
         x.pdirection.ptaken == y.pdirection.ptaken;
}

// Insert one explicitly sized field into an instruction or packed oracle value.
inline uint128 field(uint128 value, unsigned width, unsigned offset) {
  return (value & mask128(width)) << offset;
}
