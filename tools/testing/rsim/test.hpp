// Provides always-active checks and explicit evaluation and clock-edge
// operations for rsim component tests.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "ports.hpp"
#include <algorithm>
#include <array>
#include <utility>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
// Allow short port names in drivers; qualify collisions as ports::name.
using namespace ports;
inline std::uint64_t test_cycles = 0;
inline void eval() { dut.eval(); }
inline void tick_model() {
  if (++test_cycles > 10000000)
    throw std::runtime_error("cycle limit exceeded");
  dut.tick();
}
inline void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(std::string(message) + " at cycle " +
                             std::to_string(test_cycles));
}
#define CHECK(condition) require(bool(condition), #condition)
template <class F> int run_test(F body) {
  try {
    body();
    std::cout << "PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
inline std::uint64_t low_mask(unsigned width) {
  return width >= 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
}
inline std::uint64_t slice(std::uint64_t value, unsigned hi, unsigned lo) {
  return (value >> lo) & low_mask(hi - lo + 1);
}

template <class T, std::size_t N>
std::uint64_t pack_bits(const std::array<T, N> &value) {
  static_assert(N <= 64);
  std::uint64_t result = 0;
  for (std::size_t i = 0; i < N; ++i)
    result |= std::uint64_t(value[i] & 1) << i;
  return result;
}

template <class... Args>
[[noreturn]] void fail(int, const char *message, Args...) {
  throw std::runtime_error(std::string(message) + " at cycle " +
                           std::to_string(test_cycles));
}

// Negative cases must fail for the intended hardware assertion, not a test
// timeout.
template <class F> void expect_failure(const char *label, F body) {
  try {
    body();
  } catch (const std::runtime_error &error) {
    require(std::string(error.what()).find(label) != std::string::npos,
            error.what());
    return;
  }
  throw std::runtime_error(std::string("missing hardware assertion: ") + label);
}
