// Checks real-core architectural records, delayed ownership, and reset epochs.
// SPDX-License-Identifier: Apache-2.0
#include "../../../../../sims/cosim/events/dpi.h"
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
using namespace rhodium::cosim::observation;
namespace {
Collector collector;
std::unique_ptr<DpiBinding> binding;
struct Expected {
  Word pc, instruction, rd, value, cause, tval, next_pc;
  bool interrupt = false;
  std::optional<MemoryEffect> memory;
};
std::deque<Expected> expected;
std::map<Word, Word> input_pins;
Word order = 0, current_epoch = 0, mask = ~Word{0};
unsigned width = 64, published = 0;
void require(bool okay, const char* message) {
  if (!okay) { std::fprintf(stderr, "core cosim: %s (epoch %llu order %llu)\n", message,
    static_cast<unsigned long long>(current_epoch), static_cast<unsigned long long>(order)); std::abort(); }
}
}
extern "C" void core_cosim_reset(std::int64_t epoch, int xlen) {
  width = xlen; mask = width == 32 ? 0xffffffffULL : ~Word{0};
  current_epoch = epoch; order = 0; expected.clear();
  collector.reset(0, epoch, {0, 0, {3, false}, width, 0});
  if (!binding) binding = std::make_unique<DpiBinding>(collector);
}
extern "C" void core_cosim_expect(std::int64_t pc, int instruction, int rd, std::int64_t value,
                                  int cause, std::int64_t tval, std::int64_t next_pc, int interrupt) {
  expected.push_back({Word(pc), Word(std::uint32_t(instruction)), Word(rd), Word(value) & mask,
                      Word(cause), Word(tval), Word(next_pc), bool(interrupt), {}});
}
extern "C" void core_cosim_memory(int kind, std::int64_t address, int bytes, int read, int write,
                                  std::int64_t read_data, std::int64_t write_data, int result) {
  require(!expected.empty(), "memory expectation needs instruction");
  expected.back().memory = MemoryEffect{0, 0, AccessKind(kind), Word(address), false, 0, Word(bytes),
    bool(read), bool(write), Word(read_data) & mask, Word(write_data) & mask, AccessResult(result)};
}
extern "C" void core_cosim_memory_offset(int offset) {
  require(!expected.empty() && expected.back().memory.has_value(), "fragment offset needs memory expectation");
  expected.back().memory->fragment_offset = offset;
}
extern "C" void core_cosim_begin(std::int64_t sample, int interrupt) {
  input_pins[Word(sample)] = interrupt ? 8ULL : 0ULL;
  collector.begin_sample(sample);
}
extern "C" void core_cosim_end() {
  try {
    binding->check();
    for (const auto& record : collector.end_sample()) {
      require(!expected.empty(), "unexpected record");
      const auto e = expected.front(); expected.pop_front();
      require(record.environment.interrupt_inputs == input_pins.at(record.sample), "sampled interrupt pins lost");
      require(!e.interrupt || record.environment.interrupt_boundary, "interrupt outside sampled boundary");
      require(record.id.epoch == current_epoch && record.id.order == order, "wrong owner/order");
      require(std::holds_alternative<Interrupt>(record.event) == e.interrupt, "wrong event kind");
      if (!e.interrupt) {
        const auto& insn = std::get<Instruction>(record.event);
        require(insn.pc == e.pc && insn.encoding == e.instruction, "wrong instruction");
        require(insn.privilege.mode == 3 && !insn.privilege.virtualized, "wrong instruction context");
        require(insn.instruction_bytes == ((e.instruction & 3) == 3 ? 4 : 2), "wrong instruction length");
        require(record.environment.time == record.sample, "header environment lost");
      }
      if (e.cause == ~Word{0}) {
        require(std::holds_alternative<Retirement>(record.outcome), "unexpected trap");
        require(std::get<Retirement>(record.outcome).next_pc == e.next_pc, "wrong next PC");
        require(std::get<Retirement>(record.outcome).privilege.mode == 3, "wrong retirement privilege");
      } else {
        require(std::holds_alternative<Trap>(record.outcome), "missing trap");
        const auto& trap = std::get<Trap>(record.outcome);
        require(trap.cause == e.cause && trap.epc == e.pc && trap.tval == e.tval, "wrong trap provenance");
        require(trap.target_pc == e.next_pc, "wrong trap target");
        require(trap.privilege.mode == 3 && !trap.privilege.virtualized && !trap.guest_valid, "wrong trap context");
      }
      unsigned writes = 0, memories = 0;
      for (const auto& [id, effect] : record.effects) {
        if (const auto* w = std::get_if<RegisterWrite>(&effect)) {
          ++writes;
          require(w->bank == Bank::Integer && w->index == e.rd && w->mask == mask &&
                  w->value == e.value && w->bit_offset == 0, "wrong GPR write");
        } else {
          ++memories;
          require(e.memory.has_value(), "unexpected memory effect");
          const auto& m = std::get<MemoryEffect>(effect);
          const auto& wanted = *e.memory;
          const Word offset = wanted.kind == AccessKind::CacheOperation ? (memories-1)*8 : wanted.fragment_offset;
          require(m.kind == wanted.kind && m.virtual_address == wanted.virtual_address + (wanted.kind == AccessKind::CacheOperation ? offset : 0) &&
                  m.byte_mask == wanted.byte_mask && !m.physical_valid && m.fragment_offset == offset,
                  "wrong logical memory access");
          require(m.read_valid == wanted.read_valid && m.write_valid == wanted.write_valid &&
                  m.result == wanted.result, "wrong memory outcome");
          Word data_mask = 0;
          for (unsigned b = 0; b < 8; ++b) if ((m.byte_mask >> b) & 1) data_mask |= Word{255} << (8*b);
          require(!m.read_valid || ((m.read_data ^ wanted.read_data) & data_mask) == 0, "wrong memory read");
          require(!m.write_valid || ((m.write_data ^ wanted.write_data) & data_mask) == 0, "wrong memory write");
        }
      }
      require(writes == (e.rd == 0 ? 0 : 1), "missing/duplicate GPR effect");
      const unsigned fragments = !e.memory ? 0 : (e.memory->kind == AccessKind::CacheOperation && e.memory->result == AccessResult::Success ? 8 : 1);
      require(memories == fragments, "missing/duplicate memory effect");
      ++order; ++published;
    }
  } catch (const std::exception& error) {
    std::fprintf(stderr, "core cosim at order %llu: %s\n", static_cast<unsigned long long>(order), error.what()); std::abort();
  }
}
extern "C" void core_cosim_pending(int count) { require(expected.size() == unsigned(count), "pending count"); }
extern "C" void core_cosim_finish() {
  require(expected.empty(), "unpublished expectations");
  collector.finish();
  std::printf("RV%u core cosim: %u ordered records, passivity checked every cycle\n", width, published);
}
