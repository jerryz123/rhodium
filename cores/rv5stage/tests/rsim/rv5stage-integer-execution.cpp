// Checks elastic integer services and exact three-cycle scalar multiply authorization.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "wide.hpp"
using Issue = std::remove_cvref_t<decltype(scalar_issue_in)>;
using Multiply = std::remove_cvref_t<decltype(multiply_result_out)>;
using Divide = std::remove_cvref_t<decltype(divide_result_out)>;
struct Product {
  unsigned tag;
  uint128 value;
};
struct Quotient {
  unsigned tag;
  std::uint64_t quotient, remainder;
};
std::array<Product, 24> products;
std::array<Quotient, 24> quotients;
std::array<Issue, 3> history{};
std::array<bool, 3> authorized{};
Multiply held_multiply{};
Divide held_divide{};
bool multiply_stalled = false, divide_stalled = false;
unsigned multiply_sent = 0, divide_sent = 0, multiply_received = 0,
         divide_received = 0;
unsigned cycles = 0, replacements = 0, scalar_cycle = 0, scalar_received = 0,
         scalar_canceled = 0;
__int128 operand(std::uint64_t value, bool signed_mode) {
  return signed_mode && (value >> 63) ? __int128(value) - (__int128(1) << 64)
                                      : __int128(value);
}
uint128 product(std::uint64_t left, std::uint64_t right, bool signed_left,
                bool signed_right) {
  return uint128(operand(left, signed_left)) *
         uint128(operand(right, signed_right));
}
bool same(const Multiply &a, const Multiply &b) {
  return a.pvalid == b.pvalid && a.pbits.ptag == b.pbits.ptag &&
         wide_value(a.pbits.pvalue) == wide_value(b.pbits.pvalue);
}
bool same(const Divide &a, const Divide &b) {
  return a.pvalid == b.pvalid && a.pbits.ptag == b.pbits.ptag &&
         a.pbits.pvalue.pquotient == b.pbits.pvalue.pquotient &&
         a.pbits.pvalue.premainder == b.pbits.pvalue.premainder;
}
void step(bool in_reset, bool drain) {
  reset = in_reset;
  multiply_request_in = {};
  divide_request_in = {};
  multiply_request_in.pvalid = !reset && multiply_sent < 24;
  auto &mul = multiply_request_in.pbits;
  mul.ptag = (multiply_sent * 17 + 3) & 255;
  mul.poperands.pleft = UINT64_C(0x8000000000000000) + multiply_sent * 113;
  mul.poperands.pright = UINT64_C(0xffffffffffffffd9) + multiply_sent;
  mul.poperands.pmode = {std::uint8_t((multiply_sent >> 1) & 1),
                         std::uint8_t(multiply_sent & 1)};
  divide_request_in.pvalid = !reset && divide_sent < 24;
  auto &div = divide_request_in.pbits;
  div.ptag = (divide_sent * 29 + 7) & 255;
  div.poperands.pdividend = UINT64_C(0x8000000000000000) + divide_sent * 197;
  div.poperands.pdivisor = divide_sent % 3 == 0   ? 0
                           : divide_sent % 3 == 1 ? UINT64_MAX
                                                  : 37;
  div.poperands.psigned_umode = divide_sent & 1;
  multiply_result_in.pready = drain && cycles % 13 < 4;
  divide_result_in.pready = drain && cycles % 17 < 3;
  scalar_issue_in = {};
  scalar_issue_in.pvalid = !reset && scalar_cycle < 70 && scalar_cycle % 9 != 8;
  auto &scalar = scalar_issue_in.pbits;
  scalar.pleft = UINT64_C(0xffffffff00000001) + scalar_cycle * 113;
  scalar.pright = UINT64_C(0xffffffffffffffd9) + scalar_cycle;
  scalar.pcontrol.pmode = {std::uint8_t((scalar_cycle >> 3) & 1),
                           std::uint8_t((scalar_cycle >> 2) & 1)};
  scalar.pcontrol.phigh_uresult = (scalar_cycle >> 1) & 1;
  scalar.pcontrol.pword_uresult = scalar_cycle % 8 == 1;
  scalar.prd = 1 + scalar_cycle % 31;
  scalar_authorize_in = {std::uint8_t(history[1].pvalid && authorized[1]),
                         history[1].pbits.prd};
  scalar_completion_in.pready = 1;
  eval();
  if (reset) {
    multiply_sent = divide_sent = multiply_received = divide_received = 0;
    multiply_stalled = divide_stalled = false;
    replacements = scalar_cycle = scalar_received = scalar_canceled = 0;
    history = {};
    authorized = {};
    tick_model();
    return;
  }
  CHECK(scalar_available);
  CHECK(bool(scalar_completion_out.pvalid) ==
        (history[2].pvalid && authorized[2]));
  if (scalar_completion_out.pvalid) {
    const auto &old = history[2].pbits;
    const auto p =
        product(old.pleft, old.pright, old.pcontrol.pmode.pleft_usigned,
                old.pcontrol.pmode.pright_usigned);
    std::uint64_t value =
        old.pcontrol.phigh_uresult ? std::uint64_t(p >> 64) : std::uint64_t(p);
    if (old.pcontrol.pword_uresult)
      value = std::uint64_t(std::int64_t(std::int32_t(value)));
    CHECK(scalar_completion_out.pbits.prd == old.prd &&
          scalar_completion_out.pbits.pvalue == value);
    ++scalar_received;
  }
  if (history[2].pvalid && !authorized[2])
    ++scalar_canceled;
  if (multiply_stalled)
    CHECK(same(multiply_result_out, held_multiply));
  if (divide_stalled)
    CHECK(same(divide_result_out, held_divide));
  held_multiply = multiply_result_out;
  held_divide = divide_result_out;
  multiply_stalled = multiply_result_out.pvalid && !multiply_result_in.pready;
  divide_stalled = divide_result_out.pvalid && !divide_result_in.pready;
  // Check returned owners before admitting this edge's new transactions.
  if (multiply_result_out.pvalid && multiply_result_in.pready) {
    CHECK(multiply_received < multiply_sent);
    const auto &e = products[multiply_received++];
    CHECK(multiply_result_out.pbits.ptag == e.tag &&
          wide_value(multiply_result_out.pbits.pvalue) == e.value);
  }
  if (divide_result_out.pvalid && divide_result_in.pready) {
    CHECK(divide_received < divide_sent);
    const auto &e = quotients[divide_received++];
    CHECK(divide_result_out.pbits.ptag == e.tag &&
          divide_result_out.pbits.pvalue.pquotient == e.quotient &&
          divide_result_out.pbits.pvalue.premainder == e.remainder);
  }
  if (multiply_request_in.pvalid && multiply_request_out.pready) {
    products[multiply_sent++] = {
        mul.ptag, product(mul.poperands.pleft, mul.poperands.pright,
                          mul.poperands.pmode.pleft_usigned,
                          mul.poperands.pmode.pright_usigned)};
    if (multiply_result_out.pvalid && multiply_result_in.pready)
      ++replacements;
  }
  if (divide_request_in.pvalid && divide_request_out.pready) {
    auto a = operand(div.poperands.pdividend, div.poperands.psigned_umode);
    auto b = operand(div.poperands.pdivisor, div.poperands.psigned_umode);
    quotients[divide_sent++] = {div.ptag, b ? std::uint64_t(a / b) : UINT64_MAX,
                                b ? std::uint64_t(a % b) : std::uint64_t(a)};
  }
  auto launched = scalar_issue_in;
  tick_model();
  for (unsigned i = 2; i > 0; --i) {
    history[i] = history[i - 1];
    authorized[i] = authorized[i - 1];
  }
  history[0] = launched;
  authorized[0] = scalar_cycle % 4 != 0;
  ++scalar_cycle;
  ++cycles;
  CHECK(cycles <= 5000);
}
int main() {
  return run_test([] {
    for (unsigned i = 0; i < 4; ++i)
      step(true, false);
    do {
      step(false, false);
    } while (!multiply_result_out.pvalid || !divide_result_out.pvalid);
    for (unsigned i = 0; i < 5; ++i)
      step(false, false);
    for (unsigned i = 0; i < 3; ++i)
      step(true, false);
    while (multiply_received != 24 || divide_received != 24)
      step(false, true);
    CHECK(replacements > 0 && scalar_received > 30 && scalar_canceled > 10);
  });
}
