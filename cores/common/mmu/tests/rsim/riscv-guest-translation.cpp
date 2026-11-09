// Checks composed guest TLB reuse, current permissions, precise faults, fences,
// and canceled walk ownership.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int checks{};
constexpr std::uint8_t NONE = 0, PAGE = 1, GUEST = 2, PHYSICAL = 3;
constexpr std::uint8_t FETCH = 0, LOAD = 1, STORE = 2;
constexpr std::uint64_t VR = UINT64_C(0x43), VW = UINT64_C(0xc7),
                        GR = UINT64_C(0x53), GW = UINT64_C(0xd7);

void falling() {
  eval();
  eval();
}
std::uint64_t entry(std::uint64_t ppn, std::uint64_t flags) {
  return (ppn << 10) | flags;
}
void defaults() {
  vs_mode = 1;
  g_mode = 1;
  vs_root = 1;
  g_root = 4;
  privilege = 1;
  vs_sum = 0;
  vs_mxr = 0;
  hs_mxr = 0;
  vs_pbmte = 0;
  g_pbmte = 0;
}
void fence_all() {
  invalidate_all = 1;
  tick_model();
  falling();
  invalidate_all = 0;
  eval();
}
void launch(std::uint64_t va, std::uint8_t kind) {
  address = va;
  access = kind;
  command_valid = 1;
  eval();
  if (!command_ready)
    fail(1, "translation not available");
  tick_model();
  falling();
  command_valid = 0;
}
void wait_read(std::uint64_t expected) {
  int timeout;
  timeout = 0;
  while (!memory_valid && timeout < 80) {
    if (completed)
      fail(1, "unexpected cached completion");
    tick_model();
    timeout++;
  }
  if (!memory_valid || memory_address != expected || memory_pbmt != 0)
    fail(1, "expected PTE %h got %h valid=%b", expected, memory_address,
         memory_valid);
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index) {
    tick_model();
    if (!memory_valid || memory_address != expected)
      fail(1, "unstable offer");
  }
  falling();
  checks++;
}
void reply(std::uint64_t expected, std::uint64_t value) {
  wait_read(expected);
  memory_ready = 1;
  tick_model();
  falling();
  memory_ready = 0;
  pte = value;
  response_valid = 1;
  tick_model();
  falling();
  response_valid = 0;
}
void nested(std::uint64_t vs_flags = VW, std::uint64_t g_flags = GW,
            std::uint64_t root = UINT64_C(0x1000)) {
  reply(UINT64_C(0x4000), entry(UINT64_C(0x40000), GR));
  reply(UINT64_C(0x40000000) + root, entry(0, vs_flags));
  reply(UINT64_C(0x4000), entry(UINT64_C(0x40000), g_flags));
}
void finish(std::uint8_t expected_fault, std::uint64_t va, std::uint8_t kind,
            std::uint64_t pa = 0, std::uint64_t gpa = 0,
            bool implicit_pte = 0) {
  int timeout;
  std::uint64_t expected_cause;
  timeout = 0;
  while (!completed && timeout < 30) {
    if (memory_valid)
      fail(1, "unexpected rewalk for %h", va);
    tick_model();
    timeout++;
  }
  if (!completed || fault != expected_fault || result_virtual != va ||
      result_access != kind)
    fail(1, "bad completion va=%h fault=%d expected=%d", result_virtual, fault,
         expected_fault);
  if (expected_fault == NONE && (result_address != pa || result_guest != gpa))
    fail(1, "wrong composed addresses PA=%h GPA=%h", result_address,
         result_guest);
  if (expected_fault != NONE) {
    switch (expected_fault) {
    case PAGE:
      expected_cause = kind == FETCH ? 12 : kind == LOAD ? 13 : 15;

      break;
    case GUEST:
      expected_cause = kind == FETCH ? 20 : kind == LOAD ? 21 : 23;

      break;
    default:
      expected_cause = kind == FETCH ? 1 : kind == LOAD ? 5 : 7;

      break;
    }
    if (cause != expected_cause)
      fail(1, "wrong fault class %d expected %d", cause, expected_cause);
  }
  if (expected_fault == GUEST &&
      (fault_guest != gpa || implicit_read != implicit_pte ||
       tinst != (implicit_pte ? UINT64_C(12288) : UINT64_C(0))))
    fail(1, "incorrect guest fault provenance GPA=%h implicit=%b tinst=%h",
         fault_guest, implicit_read, tinst);
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
    tick_model();
    if (!completed || result_virtual != va || fault != expected_fault ||
        memory_valid)
      fail(1, "unstable held result");
  }
  falling();
  completion_ready = 1;
  tick_model();
  falling();
  completion_ready = 0;
  checks++;
}

int main() {
  return run_test([] {
    reset = 1;
    virtualized = 1;
    command_valid = 0;
    vs_mode = 1;
    g_mode = 1;
    address = 0;
    access = 1;
    privilege = 1;
    vs_root = 1;
    g_root = 4;
    vs_sum = 0;
    vs_mxr = 0;
    hs_mxr = 0;
    vs_pbmte = 0;
    g_pbmte = 0;
    cancel = 0;
    invalidate_all = 0;
    memory_ready = 0;
    memory_fault = 0;
    response_valid = 0;
    completion_ready = 0;
    pte = 0;

    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      tick_model();
    falling();
    reset = 0;

    defaults();
    g_mode = 0;
    virtualized = 0;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x1000), entry(UINT64_C(0x40000), VW));
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x40001234));
    virtualized = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x1000), entry(UINT64_C(0x80000), VW));
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(2147488308),
           UINT64_C(2147488308));
    virtualized = 0;
    launch(UINT64_C(0x1235), LOAD);
    finish(NONE, UINT64_C(0x1235), LOAD, UINT64_C(0x40001235),
           UINT64_C(0x40001235));

    launch(UINT64_C(0x2234), LOAD);
    finish(NONE, UINT64_C(0x2234), LOAD, UINT64_C(0x40002234),
           UINT64_C(0x40002234));
    virtualized = 1;
    launch(UINT64_C(0x1236), LOAD);
    finish(NONE, UINT64_C(0x1236), LOAD, UINT64_C(2147488310),
           UINT64_C(2147488310));
    launch(UINT64_C(0x2234), LOAD);
    reply(UINT64_C(0x1000), entry(UINT64_C(0x80000), VW));
    finish(NONE, UINT64_C(0x2234), LOAD, UINT64_C(2147492404),
           UINT64_C(2147492404));
    fence_all();

    defaults();
    launch(UINT64_C(0x1234), LOAD);
    nested();
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));
    launch(UINT64_C(0x1fed), STORE);
    finish(NONE, UINT64_C(0x1fed), STORE, UINT64_C(0x40001fed),
           UINT64_C(0x1fed));

    launch(UINT64_C(0x2234), LOAD);
    nested();
    finish(NONE, UINT64_C(0x2234), LOAD, UINT64_C(0x40002234),
           UINT64_C(0x2234));
    launch(UINT64_C(0x1235), LOAD);
    finish(NONE, UINT64_C(0x1235), LOAD, UINT64_C(0x40001235),
           UINT64_C(0x1235));

    launch(UINT64_C(0x3234), LOAD);
    nested();
    finish(NONE, UINT64_C(0x3234), LOAD, UINT64_C(0x40003234),
           UINT64_C(0x3234));
    launch(UINT64_C(0x1234), LOAD);
    nested();
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));

    fence_all();
    launch(UINT64_C(0x1234), LOAD);
    nested(VR, GR);
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));
    launch(UINT64_C(0x1278), STORE);
    finish(PAGE, UINT64_C(0x1278), STORE);
    fence_all();
    launch(UINT64_C(0x1234), LOAD);
    nested(VW, GR);
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));
    launch(UINT64_C(0x1278), STORE);
    finish(GUEST, UINT64_C(0x1278), STORE, 0, UINT64_C(0x1278));
    launch(UINT64_C(0x12ab), FETCH);
    finish(PAGE, UINT64_C(0x12ab), FETCH);
    fence_all();
    launch(UINT64_C(0x1234), LOAD);
    nested(UINT64_C(0x47), GW);
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));
    launch(UINT64_C(0x1278), STORE);
    finish(PAGE, UINT64_C(0x1278), STORE);
    fence_all();
    launch(UINT64_C(0x1234), LOAD);
    nested(VW, UINT64_C(0x57));
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));
    launch(UINT64_C(0x1278), STORE);
    finish(GUEST, UINT64_C(0x1278), STORE, 0, UINT64_C(0x1278));

    launch(UINT64_C(0x1279), UINT64_C(3));
    finish(NONE, UINT64_C(0x1279), UINT64_C(3), UINT64_C(0x40001279),
           UINT64_C(0x1279));
    fence_all();
    launch(UINT64_C(0x1234), LOAD);
    nested(UINT64_C(0x4b), GR);
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));
    launch(UINT64_C(0x12ab), FETCH);
    finish(GUEST, UINT64_C(0x12ab), FETCH, 0, UINT64_C(0x12ab));

    fence_all();
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x4000), entry(UINT64_C(0x40000), GR));
    reply(UINT64_C(0x40001000), entry(UINT64_C(0x80000), VW));
    reply(UINT64_C(0x4010), entry(UINT64_C(0xc0000), GR));
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(3221230132),
           UINT64_C(2147488308));
    launch(UINT64_C(0x12ab), STORE);
    finish(GUEST, UINT64_C(0x12ab), STORE, 0, UINT64_C(2147488427));

    fence_all();
    privilege = 0;
    launch(UINT64_C(0x1234), LOAD);
    nested(GR, GR);
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));
    privilege = 1;
    launch(UINT64_C(0x1234), LOAD);
    finish(PAGE, UINT64_C(0x1234), LOAD);
    vs_sum = 1;
    launch(UINT64_C(0x1234), LOAD);
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));
    fence_all();
    defaults();
    vs_mxr = 1;
    launch(UINT64_C(0x1234), LOAD);
    nested(UINT64_C(0x49), GR);
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));
    vs_mxr = 0;
    launch(UINT64_C(0x1234), LOAD);
    while (!completed)
      tick_model();
    if (!probe_hit || !probe_fault)
      fail(1, "disjoint stage permissions authorized a probe");
    finish(PAGE, UINT64_C(0x1234), LOAD);

    hs_mxr = 1;
    launch(UINT64_C(0x1234), LOAD);
    nested(UINT64_C(0x49), GR);
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));
    hs_mxr = 0;
    vs_mxr = 1;
    launch(UINT64_C(0x1234), LOAD);
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));

    fence_all();
    defaults();
    launch(UINT64_C(0x1234), LOAD);
    nested();
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));
    vs_root = 2;
    launch(UINT64_C(0x1234), LOAD);
    nested(VW, GW, UINT64_C(0x2000));
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));
    g_root = 8;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x8000), 0);
    finish(GUEST, UINT64_C(0x1234), LOAD, 0, UINT64_C(0x2000), 1);
    defaults();
    vs_pbmte = 1;
    launch(UINT64_C(0x1234), LOAD);
    nested();
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));
    vs_mode = 0;
    g_mode = 0;
    launch(UINT64_C(0x1234), LOAD);
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x1234), UINT64_C(0x1234));
    launch(UINT64_C(0x12ab), LOAD);
    finish(NONE, UINT64_C(0x12ab), LOAD, UINT64_C(0x12ab), UINT64_C(0x12ab));

    fence_all();
    defaults();
    vs_pbmte = 1;
    g_pbmte = 1;
    launch(UINT64_C(0x1234), LOAD);
    nested(VW | (UINT64_C(1) << 61), GW | (UINT64_C(2) << 61));
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));
    launch(UINT64_C(0x12ab), LOAD);
    while (!completed)
      tick_model();
    if (slice(vs_leaf, 62, 61) != 1 || slice(g_leaf, 62, 61) != 2)
      fail(1, "cached PBMT lost");
    if (!probe_hit || probe_fault || probe_pbmt != 1)
      fail(1, "VS probe PBMT must override G");
    finish(NONE, UINT64_C(0x12ab), LOAD, UINT64_C(0x400012ab),
           UINT64_C(0x12ab));
    fence_all();
    launch(UINT64_C(0x1234), LOAD);
    nested(VW, GW | (UINT64_C(2) << 61));
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));
    launch(UINT64_C(0x12ab), LOAD);
    while (!completed)
      tick_model();
    if (!probe_hit || probe_fault || probe_pbmt != 2)
      fail(1, "PMA VS leaf must inherit G PBMT");
    finish(NONE, UINT64_C(0x12ab), LOAD, UINT64_C(0x400012ab),
           UINT64_C(0x12ab));

    fence_all();
    defaults();
    for (int kind = 0; kind < 4; kind++) {
      launch(UINT64_C(0x1234), slice(kind, 1, 0));
      reply(UINT64_C(0x4000), 0);
      finish(GUEST, UINT64_C(0x1234), slice(kind, 1, 0), 0, UINT64_C(0x1000),
             1);
    }
    launch(UINT64_C(0x1234), STORE);
    nested();
    finish(NONE, UINT64_C(0x1234), STORE, UINT64_C(0x40001234),
           UINT64_C(0x1234));
    fence_all();
    launch(UINT64_C(0x1234), STORE);
    wait_read(UINT64_C(0x4000));
    memory_ready = 1;
    memory_fault = 1;
    tick_model();
    falling();
    memory_ready = 0;
    memory_fault = 0;
    finish(PHYSICAL, UINT64_C(0x1234), STORE);
    launch(UINT64_C(0x1234), LOAD);
    nested();
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));

    fence_all();
    launch(UINT64_C(0x1234), LOAD);
    nested();
    fence_all();
    if (completed)
      fail(1, "fenced refill completion escaped");
    launch(UINT64_C(0x1234), LOAD);
    nested();
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));

    launch(UINT64_C(0x123a), LOAD);
    address = UINT64_C(0xabc);
    vs_root = 99;
    access = STORE;
    finish(NONE, UINT64_C(0x123a), LOAD, UINT64_C(0x4000123a),
           UINT64_C(0x123a));
    defaults();
    launch(UINT64_C(0x1234), LOAD);
    while (!completed)
      tick_model();
    falling();
    fence_all();
    if (completed || !command_ready)
      fail(1, "fence did not cancel held result");
    launch(UINT64_C(0x1234), LOAD);
    nested();
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));

    fence_all();
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x4000), entry(UINT64_C(0x40000), GR));
    reply(UINT64_C(0x40001000), entry(0, VW));
    wait_read(UINT64_C(0x4000));
    memory_ready = 1;
    tick_model();
    falling();
    memory_ready = 0;
    response_valid = 1;
    pte = entry(UINT64_C(0x40000), GW);
    fence_all();
    response_valid = 0;
    if (completed)
      fail(1, "fenced response escaped");
    launch(UINT64_C(0x1234), LOAD);
    nested();
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40001234),
           UINT64_C(0x1234));

    fence_all();
    launch(UINT64_C(0x1234), LOAD);
    wait_read(UINT64_C(0x4000));
    memory_ready = 1;
    tick_model();
    falling();
    memory_ready = 0;
    fence_all();
    command_valid = 1;
    address = UINT64_C(0x5678);
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
      tick_model();
      if (command_ready || memory_valid || completed)
        fail(1, "orphan owner lost");
    }
    falling();
    fence_all();
    if (command_ready)
      fail(1, "second fence forgot orphan");
    response_valid = 1;
    pte = entry(UINT64_C(0x40000), GW);
    tick_model();
    falling();
    response_valid = 0;
    command_valid = 0;
    launch(UINT64_C(0x5678), LOAD);
    nested();
    finish(NONE, UINT64_C(0x5678), LOAD, UINT64_C(0x40005678),
           UINT64_C(0x5678));

    launch(UINT64_C(0x5678), LOAD);
    cancel = 1;
    tick_model();
    falling();
    cancel = 0;
    eval();
    if (completed)
      fail(1, "canceled result escaped");
    launch(UINT64_C(0x5679), LOAD);
    finish(NONE, UINT64_C(0x5679), LOAD, UINT64_C(0x40005679),
           UINT64_C(0x5679));

    reset = 1;
    tick_model();
    falling();
    reset = 0;
    launch(UINT64_C(0x5678), LOAD);
    nested();
    finish(NONE, UINT64_C(0x5678), LOAD, UINT64_C(0x40005678),
           UINT64_C(0x5678));
  });
}
