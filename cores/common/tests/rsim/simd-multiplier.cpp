// SPDX-License-Identifier: Apache-2.0
#include "wide.hpp"

namespace {
using sint128 = __int128;
enum Select { Low, High, Wide, Fractional };
struct Operation {
  unsigned sew = 8, select = Low, rounding = 0;
  bool left_signed = false, right_signed = false, upper = false, accumulate = false, subtract = false;
  std::uint64_t left = 0, right = 0, addend = 0;
};
struct Answer { std::uint64_t data = 0, saturated = 0; };

uint128 mask128(unsigned width) {
  return width == 128 ? ~uint128(0) : (uint128(1) << width) - 1;
}
uint128 lane_product(unsigned sew, std::uint64_t left, std::uint64_t right, bool ls, bool rs) {
  const bool ln = ls && (left >> (sew - 1)), rn = rs && (right >> (sew - 1));
  const uint128 a = ln ? ((~left + 1) & low_mask(sew)) : left;
  const uint128 b = rn ? ((~right + 1) & low_mask(sew)) : right;
  const uint128 magnitude = a * b;
  return (ln != rn ? -magnitude : magnitude) & mask128(sew * 2);
}
uint128 products(unsigned width, const Operation &op) {
  uint128 result = 0;
  for (unsigned lane = 0; lane < width / op.sew; ++lane) {
    const auto a = (op.left >> (lane * op.sew)) & low_mask(op.sew);
    const auto b = (op.right >> (lane * op.sew)) & low_mask(op.sew);
    result |= lane_product(op.sew, a, b, op.left_signed, op.right_signed) << (lane * op.sew * 2);
  }
  return result;
}
Answer format(unsigned width, const Operation &op, uint128 packed) {
  Answer result;
  const unsigned ew = op.select == Wide ? op.sew * 2 : op.sew;
  const unsigned first = op.select == Wide && op.upper ? width / ew : 0;
  for (unsigned lane = 0; lane < width / ew; ++lane) {
    const uint128 product = (packed >> ((first + lane) * op.sew * 2)) & mask128(op.sew * 2);
    std::uint64_t value = std::uint64_t(op.select == High ? product >> op.sew : product) & low_mask(ew);
    bool saturated = false;
    if (op.select == Fractional) {
      // Signed quotient/remainder oracle, independent of RTL bit selection.
      sint128 signed_product;
      if (product & (uint128(1) << (op.sew * 2 - 1)))
        signed_product = -sint128((-product) & mask128(op.sew * 2));
      else signed_product = sint128(product);
      const sint128 denominator = sint128(1) << (op.sew - 1);
      sint128 q = signed_product / denominator, rem = signed_product % denominator;
      if (rem < 0) { --q; rem += denominator; }
      if (op.rounding == 0 && rem >= denominator / 2) ++q;
      if (op.rounding == 1 && (rem > denominator / 2 || (rem == denominator / 2 && q % 2 != 0))) ++q;
      if (op.rounding == 3 && rem != 0 && q % 2 == 0) ++q;
      const sint128 min = -denominator, max = denominator - 1;
      saturated = q < min || q > max;
      if (q < min) q = min;
      if (q > max) q = max;
      value = std::uint64_t(q) & low_mask(ew);
    } else if (op.accumulate) {
      const auto addend = (op.addend >> (lane * ew)) & low_mask(ew);
      value = (op.subtract ? addend - value : addend + value) & low_mask(ew);
    }
    result.data |= value << (lane * ew);
    if (saturated) result.saturated |= low_mask(ew / 8) << (lane * ew / 8);
  }
  return result;
}
std::uint64_t next_random(std::uint64_t &state) {
  state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state;
}
std::uint64_t edge(unsigned sew, unsigned index) {
  const std::uint64_t sign = UINT64_C(1) << (sew - 1);
  const std::array<std::uint64_t, 8> values = {0, 1, low_mask(sew), sign, sign - 1, sign + 1, 3, sign / 2};
  return values[index % values.size()];
}
unsigned width_code(unsigned sew) { return sew == 8 ? 0 : sew == 16 ? 1 : sew == 32 ? 2 : 3; }

template <unsigned W, unsigned MaxElement, class Request, class Raw, class Result> class Checker {
  static constexpr unsigned formats = MaxElement == 64 ? 4 : MaxElement == 32 ? 3 : MaxElement == 16 ? 2 : 1;
  static constexpr unsigned exhaustive = 256 * 256 * 4;
  static constexpr unsigned directed = formats * 4 * 4 * 4 * 8 * 64;
  static constexpr unsigned count = exhaustive + directed + 8192;
  struct Expected { bool valid = false; uint128 product = 0; unsigned sew = 0; Answer answer; };
  Request &request;
  const Raw &raw;
  const Result &result;
  std::array<Expected, 4> history{};
  unsigned sent = 0, received = 0, cycles = 0;
  std::uint64_t rng = 0x79abcde019347852;
  Operation op;
public:
  Checker(Request &r, const Raw &p, const Result &o) : request(r), raw(p), result(o) {}
  bool done() const { return received == count; }
  void clear() { request = {}; history = {}; sent = received = cycles = 0; }
  void offer() {
    op = {};
    if (sent < exhaustive) {
      op.left_signed = sent & 1; op.right_signed = (sent >> 1) & 1;
      for (unsigned lane = 0; lane < W / 8; ++lane) {
        op.left |= std::uint64_t(((sent >> 2) + lane * 17) & 255) << (lane * 8);
        op.right |= std::uint64_t(((sent >> 10) + lane * 29) & 255) << (lane * 8);
      }
    } else if (sent < exhaustive + directed) {
      unsigned index = sent - exhaustive;
      const unsigned pair = index % 64; index /= 64;
      op.accumulate = index & 1; op.subtract = index & 2; op.upper = index & 4; index /= 8;
      op.rounding = index % 4; index /= 4;
      op.select = index % 4; index /= 4;
      op.left_signed = index & 1; op.right_signed = index & 2; index /= 4;
      op.sew = 8 << (index % formats);
      for (unsigned lane = 0; lane < W / op.sew; ++lane) {
        op.left |= edge(op.sew, pair % 8 + lane) << (lane * op.sew);
        op.right |= edge(op.sew, pair / 8 + lane * 3) << (lane * op.sew);
      }
    } else {
      const auto control = next_random(rng);
      op.sew = 8 << (control % formats); op.select = (control >> 4) % 4;
      op.rounding = (control >> 6) % 4; op.accumulate = control & 256;
      op.subtract = control & 512; op.upper = control & 1024;
      op.left_signed = control & 2048; op.right_signed = control & 4096;
      op.left = next_random(rng) & low_mask(W); op.right = next_random(rng) & low_mask(W);
    }
    if (op.select == Wide && (op.sew == W || op.sew == 64)) op.select = High;
    if (op.select == Fractional) op.left_signed = op.right_signed = true;
    op.addend = next_random(rng) & low_mask(W);
    request = {};
    request.pvalid = !done() && sent < count && cycles % 71 != 70;
    auto &operand = request.pbits.poperands;
    operand.pleft = op.left; operand.pright = op.right;
    operand.pelement_uwidth = width_code(op.sew);
    operand.pmode.pleft_usigned = op.left_signed; operand.pmode.pright_usigned = op.right_signed;
    auto &context = request.pbits.pcontext;
    context.paddend = op.addend; context.pcontrol.pselect = op.select;
    context.pcontrol.pupper_uhalf = op.upper; context.pcontrol.paccumulate = op.accumulate;
    context.pcontrol.psubtract = op.subtract; context.pcontrol.prounding_umode = op.rounding;
  }
  void check() {
    CHECK(bool(raw.pvalid) == history[2].valid);
    if (raw.pvalid) {
      uint128 actual;
      if constexpr (W == 64) actual = wide_value(raw.pbits.pdata);
      else actual = raw.pbits.pdata;
      CHECK(actual == history[2].product);
      CHECK(raw.pbits.pelement_uwidth == width_code(history[2].sew));
    }
    CHECK(bool(result.pvalid) == history[3].valid);
    if (result.pvalid) {
      CHECK(result.pbits.pdata == history[3].answer.data);
      CHECK(result.pbits.psaturated == history[3].answer.saturated);
      ++received;
    }
    for (unsigned i = 3; i > 0; --i) history[i] = history[i - 1];
    const auto p = products(W, op);
    history[0] = {bool(request.pvalid), p, op.sew, format(W, op, p)};
    sent += bool(request.pvalid); ++cycles;
  }
  void report() const { std::cout << "SIMD multiplier W=" << W << " max-element=" << MaxElement << ": " << received << " checked transactions\n"; }
};
}

int main() {
  return run_test([] {
    Checker<32, 16, std::remove_reference_t<decltype(request32_in)>, std::remove_reference_t<decltype(raw32_out)>, std::remove_reference_t<decltype(result32_out)>> small(request32_in, raw32_out, result32_out);
    Checker<64, 32, std::remove_reference_t<decltype(request64_in)>, std::remove_reference_t<decltype(raw64_out)>, std::remove_reference_t<decltype(result64_out)>> full(request64_in, raw64_out, result64_out);
    Checker<64, 64, std::remove_reference_t<decltype(request_self64_in)>, std::remove_reference_t<decltype(raw_self64_out)>, std::remove_reference_t<decltype(result_self64_out)>> self(request_self64_in, raw_self64_out, result_self64_out);
    const auto reset_all = [&] {
      reset = 1; small.clear(); full.clear(); self.clear(); tick_model(); reset = 0;
      eval(); CHECK(!raw32_out.pvalid && !raw64_out.pvalid && !result32_out.pvalid && !result64_out.pvalid);
      CHECK(!raw_self64_out.pvalid && !result_self64_out.pvalid);
    };
    const auto step = [&] { small.offer(); full.offer(); self.offer(); eval(); small.check(); full.check(); self.check(); tick_model(); };
    reset_all();
    for (unsigned cycle = 0; cycle < 7; ++cycle) step();
    reset_all(); // Discard occupied stages, then prove the complete stream again.
    for (unsigned cycle = 0; cycle < 500000 && !(small.done() && full.done() && self.done()); ++cycle) step();
    CHECK(small.done() && full.done() && self.done()); small.report(); full.report(); self.report();

    // Full-width scalar products use the same formatter without another multiplier.
    request32_in = {}; request64_in = {}; request_self64_in = {};
    for (unsigned width : {32, 64}) for (unsigned pair = 0; pair < 64; ++pair) for (unsigned mode = 0; mode < 4; ++mode)
      for (unsigned selection : {Low, High, Fractional}) for (unsigned rounding = 0; rounding < 4; ++rounding)
        for (unsigned acc = 0; acc < 4; ++acc) {
          Operation op;
          op.sew = width; op.left = edge(width, pair % 8); op.right = edge(width, pair / 8);
          op.left_signed = mode & 1; op.right_signed = mode & 2;
          if (selection == Fractional) op.left_signed = op.right_signed = true;
          op.select = selection; op.rounding = rounding; op.accumulate = acc & 1; op.subtract = acc & 2;
          op.addend = 0xfedcba9876543210 & low_mask(width);
          const auto p = products(width, op); const auto expected = format(width, op, p);
          if (width == 64) {
            scalar_product.pdata = wide(p); scalar_product.pelement_uwidth = 3;
            scalar_context.paddend = op.addend; scalar_context.pcontrol.pselect = selection;
            scalar_context.pcontrol.paccumulate = op.accumulate; scalar_context.pcontrol.psubtract = op.subtract;
            scalar_context.pcontrol.prounding_umode = rounding;
            eval(); CHECK(scalar_result.pdata == expected.data); CHECK(scalar_result.psaturated == expected.saturated);
          } else {
            scalar32_product.pdata = std::uint64_t(p); scalar32_product.pelement_uwidth = 2;
            scalar32_context.paddend = op.addend; scalar32_context.pcontrol.pselect = selection;
            scalar32_context.pcontrol.paccumulate = op.accumulate; scalar32_context.pcontrol.psubtract = op.subtract;
            scalar32_context.pcontrol.prounding_umode = rounding;
            eval(); CHECK(scalar32_result.pdata == expected.data); CHECK(scalar32_result.psaturated == expected.saturated);
          }
        }
    request32_in.pvalid = true; request32_in.pbits.poperands.pelement_uwidth = 2;
    expect_failure("simd_multiply_element_width", [] { tick_model(); });
    request32_in = {}; request64_in.pvalid = true; request64_in.pbits.poperands.pelement_uwidth = 3;
    expect_failure("simd_multiply_element_width", [] { tick_model(); });
    request64_in = {}; request_self64_in.pvalid = true;
    request_self64_in.pbits.poperands.pelement_uwidth = 3;
    request_self64_in.pbits.pcontext.pcontrol.pselect = Wide;
    expect_failure("simd_multiply_wide_destination", [] { tick_model(); });
  });
}
