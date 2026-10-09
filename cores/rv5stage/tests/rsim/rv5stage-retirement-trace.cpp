// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
extern "C" void retirement_trace_init();
extern "C" void retirement_trace_expect(std::uint64_t pc, unsigned instruction,
                                        unsigned prediction,
                                        unsigned ras_mismatch);
extern "C" void retirement_trace_check(unsigned reset);
extern "C" void retirement_trace_pending(unsigned count);
extern "C" void retirement_trace_finish();
std::uint16_t saved_tag = 0;
void tick() {
  eval();
  if (data_valid && data_ready)
    saved_tag = data_tag;
  tick_model();
  retirement_trace_check(unsigned(reset));
}
void idle(int cycles = 8) {
  for (unsigned repeat_index = 0; repeat_index < (cycles); ++repeat_index)
    tick();
}
void packet(std::uint64_t address, std::uint32_t word, bool prediction = 0,
            std::uint64_t destination = 0, std::uint8_t ras_action = 0) {
  int waited;
  pc = address;
  instruction = word;
  predicted = prediction;
  target = destination;
  ras = ras_action;
  packet_valid = 1;
  waited = 0;
  eval();
  while (!packet_ready) {
    tick();
    waited++;
    if (waited > 100)
      fail(1, "packet admission timeout");
  }
  tick();
  packet_valid = 0;
}
void retire(std::uint64_t address, std::uint32_t word, int status = 0,
            bool prediction = 0, std::uint64_t destination = 0,
            bool ras_mismatch = 0, std::uint8_t ras_action = 0) {
  retirement_trace_expect(address, word, status, unsigned(ras_mismatch));
  packet(address, word, prediction, destination, ras_action);
  idle();
  retirement_trace_pending(0);
}
void respond(bool fault = 0) {
  response_tag = saved_tag;
  response_fault = fault;
  response_valid = 1;
  tick();
  response_valid = 0;
  idle();
}
int main() {
  return run_test([] {
    reset = 1;
    packet_valid = 0;
    predicted = 0;
    pc = 0;
    target = 0;
    instruction = 0;
    ras = 0;
    data_ready = 1;
    data_fault = 0;
    response_valid = 0;
    response_fault = 0;
    reservation = 0;
    response_tag = 0;
    retirement_trace_init();
    idle(3);
    reset = 0;
    idle(3);
    retire(UINT64_C(0x0), UINT64_C(134217875));
    retire(UINT64_C(0x10), UINT64_C(1123), 2);
    retire(UINT64_C(0x10), UINT64_C(1123), 1, 1, UINT64_C(0x18));
    retire(UINT64_C(0x20), UINT64_C(5219), 1);
    retire(UINT64_C(0x20), UINT64_C(5219), 2, 1, UINT64_C(0x28));
    retire(UINT64_C(0x30), UINT64_C(1123), 2, 1, UINT64_C(0x3c));
    retire(UINT64_C(0x40), UINT64_C(8388719), 1, 1, UINT64_C(0x48));
    retire(UINT64_C(0x50), UINT64_C(32871), 1, 1, UINT64_C(0x80), 1);
    retire(UINT64_C(0x50), UINT64_C(32871), 2, 1, UINT64_C(0x84), 1);
    retire(UINT64_C(0x50), UINT64_C(32871), 1, 1, UINT64_C(0x80), 0, 2);
    retire(UINT64_C(0x50), UINT64_C(32871), 2, 1, UINT64_C(0x84), 0, 2);
    retire(UINT64_C(0x54), UINT64_C(8389359), 1, 1, UINT64_C(0x5c), 1, 2);
    retire(UINT64_C(0x54), UINT64_C(8389359), 1, 1, UINT64_C(0x5c), 0, 1);
    retire(UINT64_C(0x58), UINT64_C(33511), 1, 1, UINT64_C(0x80), 1, 1);
    retire(UINT64_C(0x58), UINT64_C(33511), 1, 1, UINT64_C(0x80), 0, 3);
    retire(UINT64_C(0x5c), UINT64_C(32898), 1, 1, UINT64_C(0x80), 1);
    retire(UINT64_C(0x5c), UINT64_C(32898), 1, 1, UINT64_C(0x80), 0, 2);
    retire(UINT64_C(0x64), UINT64_C(19), 0, 1, UINT64_C(0x68), 1, 1);
    retire(UINT64_C(0x60), UINT64_C(40961), 1, 1, UINT64_C(0x60));
    retire(UINT64_C(0x60), UINT64_C(40961), 2);
    retirement_trace_expect(UINT64_C(0x70), UINT64_C(1123), 2, 0);
    packet(UINT64_C(0x70), UINT64_C(1123));
    packet(UINT64_C(0x74), UINT64_C(1048851));
    idle();
    retirement_trace_pending(0);

    data_ready = 0;
    packet(UINT64_C(0x80), UINT64_C(12803), 1, UINT64_C(0x84), 1);
    idle();
    retirement_trace_pending(0);
    data_ready = 1;
    retire(UINT64_C(0x80), UINT64_C(12803));
    respond();

    packet(UINT64_C(0x90), UINT64_C(115));
    idle();
    retirement_trace_pending(0);
    packet(UINT64_C(0x94), UINT64_C(4293927667), 1, UINT64_C(0x98), 1);
    idle();
    retirement_trace_pending(0);
    data_fault = 1;
    packet(UINT64_C(0x98), UINT64_C(12803));
    idle();
    retirement_trace_pending(0);
    data_fault = 0;
    retire(UINT64_C(0x9c), UINT64_C(807403635));

    reservation = 1;
    retirement_trace_expect(UINT64_C(0xa0), UINT64_C(13631603), 0, 1);
    packet(UINT64_C(0xa0), UINT64_C(13631603), 1, UINT64_C(0xa4), 1);
    idle(12);
    retirement_trace_pending(1);
    reservation = 0;
    idle();
    retirement_trace_pending(0);

    retirement_trace_expect(UINT64_C(0xb0), UINT64_C(1056783), 0, 1);
    packet(UINT64_C(0xb0), UINT64_C(1056783), 1, UINT64_C(0xb4), 1);
    idle(12);
    retirement_trace_pending(1);
    respond();
    retirement_trace_pending(0);
    packet(UINT64_C(0xb4), UINT64_C(1056783));
    idle(12);
    retirement_trace_pending(0);
    respond(1);
    retirement_trace_pending(0);
    retire(UINT64_C(0xc0), UINT64_C(1048851));
    retire(UINT64_C(0xc4), UINT64_C(2097555));
    retire(UINT64_C(0xc8), UINT64_C(3146259));
    retire(UINT64_C(0xcc), UINT64_C(4194963));
    retirement_trace_finish();
  });
}
