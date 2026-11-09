// Checks nested VS/G walks, stage permissions, precise faults, and
// canceled-response ownership.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int reads{};
int checks{};
constexpr unsigned NONE = 0, PAGE = 1, GUEST = 2, PHYSICAL = 3;
constexpr unsigned FETCH = 0, LOAD = 1, STORE = 2;
constexpr std::uint64_t VR = UINT64_C(0x43), VW = UINT64_C(0xc7),
                        GX = UINT64_C(0x59), GR = UINT64_C(0x53),
                        GW = UINT64_C(0xd7);

void falling() {
  eval();
  eval();
}
std::uint64_t entry(std::uint64_t ppn, std::uint64_t flags) {
  return (ppn << 10) | flags;
}
void defaults() {
  vs_mode = 0;
  g_mode = 0;
  vs_root = 1;
  g_root = 4;
  privilege = 1;
  vs_sum = 0;
  vs_mxr = 0;
  hs_mxr = 0;
  vs_pbmte = 0;
  g_pbmte = 0;
}
void launch(std::uint64_t va, std::uint8_t kind) {
  address = va;
  access = kind;
  command_valid = 1;
  eval();
  if (!command_ready)
    fail(1, "walker not available");
  tick_model();
  falling();
  command_valid = 0;
}
void wait_read(std::uint64_t expected, std::uint8_t expected_pbmt = 0) {
  int timeout;
  timeout = 0;
  while (!memory_valid && timeout < 60) {
    tick_model();
    timeout++;
  }
  if (!memory_valid || memory_address != expected ||
      memory_pbmt != expected_pbmt)
    fail(1, "PTE request expected %h, got valid=%b address=%h", expected,
         memory_valid, memory_address);
  checks++;

  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index) {
    tick_model();
    if (!memory_valid || memory_address != expected ||
        memory_pbmt != expected_pbmt)
      fail(1, "unstable PTE offer");
  }
  falling();
}
void reply(std::uint64_t expected, std::uint64_t value,
           std::uint8_t expected_pbmt = 0) {
  wait_read(expected, expected_pbmt);
  memory_ready = 1;
  tick_model();
  falling();
  memory_ready = 0;
  reads++;
  tick_model();
  falling();
  pte = value;
  response_valid = 1;
  tick_model();
  falling();
  response_valid = 0;
}
void translate_g(std::uint64_t gpa, std::uint64_t flags) {
  reply(UINT64_C(0x4000) + (((gpa >> 30) & UINT64_C(0x7ff)) << 3), entry(8, 1));
  reply(UINT64_C(0x8000) + (((gpa >> 21) & UINT64_C(0x1ff)) << 3), entry(9, 1));
  reply(UINT64_C(0x9000) + (((gpa >> 12) & UINT64_C(0x1ff)) << 3),
        entry(UINT64_C(256) + ((gpa >> 12) & low_mask(44)), flags));
}
void finish(std::uint8_t expected_fault, std::uint64_t va, std::uint8_t kind,
            std::uint64_t pa, std::uint64_t fault_gpa = 0,
            bool implicit_pte = 0) {
  int timeout;
  timeout = 0;
  while (!completed && timeout < 60) {
    tick_model();
    timeout++;
  }
  if (!completed || fault != expected_fault || result_virtual != va ||
      result_access != kind)
    fail(1,
         "completion expected fault=%d va=%h access=%d, got valid=%b fault=%d "
         "va=%h access=%d",
         expected_fault, va, kind, completed, fault, result_virtual,
         result_access);
  if (expected_fault == NONE && result_address != pa)
    fail(1, "physical address expected %h got %h", pa, result_address);
  if (expected_fault == GUEST &&
      (fault_guest != fault_gpa || implicit_read != implicit_pte || !gva))
    fail(1,
         "guest provenance expected GPA=%h implicit=%b got GPA=%h implicit=%b",
         fault_gpa, implicit_pte, fault_guest, implicit_read);
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
    tick_model();
    if (!completed || fault != expected_fault || result_virtual != va ||
        memory_valid)
      fail(1, "unstable completion");
  }
  falling();
  completion_ready = 1;
  tick_model();
  falling();
  completion_ready = 0;
  checks++;
}
void cancel_now() {
  cancel = 1;
  tick_model();
  falling();
  cancel = 0;
  eval();
}

int main() {
  return run_test([] {
    reset = 1;
    virtualized = 1;
    command_valid = 0;
    vs_mode = 0;
    g_mode = 0;
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
    launch(UINT64_C(0x123456), LOAD);
    finish(NONE, UINT64_C(0x123456), LOAD, UINT64_C(0x123456));

    defaults();
    launch(UINT64_C(549755813888), FETCH);
    finish(NONE, UINT64_C(549755813888), FETCH, UINT64_C(549755813888));
    defaults();
    launch(UINT64_C(72057594037927936), LOAD);
    finish(PHYSICAL, UINT64_C(72057594037927936), LOAD, 0);
    defaults();
    vs_mode = 1;
    launch(UINT64_C(549755813888), LOAD);
    finish(PAGE, UINT64_C(549755813888), LOAD, 0);

    defaults();
    vs_mode = 1;
    launch(UINT64_C(0x1234), LOAD);
    defaults();
    vs_root = 99;
    privilege = 0;
    reply(UINT64_C(0x1000), entry(2, UINT64_C(0x21)));
    reply(UINT64_C(0x2000), entry(3, 1));
    reply(UINT64_C(0x3008), entry(5, VR));
    while (!completed)
      tick_model();
    if (!((vs_leaf >> 5) & 1) || g_leaf != 0 ||
        result_guest != UINT64_C(0x5234))
      fail(1, "VS leaf or Bare G snapshot");
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x5234));

    defaults();
    g_mode = 1;
    launch(UINT64_C(1099511632436), LOAD);
    reply(UINT64_C(0x6000), entry(8, 1));
    reply(UINT64_C(0x8000), entry(9, 1));
    reply(UINT64_C(0x9008), entry(UINT64_C(0x222), GR | UINT64_C(0x20)));
    while (!completed)
      tick_model();
    if (((g_leaf >> 5) & 1) || vs_leaf != 0)
      fail(1, "G global bool must be ignored");
    finish(NONE, UINT64_C(1099511632436), LOAD, UINT64_C(0x222234));
    defaults();
    g_mode = 1;
    launch(UINT64_C(2199023255552), LOAD);
    finish(GUEST, UINT64_C(2199023255552), LOAD, 0, UINT64_C(2199023255552));
    defaults();
    g_mode = 1;
    g_root = 5;
    launch(UINT64_C(0x1234), LOAD);
    finish(GUEST, UINT64_C(0x1234), LOAD, 0, UINT64_C(0x1234));
    defaults();
    vs_mode = 1;
    g_mode = 1;
    vs_root = UINT64_C(536870912);
    launch(UINT64_C(0x1234), LOAD);
    finish(GUEST, UINT64_C(0x1234), LOAD, 0, UINT64_C(2199023255552), 1);

    defaults();
    vs_mode = 1;
    g_mode = 1;
    reads = 0;
    launch(UINT64_C(0x1234), STORE);
    translate_g(UINT64_C(0x1000), GR);
    reply(UINT64_C(0x101000), entry(2, 1));
    translate_g(UINT64_C(0x2000), GR);
    reply(UINT64_C(0x102000), entry(3, 1));
    translate_g(UINT64_C(0x3008), GR);
    reply(UINT64_C(0x103008), entry(5, VW));
    translate_g(UINT64_C(0x5234), GW);
    finish(NONE, UINT64_C(0x1234), STORE, UINT64_C(0x105234));
    if (reads != 15)
      fail(1, "nested walk bypassed a stage");

    defaults();
    vs_mode = 1;
    g_mode = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x4000), entry(UINT64_C(0x40000), GR));
    reply(UINT64_C(0x40001000), entry(2, 1));
    reply(UINT64_C(0x4000), entry(UINT64_C(0x40000), GR));
    reply(UINT64_C(0x40002000), entry(UINT64_C(0x200), VR));
    reply(UINT64_C(0x4000), entry(UINT64_C(0x40000), GR));
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x40201234));

    for (int kind = 0; kind < 3; kind++) {
      defaults();
      vs_mode = 1;
      g_mode = 1;
      launch(UINT64_C(0x1234), slice(kind, 1, 0));
      reply(UINT64_C(0x4000), 0);
      finish(GUEST, UINT64_C(0x1234), slice(kind, 1, 0), 0, UINT64_C(0x1000),
             1);
    }
    defaults();
    vs_mode = 1;
    g_mode = 1;
    launch(UINT64_C(0x1234), FETCH);
    reply(UINT64_C(0x4000), entry(0, GR));
    reply(UINT64_C(0x1000), 0);
    finish(PAGE, UINT64_C(0x1234), FETCH, 0);
    defaults();
    vs_mode = 1;
    g_mode = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x4000), entry(0, GR));
    reply(UINT64_C(0x1000), entry(0, VR));
    reply(UINT64_C(0x4000), 0);
    finish(GUEST, UINT64_C(0x1234), LOAD, 0, UINT64_C(0x1234));

    for (int mxr_bits = 0; mxr_bits < 4; mxr_bits++) {
      for (int kind = 0; kind < 3; kind++) {
        defaults();
        vs_mode = 1;
        g_mode = 1;
        hs_mxr = ((mxr_bits >> (0)) & 1);
        vs_mxr = ((mxr_bits >> (1)) & 1);
        launch(UINT64_C(0x1234), slice(kind, 1, 0));
        reply(UINT64_C(0x4000), entry(0, GX));
        finish(GUEST, UINT64_C(0x1234), slice(kind, 1, 0), 0, UINT64_C(0x1000),
               1);
      }
    }

    defaults();
    vs_mode = 1;
    g_mode = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x4000), entry(0, VR));
    finish(GUEST, UINT64_C(0x1234), LOAD, 0, UINT64_C(0x1000), 1);
    defaults();
    g_mode = 1;
    vs_mxr = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x4000), entry(0, GX));
    finish(GUEST, UINT64_C(0x1234), LOAD, 0, UINT64_C(0x1234));
    defaults();
    g_mode = 1;
    hs_mxr = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x4000), entry(0, GX));
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x1234));
    defaults();
    vs_mode = 1;
    vs_mxr = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x1000), entry(0, UINT64_C(0x49)));
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x1234));
    defaults();
    vs_mode = 1;
    hs_mxr = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x1000), entry(0, UINT64_C(0x49)));
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x1234));
    defaults();
    vs_mode = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x1000), entry(0, GR));
    finish(PAGE, UINT64_C(0x1234), LOAD, 0);
    defaults();
    vs_mode = 1;
    vs_sum = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x1000), entry(0, GR));
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x1234));
    defaults();
    vs_mode = 1;
    vs_sum = 1;
    launch(UINT64_C(0x1234), FETCH);
    reply(UINT64_C(0x1000), entry(0, GX));
    finish(PAGE, UINT64_C(0x1234), FETCH, 0);
    defaults();
    vs_mode = 1;
    privilege = 0;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x1000), entry(0, VR));
    finish(PAGE, UINT64_C(0x1234), LOAD, 0);
    defaults();
    vs_mode = 1;
    privilege = 0;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x1000), entry(0, GR));
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x1234));
    defaults();
    g_mode = 1;
    launch(UINT64_C(0x1234), FETCH);
    reply(UINT64_C(0x4000), entry(0, GX));
    finish(NONE, UINT64_C(0x1234), FETCH, UINT64_C(0x1234));

    defaults();
    vs_mode = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x1000), entry(2, 1));
    reply(UINT64_C(0x2000), entry(3, 1));
    reply(UINT64_C(0x3008), entry(8, VR | (UINT64_C(1) << 63)));
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x1234));
    defaults();
    g_mode = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x4000), entry(8, 1));
    reply(UINT64_C(0x8000), entry(9, 1));
    reply(UINT64_C(0x9008), entry(UINT64_C(0x108), GR | (UINT64_C(1) << 63)));
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x101234));

    defaults();
    g_mode = 1;
    launch(UINT64_C(0x1234), STORE);
    reply(UINT64_C(0x4000), entry(0, GW & ~UINT64_C(128)));
    finish(GUEST, UINT64_C(0x1234), STORE, 0, UINT64_C(0x1234));
    defaults();
    vs_mode = 1;
    launch(UINT64_C(0x1234), STORE);
    reply(UINT64_C(0x1000), entry(0, VW & ~UINT64_C(128)));
    finish(PAGE, UINT64_C(0x1234), STORE, 0);
    defaults();
    g_mode = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x4000), entry(0, GR & ~UINT64_C(64)));
    finish(GUEST, UINT64_C(0x1234), LOAD, 0, UINT64_C(0x1234));
    defaults();
    vs_mode = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x1000), entry(1, VR));
    finish(PAGE, UINT64_C(0x1234), LOAD, 0);
    defaults();
    g_mode = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x4000), entry(0, GR | (UINT64_C(1) << 54)));
    finish(GUEST, UINT64_C(0x1234), LOAD, 0, UINT64_C(0x1234));
    defaults();
    g_mode = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x4000), entry(1, GR));
    finish(GUEST, UINT64_C(0x1234), LOAD, 0, UINT64_C(0x1234));
    defaults();
    g_mode = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x4000), entry(8, UINT64_C(0x41)));
    finish(GUEST, UINT64_C(0x1234), LOAD, 0, UINT64_C(0x1234));
    defaults();
    vs_mode = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x1000), entry(2, 1));
    reply(UINT64_C(0x2000), entry(3, 1));
    reply(UINT64_C(0x3008), entry(4, 1));
    finish(PAGE, UINT64_C(0x1234), LOAD, 0);

    defaults();
    vs_mode = 1;
    vs_pbmte = 1;
    launch(UINT64_C(0x1234), LOAD);
    vs_pbmte = 0;
    reply(UINT64_C(0x1000), entry(0, VR | (UINT64_C(2) << 61)));
    while (!completed)
      tick_model();
    if (slice(vs_leaf, 62, 61) != 2)
      fail(1, "VS PBMT or captured PBMTE lost");
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x1234));
    defaults();
    g_mode = 1;
    vs_pbmte = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x4000), entry(0, GR | (UINT64_C(1) << 61)));
    finish(GUEST, UINT64_C(0x1234), LOAD, 0, UINT64_C(0x1234));
    defaults();
    g_mode = 1;
    g_pbmte = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x4000), entry(0, GR | (UINT64_C(1) << 61)));
    while (!completed)
      tick_model();
    if (slice(g_leaf, 62, 61) != 1)
      fail(1, "G PBMT lost");
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x1234));

    defaults();
    vs_mode = 1;
    g_mode = 1;
    g_pbmte = 1;
    launch(UINT64_C(0x1234), LOAD);
    reply(UINT64_C(0x4000), entry(0, GR | (UINT64_C(1) << 61)));
    reply(UINT64_C(0x1000), entry(0, VR), 1);
    reply(UINT64_C(0x4000), entry(0, GR));
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x1234));

    defaults();
    vs_mode = 1;
    g_mode = 1;
    launch(UINT64_C(0x1234), STORE);
    wait_read(UINT64_C(0x4000));
    memory_fault = 1;
    memory_ready = 1;
    tick_model();
    falling();
    memory_fault = 0;
    memory_ready = 0;
    finish(PHYSICAL, UINT64_C(0x1234), STORE, 0);

    defaults();
    g_mode = 1;
    launch(UINT64_C(0x1234), LOAD);
    wait_read(UINT64_C(0x4000));
    memory_ready = 1;
    response_valid = 1;
    pte = entry(0, GR);
    tick_model();
    falling();
    memory_ready = 0;
    response_valid = 0;
    finish(NONE, UINT64_C(0x1234), LOAD, UINT64_C(0x1234));

    defaults();
    g_mode = 1;
    launch(UINT64_C(0x1234), LOAD);
    wait_read(UINT64_C(0x4000));
    cancel_now();
    if (!command_ready || memory_valid || completed)
      fail(1, "unaccepted cancel did not release");
    defaults();
    g_mode = 1;
    launch(UINT64_C(0x1234), LOAD);
    wait_read(UINT64_C(0x4000));
    memory_ready = 1;
    tick_model();
    falling();
    memory_ready = 0;
    cancel_now();
    command_valid = 1;
    address = UINT64_C(0x5678);
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
      tick_model();
      if (command_ready || memory_valid || completed)
        fail(1, "orphan response owner lost");
    }
    falling();
    cancel_now();
    if (command_ready)
      fail(1, "repeated cancellation forgot pending PTE");
    response_valid = 1;
    pte = entry(0, GR);
    tick_model();
    falling();
    response_valid = 0;
    command_valid = 0;
    if (!command_ready || completed)
      fail(1, "drain did not release silently");
    defaults();
    g_mode = 1;
    launch(UINT64_C(0x5678), LOAD);
    reply(UINT64_C(0x4000), 0);
    finish(GUEST, UINT64_C(0x5678), LOAD, 0, UINT64_C(0x5678));
    defaults();
    g_mode = 1;
    launch(UINT64_C(0x1234), LOAD);
    wait_read(UINT64_C(0x4000));
    memory_ready = 1;
    tick_model();
    falling();
    memory_ready = 0;
    response_valid = 1;
    pte = entry(0, GR);
    cancel_now();
    response_valid = 0;
    if (!command_ready || completed)
      fail(1, "cancel on response edge leaked completion");
    defaults();
    launch(UINT64_C(0x1234), LOAD);
    while (!completed)
      tick_model();
    falling();
    cancel_now();
    if (!command_ready || completed)
      fail(1, "canceled completion escaped");
    command_valid = 1;
    cancel = 1;
    eval();
    if (command_ready)
      fail(1, "admitted during cancel");
    tick_model();
    falling();
    command_valid = 0;
    cancel = 0;
    defaults();
    launch(UINT64_C(0x4321), LOAD);
    finish(NONE, UINT64_C(0x4321), LOAD, UINT64_C(0x4321));
  });
}
