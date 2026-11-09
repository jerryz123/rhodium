// Checks double-width products, exact iteration latency, and replacement of a stalled response.
// SPDX-License-Identifier: Apache-2.0
#include "wide.hpp"

template <unsigned W>
void check_multiplier(auto &request, const auto &admission, auto &sink, const auto &response) {
  constexpr std::uint64_t sign = UINT64_C(1) << (W - 1);
  const auto mask = low_mask(W);
  std::vector<std::uint64_t> operands;
  if constexpr (W == 8) {
    for (unsigned i = 0; i < 256; ++i) operands.push_back(i);
  } else {
    operands = {0, 1, 7, sign - 1, sign, sign + 1, mask, UINT64_C(0xfedcba9876543210) & mask};
  }
  const auto extend = [=](std::uint64_t operand, bool is_signed) -> uint128 {
    return is_signed && (operand & sign) ? (uint128(operand) | ~uint128(mask)) : operand;
  };
  const auto result = [&]() -> uint128 {
    if constexpr (W == 64) return wide_value(response.pbits);
    else return response.pbits;
  };

  // Reset during an operation must discard its response and admit a fresh request.
  request = {};
  request.pvalid = 1;
  tick_model();
  request = {};
  tick_model();
  reset = 1;
  tick_model();
  reset = 0;
  for (unsigned i = 0; i <= W + 1; ++i) {
    CHECK(admission.pready && !response.pvalid);
    tick_model();
  }

  bool held = false;
  uint128 expected = 0;
  for (auto left : operands) {
    for (auto right : operands) {
      for (unsigned mode = 0; mode < 4; ++mode) {
        const bool left_signed = mode & 2, right_signed = mode & 1;
        request.pvalid = 1;
        request.pbits.pleft = left;
        request.pbits.pright = right;
        request.pbits.pmode = {left_signed, right_signed};
        sink.pready = 0;
        if (held) {
          // Offer the next request while the previous full product is stalled.
          for (unsigned stall = 0; stall < 3; ++stall) {
            eval();
            CHECK(!admission.pready && response.pvalid && result() == expected);
            tick_model();
          }
        }

        sink.pready = 1;
        eval();
        CHECK(admission.pready && bool(response.pvalid) == held);
        if (held) CHECK(result() == expected);
        tick_model();  // Accept the new operands on the edge that consumes the old product.
        request = {};
        sink.pready = 0;
        expected = (extend(left, left_signed) * extend(right, right_signed))
                   & (~uint128(0) >> (128 - 2 * W));
        for (unsigned cycle = 0; cycle < W + 1; ++cycle) {
          eval();
          CHECK(!admission.pready && !response.pvalid);
          tick_model();
        }
        CHECK(response.pvalid && result() == expected);
        held = true;
      }
    }
  }
  sink.pready = 1;
  tick_model();
  CHECK(!response.pvalid && admission.pready);
}

int main() {
  return run_test([] {
    reset = 1;
    tick_model();
    reset = 0;
    check_multiplier<8>(request8_in, request8_out, response8_in, response8_out);
    check_multiplier<32>(request32_in, request32_out, response32_in, response32_out);
    check_multiplier<64>(request64_in, request64_out, response64_in, response64_out);
  });
}
