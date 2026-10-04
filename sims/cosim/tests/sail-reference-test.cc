// Qualifies real embedded Sail stepping, memory replay, traps, and FESVR coexistence.
// SPDX-License-Identifier: Apache-2.0
#include "sail-reference.h"
#include "config_utils.h"
#include "direct_mem_htif.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace rhodium::cosim;
namespace {
constexpr std::uint64_t ram = 0x80000000;
constexpr std::array backing = {MemoryRange{ram, 0x10000}, MemoryRange{0x1000, 0x1000}};
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(std::string(__func__) + ": " #condition); } while (false)

template<class Exception, class F> void rejects(F&& action) {
  try { action(); } catch (const Exception&) { return; }
  throw std::runtime_error("expected rejection");
}

std::string configuration(unsigned xlen) {
  auto config = jsoncons::json::parse(xlen == 32 ? get_default_rv32_config() : get_default_config());
  config["platform"]["clint"]["supported"] = false;
  config["platform"]["simple_interrupt_generator"]["supported"] = false;
  return config.to_string();
}

std::uint32_t addi(unsigned rd, unsigned rs, int immediate) {
  return (std::uint32_t(immediate) & 0xfff) << 20 | rs << 15 | rd << 7 | 0x13;
}
std::uint32_t load(unsigned rd, unsigned rs, unsigned offset, unsigned width) {
  return offset << 20 | rs << 15 | width << 12 | rd << 7 | 0x03;
}
std::uint32_t store(unsigned rs, unsigned base, unsigned offset, unsigned width) {
  return (offset & 0xfe0) << 20 | rs << 20 | base << 15 | width << 12 | (offset & 31) << 7 | 0x23;
}
std::uint32_t csrw(unsigned csr, unsigned rs) { return csr << 20 | rs << 15 | 0x1073; }
void program(SailReference& model, std::uint64_t address, std::initializer_list<std::uint32_t> words) {
  std::vector<std::uint8_t> bytes;
  for (auto word : words)
    for (unsigned i = 0; i != 4; ++i) bytes.push_back(word >> (8 * i));
  model.load(address, bytes);
}
bool written(const StepResult& step, unsigned reg, std::uint64_t value) {
  for (const auto& write : step.writes) {
    if (write.bank != RegisterWrite::Bank::Integer || write.index != reg) continue;
    std::uint64_t actual = 0;
    for (unsigned i = 0; i != write.value.size(); ++i) actual |= std::uint64_t(write.value[i]) << (8 * i);
    if (actual == value) return true;
  }
  return false;
}
void check_scalar(unsigned xlen) {
  SailReference model(configuration(xlen), ram, backing);
  program(model, ram, {
    0x00000517,                 // auipc a0, 0
    addi(5, 10, 256), csrw(0x305, 5),
    addi(1, 0, 7), addi(2, 1, 5), store(2, 10, 128, 2), load(3, 10, 128, 2),
    0x00218463,                 // beq x3, x2, +8
    addi(4, 0, 99), addi(4, 0, 42), 0x00000073 // ecall
  });
  program(model, ram + 256, {addi(6, 0, 55)});
  for (unsigned offset : {0, 4, 8, 12, 16, 20, 24, 28, 36}) {
    auto step = model.step();
    CHECK(step.pc == ram + offset && step.retired && !step.trap && !step.waiting);
    CHECK(step.instruction_bytes == 4 && step.instruction.has_value());
    if (offset == 0) {
      CHECK(!step.memory.empty());
      CHECK(std::all_of(step.memory.begin(), step.memory.end(), [](const auto& access) { return access.instruction && !access.write; }));
    }
    if (offset == 16) CHECK(written(step, 2, 12));
    if (offset == 20) {
      CHECK(std::any_of(step.memory.begin(), step.memory.end(), [](const auto& access) {
        return access.write && !access.device && access.address == ram + 128 && access.value == std::vector<std::uint8_t>({12, 0, 0, 0});
      }));
    }
    if (offset == 24) CHECK(written(step, 3, 12));
  }
  CHECK(model.integer_register(0) == 0 && model.integer_register(4) == 42);
  CHECK(model.read_memory(ram + 128, 4) == std::vector<std::uint8_t>({12, 0, 0, 0}));
  auto trap = model.step();
  CHECK(trap.pc == ram + 40 && trap.trap && !trap.trap->interrupt && trap.trap->cause == 11);
  CHECK(!trap.retired && trap.next_pc == ram + 256 && !written(trap, 6, 55));
  CHECK(model.step().retired && model.integer_register(6) == 55);
  rejects<std::logic_error>([&] { SailReference other(configuration(xlen), ram, backing); });
}

void check_mmio(unsigned xlen) {
  SailReference model(configuration(xlen), ram, backing);
  program(model, ram, {0x02000537, load(1, 10, 0, 0), store(1, 10, 1, 0)});
  CHECK(model.step().retired);
  StepInputs inputs;
  inputs.device_reads = {{0x02000000, {0x80}}};
  auto read = model.step(inputs);
  CHECK(read.retired && written(read, 1, xlen == 32 ? 0xffffff80 : 0xffffffffffffff80));
  CHECK(std::count_if(read.memory.begin(), read.memory.end(), [](const auto& access) {
    return access.device && !access.write && access.address == 0x02000000 && access.value == std::vector<std::uint8_t>({0x80});
  }) == 1);
  auto write = model.step();
  CHECK(write.retired && std::any_of(write.memory.begin(), write.memory.end(), [](const auto& access) {
    return access.device && access.write && access.address == 0x02000001 && access.value == std::vector<std::uint8_t>({0x80});
  }));
  rejects<std::invalid_argument>([&] { model.read_memory(0x02000000, 1); });
}

void check_faults() {
  SailReference model(configuration(64), ram, backing);
  program(model, ram, {0x00000517, addi(5, 10, 256), csrw(0x305, 5),
                       0x000010b7, store(0, 1, 0, 2)}); // store to read-only ROM
  program(model, ram + 256, {0x00000000}); // illegal compressed instruction
  model.load(0x1000, std::array<std::uint8_t, 4>{1, 2, 3, 4});
  for (unsigned i = 0; i != 4; ++i) CHECK(model.step().retired);
  auto fault = model.step();
  CHECK(fault.trap && !fault.trap->interrupt && fault.trap->cause == 7 && !fault.retired);
  CHECK(std::none_of(fault.memory.begin(), fault.memory.end(), [](const auto& a) { return a.write; }));
  CHECK(model.read_memory(0x1000, 4) == std::vector<std::uint8_t>({1, 2, 3, 4}));
  auto illegal = model.step();
  CHECK(illegal.trap && illegal.trap->cause == 2 && !illegal.retired && illegal.next_pc == ram + 256);
}

void check_interrupt() {
  SailReference model(configuration(64), ram, backing);
  program(model, ram, {0x00000517, addi(5, 10, 256), csrw(0x305, 5),
                       addi(1, 0, -2048), csrw(0x304, 1), 0x30046073, addi(7, 0, 33)}); // csrsi mstatus,8
  program(model, ram + 256, {addi(8, 0, 77)});
  StepInputs pending;
  pending.machine_external_interrupt = true;
  // Pending before enables is masked, including the enabling instruction itself.
  for (unsigned i = 0; i != 6; ++i) CHECK(model.step(pending).retired);
  auto interrupt = model.step(pending);
  CHECK(interrupt.trap && interrupt.trap->interrupt && interrupt.trap->cause == 11);
  CHECK(!interrupt.retired && !interrupt.instruction && interrupt.pc == ram + 24 && interrupt.next_pc == ram + 256);
  CHECK(interrupt.memory.empty() && !written(interrupt, 8, 77));
  CHECK(model.step().retired && model.integer_register(8) == 77);
}

void check_wait_and_reset() {
  SailReference model(configuration(64), ram, backing);
  CHECK(model.pc() == ram && model.read_memory(ram + 128, 4) == std::vector<std::uint8_t>(4));
  program(model, ram, {0x10500073, addi(1, 0, 42)}); // WFI
  CHECK(model.step().waiting);
  CHECK(model.step().waiting && model.pc() == ram);
  StepInputs wake;
  wake.wake_wait = true;
  wake.time = 1;
  auto completed = model.step(wake);
  CHECK(!completed.waiting && completed.retired && completed.next_pc == ram + 4);
  CHECK(model.step().retired && model.integer_register(1) == 42);
}

void check_replay_failures() {
  for (unsigned kind = 0; kind != 4; ++kind) {
    SailReference model(configuration(64), ram, backing);
    program(model, ram, {0x02000537, load(1, 10, 0, 0)});
    CHECK(model.step().retired);
    StepInputs replay;
    if (kind == 1) replay.device_reads = {{0x02000001, {0x80}}};
    if (kind == 2) replay.device_reads = {{0x02000000, {0x80, 0}}};
    if (kind == 3) replay.device_reads = {{0x02000000, {0x80}}, {0x02000000, {0x81}}};
    rejects<std::runtime_error>([&] { model.step(replay); });
    rejects<std::logic_error>([&] { model.step(); });
  }
}

void check_time(unsigned xlen) {
  SailReference model(configuration(xlen), ram, backing);
  program(model, ram, {0xc01020f3, 0xc0102173, csrw(0x320, 0), csrw(0xb00, 0),
                       0xc00021f3, 0xc0002273}); // time x1/x2; reset counters; cycle x3/x4
  StepInputs time;
  time.time = 0x123456789;
  CHECK(written(model.step(time), 1, xlen == 32 ? 0x23456789 : 0x123456789));
  time.time += 99;
  CHECK(written(model.step(time), 2, xlen == 32 ? 0x234567ec : 0x1234567ec));
  CHECK(model.step().retired && model.step().retired);
  time.clock_ticks = 7;
  CHECK(written(model.step(time), 3, 7));
  CHECK(written(model.step(), 4, 7));
}

void check_fesvr_coexistence() {
  // Construct the actual existing transport while Sail is live. No second host
  // service or loader belongs to the reference model, and no target is launched.
  SailReference model(configuration(64), ram, backing);
  char name[] = "sail-reference-test";
  char payload[] = "none";
  char* argv[] = {name, payload};
  rhodium::fesvr::DirectMemoryHtif transport(2, argv, 64, 0x3000);
  CHECK(!transport.request_valid());
  program(model, ram, {addi(1, 0, 123)});
  CHECK(model.step().retired && model.integer_register(1) == 123);
}
}  // namespace

int main() {
  try {
    for (unsigned xlen : {32, 64}) { check_scalar(xlen); check_mmio(xlen); check_time(xlen); }
    check_faults();
    check_interrupt();
    check_wait_and_reset();
    check_replay_failures();
    check_fesvr_coexistence();
    std::cout << "Sail embedding: RV32/RV64, traps, interrupts, MMIO, lifecycle, FESVR passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
