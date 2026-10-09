// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
extern "C" void walk_bind();
extern "C" void walk_sample(unsigned reset, unsigned cancel, unsigned start,
                            std::uint64_t address, unsigned request,
                            std::uint64_t memory_address, unsigned finish,
                            unsigned fault, unsigned access_fault);
extern "C" void walk_check();
extern "C" void walk_finish();
void tick() {
  eval();

  walk_sample(unsigned(reset), unsigned(cancel),
              unsigned(command_valid && command_ready), address,
              unsigned(memory_valid && memory_ready), memory_address,
              unsigned(completed && completion_ready), unsigned(fault),
              unsigned(access_fault));
  tick_model();
  walk_check();
}
void start(std::uint64_t location = UINT64_C(16384)) {
  address = location;
  command_valid = 1;
  eval();
  CHECK(command_ready);
  tick();
  command_valid = 0;
}
void read_pte(std::uint64_t expected_address, std::uint64_t value) {
  eval();
  CHECK(memory_valid && memory_address == expected_address);
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    tick();
  memory_ready = 1;
  tick();
  memory_ready = 0;
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    tick();
  pte = value;
  response_valid = 1;
  tick();
  response_valid = 0;
}
void finish(bool page_fault = 0, bool bus_fault = 0) {
  eval();
  CHECK(completed && fault == page_fault && access_fault == bus_fault);
  for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
    tick();
  completion_ready = 1;
  tick();
  completion_ready = 0;
  CHECK(command_ready && !completed);
}
int main() {
  return run_test([] {
    reset = 1;
    command_valid = 0;
    cancel = 0;
    address = 0x4000;
    pte = 0;
    memory_ready = 0;
    memory_fault = 0;
    response_valid = 0;
    completion_ready = 0;
    walk_bind();
    tick();
    reset = 0;
    tick();

    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
      start();
      read_pte(0x1000, (0x2 << 10) | 1);
      read_pte(0x2000, (0x3 << 10) | 1);
      read_pte(0x3020, (0x5 << 10) | 0xc3);
      finish();
    }
    start();
    read_pte(0x1000, 0);
    finish(1, 0);
    start(UINT64_C(549755813888));
    finish(1, 0);
    start();
    memory_ready = 1;
    memory_fault = 1;
    tick();
    memory_ready = 0;
    memory_fault = 0;
    finish(0, 1);

    start();
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      tick();
    cancel = 1;
    tick();
    cancel = 0;
    start();
    memory_ready = 1;
    tick();
    memory_ready = 0;
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      tick();
    cancel = 1;
    tick();
    cancel = 0;

    CHECK(!command_ready);
    response_valid = 1;
    pte = 0;
    tick();
    response_valid = 0;
    start(UINT64_C(549755813888));
    cancel = 1;
    completion_ready = 1;
    tick();
    cancel = 0;
    completion_ready = 0;

    command_valid = 1;
    cancel = 1;
    tick();
    command_valid = 0;
    cancel = 0;
    start();
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick();
    reset = 1;
    tick();
    reset = 0;
    tick();
    start();
    read_pte(0x1000, (0x2 << 10) | 1);
    read_pte(0x2000, (0x3 << 10) | 1);
    read_pte(0x3020, (0x5 << 10) | 0xc3);
    finish();
    tick();
    walk_finish();
  });
}
