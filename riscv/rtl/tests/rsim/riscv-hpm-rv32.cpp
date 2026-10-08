// Scores HPM filters, split writes, rearming, overflow and reset priority.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#ifndef HPM_XLEN
#define HPM_XLEN 32
#endif
constexpr int XLEN = HPM_XLEN;
constexpr bool HYPERVISOR = XLEN == 64;
constexpr std::uint64_t OF = UINT64_C(1) << 63;
constexpr std::uint64_t CONTROL_MASK =
    HYPERVISOR ? UINT64_C(0xfcffffffffffffff) : UINT64_C(0xf0ffffffffffffff);
std::uint64_t expected_count = 0, expected_control = 0;
unsigned checks = 0;
auto write_port(bool valid, bool high, std::uint64_t value) {
  auto port = counter_write_in;
  port.pvalid = valid;
#if HPM_XLEN == 32
  port.pbits.phigh = high;
#endif
  port.pbits.pvalue = value;
  return port;
}
void step(bool pulse = 0, std::uint8_t mode = 3, bool guest = 0,
          bool stopped = 0, bool cw = 0, bool ch = 0, std::uint64_t cv = 0,
          bool sw = 0, bool sh = 0, std::uint64_t sv = 0,
          std::uint8_t amount = 1) {
  event_in = {std::uint8_t(pulse), {mode, std::uint8_t(guest)}};
  event_count = amount;
  inhibit = stopped;
  counter_write_in = write_port(cw, ch, cv);
  selector_write_in = write_port(sw, sh, sv);
  int filter_bit = mode == 3 ? 62 : mode == 1 ? 61 : 60;
  if (HYPERVISOR && guest && mode != 3)
    filter_bit -= 2;
  bool increment = pulse && !stopped &&
                   !((expected_control >> filter_bit) & 1) && !cw && !sw;
  bool wrap = increment && amount > UINT64_MAX - expected_count;
  eval();
  CHECK(overflow_out.pvalid == (wrap && !(expected_control & OF)));
  if (cw) {
    if (XLEN == 64)
      expected_count = cv;
    else if (ch)
      expected_count = (expected_count & UINT32_MAX) |
                       (std::uint64_t(std::uint32_t(cv)) << 32);
    else
      expected_count =
          (expected_count & ~std::uint64_t(UINT32_MAX)) | std::uint32_t(cv);
  } else if (increment)
    expected_count += amount;
  if (sw) {
    if (XLEN == 64)
      expected_control = sv;
    else if (sh)
      expected_control = (expected_control & UINT32_MAX) |
                         (std::uint64_t(std::uint32_t(sv)) << 32);
    else
      expected_control =
          (expected_control & ~std::uint64_t(UINT32_MAX)) | std::uint32_t(sv);
    expected_control &= CONTROL_MASK;
  } else if (wrap)
    expected_control |= OF;
  tick_model();
  CHECK(counter == expected_count && selector == expected_control);
  ++checks;
}
void set_count(std::uint64_t value) {
  step(0, 3, 0, 0, 1, 0, value);
  if (XLEN == 32)
    step(0, 3, 0, 0, 1, 1, value >> 32);
}
void set_selector(std::uint64_t value) {
  step(0, 3, 0, 0, 0, 0, 0, 1, 0, value);
  if (XLEN == 32)
    step(0, 3, 0, 0, 0, 0, 0, 1, 1, value >> 32);
}
int main() {
  return run_test([] {
    reset = 1;
    event_in = {};
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    reset = 0;
    CHECK(counter == 0 && selector == 0);

    for (int filters = 0; filters < 32; filters++) {
      set_selector((((filters)&low_mask(64)) << 58) |
                   UINT64_C(24112363585356885));
      for (int mode = 0; mode < 5; mode++) {
        step(1,
             mode == 0                ? UINT64_C(3)
             : mode == 1 || mode == 3 ? UINT64_C(1)
                                      : UINT64_C(0),
             mode >= 3);
        step(1,
             mode == 0                ? UINT64_C(3)
             : mode == 1 || mode == 3 ? UINT64_C(1)
                                      : UINT64_C(0),
             mode >= 3, 1);
        step(0,
             mode == 0                ? UINT64_C(3)
             : mode == 1 || mode == 3 ? UINT64_C(1)
                                      : UINT64_C(0),
             mode >= 3);
      }
    }
    set_selector(UINT64_MAX);
    set_selector(0);
    set_count(UINT64_C(1311768469162688511));
    step(1);
    set_count(UINT64_MAX);
    step(1);
    step(1);
    set_count(UINT64_MAX);
    step(1);
    set_selector(0);
    set_count(UINT64_C(18446744073709551614));
    step(1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 2);
    step(1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    set_selector(0);
    set_count(UINT64_MAX);
    step(1);
    set_selector(OF);
    set_count(UINT64_MAX);
    step(1);
    set_selector(0);
    set_count(UINT64_MAX);
    step(1, 3, 0, 0, 1, 0, 7);
    set_count(UINT64_MAX);
    step(1, 3, 0, 0, 0, 0, 0, 1, 0, 0);
    step(1);
    step(1, 3, 0, 0, 1, 0, 42, 1, 0, 5);
    if (XLEN == 32) {
      step(1, 3, 0, 0, 1, 1, UINT64_C(1985229328), 1, 1, UINT64_C(1079356074));
      step(0, 3, 0, 0, 0, 0, 0, 1, 0, UINT64_C(3735928559));
    }

    eval();
    reset = 1;
    event_in = {1, {3, 0}};
    inhibit = 0;
    counter_write_in = write_port(1, 0, UINT64_MAX);
    selector_write_in = write_port(1, 0, UINT64_MAX);
    tick_model();
    CHECK(counter == 0 && selector == 0);
  });
}
