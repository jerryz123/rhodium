// Checks exact three-cycle products, exhaustive small widths, full-width edges,
// throughput, bubbles, and reset with products in flight.
// SPDX-License-Identifier: Apache-2.0
#include "wide.hpp"

template <unsigned W, class Request, class Response> class Checker {
  static constexpr unsigned latency = 3;
  static constexpr unsigned count = [] {
    if constexpr (W <= 8) return 4U * (1U << W) * (1U << W);
    else return 12U * 12U * 4U + 4096U;
  }();
  struct Expected { bool valid = false; uint128 value = 0; };
  Request &request;
  const Response &response;
  std::array<Expected, latency> history{};
  unsigned sent = 0, received = 0, cycles = 0;
  std::uint64_t random_left = 0x983127abc5961305, random_right = 0x617ea470db1279f3;

  static std::uint64_t edge_value(unsigned index) {
    const std::uint64_t sign = UINT64_C(1) << (W - 1), half = UINT64_C(1) << (W / 2);
    const std::array<std::uint64_t, 12> edges = {
      0, 1, 2, UINT64_MAX, UINT64_MAX - 1, sign, sign - 1, sign + 1,
      half - 1, half, half + 1, UINT64_C(0xfedcba9876543210)
    };
    return edges[index] & low_mask(W);
  }
  static std::uint64_t advance_random(std::uint64_t value) {
    value ^= value << 13; value ^= value >> 7; value ^= value << 17; return value;
  }
  static uint128 product(std::uint64_t left, std::uint64_t right, bool left_signed, bool right_signed) {
    // Independent sign/magnitude oracle, without the RTL's raw-product
    // corrections or carry-save tree.
    const bool left_negative = left_signed && (left >> (W - 1));
    const bool right_negative = right_signed && (right >> (W - 1));
    const uint128 a = left_negative ? ((~left + 1) & low_mask(W)) : left;
    const uint128 b = right_negative ? ((~right + 1) & low_mask(W)) : right;
    uint128 result = a * b;
    if (left_negative != right_negative) result = -result;
    if constexpr (W < 64) result &= (uint128(1) << (2 * W)) - 1;
    return result;
  }
  uint128 returned_value() const {
    if constexpr (W == 64) return wide_value(response.pbits);
    else return response.pbits;
  }

public:
  Checker(Request &input, const Response &output) : request(input), response(output) {}
  bool done = false;
  void clear() {
    request = {}; history = {}; sent = received = cycles = 0; done = false;
    random_left = 0x983127abc5961305; random_right = 0x617ea470db1279f3;
  }
  void offer() {
    request = {};
    request.pvalid = !done && sent < count && cycles % 67 != 66;
    auto &bits = request.pbits;
    bits.pmode.pleft_usigned = (sent >> 1) & 1;
    bits.pmode.pright_usigned = sent & 1;
    if constexpr (W <= 8) {
      bits.pleft = (sent >> 2) & low_mask(W);
      bits.pright = (sent >> (W + 2)) & low_mask(W);
    } else if (sent < 12 * 12 * 4) {
      bits.pleft = edge_value((sent / 4) % 12);
      bits.pright = edge_value((sent / 4) / 12);
    } else {
      bits.pleft = random_left & low_mask(W); bits.pright = random_right & low_mask(W);
    }
  }
  void check() {
    CHECK(bool(response.pvalid) == history[latency - 1].valid);
    if (response.pvalid) { CHECK(returned_value() == history[latency - 1].value); ++received; }
    for (unsigned i = latency - 1; i > 0; --i) history[i] = history[i - 1];
    const auto &bits = request.pbits;
    history[0] = {bool(request.pvalid), product(bits.pleft, bits.pright, bits.pmode.pleft_usigned, bits.pmode.pright_usigned)};
    if (request.pvalid) {
      ++sent; random_left = advance_random(random_left); random_right = advance_random(random_right);
    }
    ++cycles;
    if (received == count && !done) {
      CHECK(sent == count); done = true;
      std::cout << "three-stage multiplier W=" << W << " passed: " << received << " products\n";
    }
  }
};

int main() {
  return run_test([] {
    Checker<2, std::remove_reference_t<decltype(request2_in)>, std::remove_reference_t<decltype(response2_out)>> small(request2_in, response2_out);
    Checker<8, std::remove_reference_t<decltype(request8_in)>, std::remove_reference_t<decltype(response8_out)>> byte(request8_in, response8_out);
    Checker<32, std::remove_reference_t<decltype(request32_in)>, std::remove_reference_t<decltype(response32_out)>> word(request32_in, response32_out);
    Checker<64, std::remove_reference_t<decltype(request64_in)>, std::remove_reference_t<decltype(response64_out)>> full(request64_in, response64_out);
    const auto reset_all = [&] {
      reset = 1; small.clear(); byte.clear(); word.clear(); full.clear();
      tick_model(); tick_model(); tick_model(); reset = 0;
    };
    const auto step = [&] {
      small.offer(); byte.offer(); word.offer(); full.offer(); eval();
      small.check(); byte.check(); word.check(); full.check(); tick_model();
    };
    reset_all();
    for (unsigned i = 0; i < 20; ++i) step();
    reset_all();
    while (!(small.done && byte.done && word.done && full.done)) step();
  });
}
