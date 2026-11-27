// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

constexpr uint64_t page_bytes = 4096;
constexpr uint64_t page_mask = page_bytes - 1;

void start(uint64_t lo, uint64_t hi, bool store) {
  check_in.prequest = {1, {lo, hi, store}};
  eval();
  CHECK(check_out.prequest.pready && !translation_out.prequest.pvalid);
  tick_model();
  check_in.prequest = {};
}

void probe(uint64_t va, uint64_t pa, bool store) {
  for (unsigned cycle = 0; cycle < 3; ++cycle) {
    check_in.prequest.pbits = {va ^ (page_bytes << cycle), 0, !store};
    eval();
    CHECK(translation_out.prequest.pvalid && translation_out.prequest.pbits.paddress == va);
    CHECK(translation_out.prequest.pbits.pstore == store && !check_out.presponse.pvalid && !owned);
    tick_model();
  }
  translation_in.prequest.pready = 1;
  translation_in.presponse.pvalid = 1;
  translation_in.presponse.pbits.pphysical_uaddress = pa | (va & page_mask);
  tick_model();
  translation_in = {};
}

void accept(bool safe) {
  for (unsigned cycle = 0; cycle < 3; ++cycle) {
    eval();
    CHECK(check_out.presponse.pvalid && check_out.presponse.pbits == safe);
    CHECK(!translation_out.prequest.pvalid && !owned);
    tick_model();
  }
  check_in.presponse.pready = 1;
  tick_model();
  check_in.presponse.pready = 0;
  eval();
  CHECK(owned == safe);
}

void release() {
  check_in.prelease.pvalid = 1;
  tick_model();
  check_in.prelease.pvalid = 0;
  eval();
  CHECK(!owned && !hits[0] && !hits[1] && check_out.prequest.pready);
}

void check_span(uint64_t lo, uint64_t hi, bool store) {
  const bool split = (lo >> 12) != (hi >> 12);
  start(lo, hi, store);
  probe(lo, 0x90000, store);
  if (split) probe(hi, 0x35000, store);
  accept(true);
  const uint64_t cases[] = {lo, hi, lo - 1, hi + 1, lo | page_mask, hi & ~page_mask,
                            lo - 2 * page_bytes, hi + 2 * page_bytes};
  for (uint64_t a : cases) {
    for (uint64_t b : cases) {
      addresses[0] = a;
      addresses[1] = b;
      eval();
      for (unsigned port = 0; port < 2; ++port) {
        const uint64_t va = addresses[port];
        const bool hit = lo <= va && va <= hi;
        CHECK(hits[port] == hit);
        if (hit) {
          const uint64_t pa = (va >> 12) == (lo >> 12) ? 0x90000 : 0x35000;
          CHECK(physical_addresses[port] == (pa | (va & page_mask)));
        }
      }
    }
  }
  release();
}

int main() {
  return run_test([] {
    reset = 1; tick_model(); reset = 0;
    check_span(0x8012, 0x80f3, false);
    check_span(UINT64_MAX, UINT64_MAX, true);
    // Carries of every length, with deliberately nonadjacent physical pages.
    for (unsigned bit = 12; bit < 64; ++bit) {
      const uint64_t boundary = UINT64_C(1) << bit;
      check_span(boundary - 8, boundary + 15, true);
    }
    for (uint64_t invalid_end : {UINT64_C(0x7004), UINT64_C(0x8001), UINT64_C(0xa007)}) {
      start(0x8008, invalid_end, false);
      accept(false);
      release();
    }
    start(UINT64_MAX - 1, 7, true);
    accept(false);
    release();
  });
}
