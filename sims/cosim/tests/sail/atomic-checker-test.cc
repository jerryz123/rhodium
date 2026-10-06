// SPDX-License-Identifier: Apache-2.0
#include "../../sail/checker.h"
#include "config_utils.h"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace rhodium::cosim;
namespace o = rhodium::cosim::observation;
namespace {
constexpr std::uint64_t base = 0x80000000, data = base + 1024;
std::vector<std::uint8_t> bytes(std::uint64_t value, unsigned size) {
  std::vector<std::uint8_t> result(size);
  for (unsigned i = 0; i != size; ++i) result[i] = value >> (8*i);
  return result;
}
std::string configuration() {
  auto config = jsoncons::json::parse(get_default_config());
  config["platform"]["clint"]["supported"] = false;
  config["platform"]["simple_interrupt_generator"]["supported"] = false;
  config["platform"]["reservation"]["require_exact_reservation_addr"] = true;
  config["platform"]["reservation"]["invalidate_on_same_hart_store"] = true;
  for (const auto* kind : {"amo","lrsc"})
    config["memory"]["misaligned"]["exceptions"][kind] = jsoncons::json::parse(R"({"Some":"AlignmentException"})");
  return config.to_string();
}
unsigned atomic(unsigned op, unsigned size, unsigned rd = 12, unsigned flags = 0, unsigned rs = 10) {
  return op << 27 | flags << 25 | (op == 2 ? 0 : 11) << 20 | rs << 15 |
         (size == 4 ? 2 : 3) << 12 | rd << 7 | 0x2f;
}
o::RegisterWrite reg(unsigned index, std::uint64_t value) {
  return {o::Bank::Integer, index, 0, UINT64_MAX, value};
}
o::MemoryEffect memory(o::AccessKind kind, unsigned size, std::uint64_t old, std::uint64_t next,
                       o::AccessResult result = o::AccessResult::Success) {
  const bool success = result == o::AccessResult::Success;
  return {0,0,kind,data,false,0,(1ULL << size)-1,
          success && (kind == o::AccessKind::Amo || kind == o::AccessKind::Lr || kind == o::AccessKind::Load),
          success && (kind == o::AccessKind::Amo || kind == o::AccessKind::Sc || kind == o::AccessKind::Store),
          old,next,result};
}
std::uint64_t extended(std::uint64_t value, unsigned size) {
  return size == 4 ? std::uint64_t(std::int64_t(std::int32_t(value))) : value;
}
struct Fixture {
  SailChecker checker;
  unsigned order = 0;
  Fixture(std::uint64_t initial, std::uint64_t operand, const std::string& config = configuration())
      : checker(config,base,{{base,4096}}) {
    checker.load(data, bytes(initial,8));
    checker.load(data+16, bytes(operand,8));
    emit(0x00000517, {{ {0,0}, reg(10,base) }});
    emit(0x40050513, {{ {0,0}, reg(10,data) }});
    auto read = memory(o::AccessKind::Load,8,operand,0); read.virtual_address += 16;
    emit(0x01053583, {{{0,0},reg(11,operand)},{{2,0},read}});
  }
  void emit(unsigned encoding, std::map<o::EffectId,o::Effect> effects, std::optional<o::Trap> trap = {}) {
    checker.load(base+4*order,bytes(encoding,4));
    o::Record r{0,0,order,{0,order},o::Instruction{base+4*order,encoding,4,4,{3,false},0},
                o::Retirement{base+4*(order+1),{3,false}}, {},std::move(effects)};
    if (trap) r.outcome = *trap;
    checker.check(r); ++order;
  }
  void read(std::uint64_t value) {
    emit(0x00053683,{{{0,0},reg(13,value)},{{2,0},memory(o::AccessKind::Load,8,value,0)}});
  }
};
void amo(unsigned op, unsigned size, unsigned flags, bool zero_rd, unsigned mutation = 0) {
  const std::uint64_t initial = 0x8000000080000001, operand = 0x1234567800000003;
  const auto mask = size == 4 ? UINT64_C(0xffffffff) : UINT64_MAX;
  const auto left = initial & mask, right = operand & mask;
  std::uint64_t next = 0;
  switch (op) {
    case 0: next = left + right; break;
    case 1: next = right; break;
    case 4: next = left ^ right; break;
    case 8: next = left | right; break;
    case 12: next = left & right; break;
    case 16: next = left; break; // Negative left is signed minimum.
    case 20: next = right; break;
    case 24: next = right; break; // Positive right is unsigned minimum.
    case 28: next = left; break;
    default: throw std::runtime_error("bad test op");
  }
  next &= mask;
  Fixture f(initial,operand);
  auto effect = memory(o::AccessKind::Amo,size,initial,next);
  auto destination = reg(12,extended(initial,size));
  if (mutation == 2) effect.write_data ^= 1;
  if (mutation == 3) effect.virtual_address += 8;
  if (mutation == 4) effect.byte_mask = 3;
  if (mutation == 5) effect.kind = o::AccessKind::Load;
  if (mutation == 6) effect.write_valid = false;
  if (mutation == 7) destination.value ^= 1;
  if (mutation == 8) destination.index = 13;
  std::map<o::EffectId,o::Effect> effects{{{2,0},effect}};
  if (!zero_rd) effects[{0,0}] = destination;
  if (mutation == 9) effects.erase({2,0});
  f.emit(atomic(op,size,zero_rd ? 0 : 12,flags),effects);
  f.read((initial & ~mask) | next);
}
void lrsc(unsigned size, unsigned scenario, unsigned mutation = 0) {
  // 0 success; 1 legal spurious failure; 2 no LR; 3 second SC;
  // 4 external overlapping store; 5 different SC address; 6 same-hart store;
  // 7 nonoverlapping external store; 8 most-recent LR moved to another address.
  const std::uint64_t initial = 0x8000000080000001, operand = 3;
  Fixture f(initial,operand);
  auto old = initial;
  if (scenario != 2)
    f.emit(atomic(2,size),{{{0,0},reg(12,extended(old,size))},{{2,0},memory(o::AccessKind::Lr,size,old,0)}});
  if (scenario == 3) {
    f.emit(atomic(3,size),{{{0,0},reg(12,0)},{{2,0},memory(o::AccessKind::Sc,size,0,operand)}});
    old = size == 4 ? (old & UINT64_C(0xffffffff00000000)) | operand : operand;
  }
  if (scenario == 4 || scenario == 7) {
    const auto addr = scenario == 4 ? data : data+32;
    f.checker.host_write(f.order,addr,bytes(9,8));
    if (scenario == 4) old = 9;
  }
  if (scenario == 6) {
    f.emit(0x00b53023,{{{2,0},memory(o::AccessKind::Store,8,0,operand)}});
    old = operand;
  }
  if (scenario == 5 || scenario == 8) {
    f.emit(0x02050713,{{{0,0},reg(14,data+32)}});
    if (scenario == 8) {
      auto lr = memory(o::AccessKind::Lr,size,0,0); lr.virtual_address += 32;
      f.emit(atomic(2,size,12,0,14),{{{0,0},reg(12,0)},{{2,0},lr}});
    }
  }
  const bool failed = scenario != 0 && scenario != 7;
  auto effect = memory(o::AccessKind::Sc,size,0,operand,failed ? o::AccessResult::ScFailure : o::AccessResult::Success);
  if (scenario == 5) effect.virtual_address += 32;
  auto destination = reg(12,failed ? 1 : 0);
  if (mutation == 1) { effect.result = o::AccessResult::Success; effect.write_valid = true; destination.value = 0; }
  if (mutation == 2) destination.value ^= 1;
  if (mutation == 3) effect.virtual_address += 8;
  if (mutation == 4) effect.byte_mask = size == 4 ? 255 : 15;
  if (mutation == 5) effect.write_valid = true;
  if (mutation == 6) effect.kind = o::AccessKind::Store;
  if (mutation == 7) effect.write_data = 4;
  std::map<o::EffectId,o::Effect> effects{{{0,0},destination},{{2,0},effect}};
  if (mutation == 8) effects.erase({2,0});
  f.emit(atomic(3,size,12,3,scenario == 5 ? 14 : 10),effects);
  if (!failed) old = size == 4 ? (old & UINT64_C(0xffffffff00000000)) | operand : operand;
  f.read(old);
}
void fault(unsigned op, bool observed, unsigned mutation = 0) {
  Fixture f(7,3);
  f.emit(0x00150513,{{{0,0},reg(10,data+1)}});
  o::Trap trap{op == 2 ? 4ULL : 6ULL,base+4*f.order,data+1,0,{3,false},false,0,0};
  if (mutation == 1) trap.cause ^= 1;
  if (mutation == 2) trap.tval -= 1;
  auto effect = memory(op == 2 ? o::AccessKind::Lr : op == 3 ? o::AccessKind::Sc : o::AccessKind::Amo,
                       8,0,0,o::AccessResult::Fault);
  effect.virtual_address += 1;
  if (mutation == 3) effect.write_valid = true;
  std::map<o::EffectId,o::Effect> effects;
  if (observed) effects[{2,0}] = effect;
  f.emit(atomic(op,8),effects,trap);
}
void zero_destination(bool failed) {
  Fixture f(7,3);
  if (!failed) f.emit(atomic(2,8,0),{{{2,0},memory(o::AccessKind::Lr,8,7,0)}});
  f.emit(atomic(3,8,0),{{{2,0},memory(o::AccessKind::Sc,8,0,3,
                                    failed ? o::AccessResult::ScFailure : o::AccessResult::Success)}});
  f.read(failed ? 7 : 3);
}
void pointer_masked(unsigned size, unsigned pmlen, unsigned mutation = 0) {
  auto config = jsoncons::json::parse(configuration());
  config["memory"]["pmp"]["count"] = 0;
  config["memory"]["pmp"]["usable_count"] = 0;
  Fixture f(7,3,config.to_string());
  const auto mode = pmlen == 7 ? 2U : 3U;
  f.emit(mode << 20 | 0x93,{{{0,0},reg(1,mode)}});
  f.emit(0x02009093,{{{0,0},reg(1,std::uint64_t(mode)<<32)}}); // slli x1,x1,32.
  f.emit(0x10a09073,{}); // senvcfg.PMM controls effective U-mode accesses.
  f.emit(0x00100093,{{{0,0},reg(1,1)}});
  f.emit(0x01109093,{{{0,0},reg(1,1U<<17)}});
  f.emit(0x30009073,{}); // MPRV=1, MPP=U; execution remains in M-mode.
  f.emit(0xfff00093,{{{0,0},reg(1,UINT64_MAX)}});
  const auto tag = UINT64_MAX << (64-pmlen);
  f.emit((64-pmlen) << 20 | 0x9093,{{{0,0},reg(1,tag)}});
  f.emit(0x00156533,{{{0,0},reg(10,tag|data)}}); // or x10,x10,x1.
  auto lr = memory(o::AccessKind::Lr,size,7,0);
  if (mutation == 1) lr.virtual_address |= tag;
  if (mutation == 2) lr.virtual_address += 8;
  f.emit(atomic(2,size,12,3),{{{0,0},reg(12,7)},{{2,0},lr}});
  f.emit(atomic(3,size,12,3),{{{0,0},reg(12,0)},{{2,0},memory(o::AccessKind::Sc,size,0,3)}});
  f.emit(atomic(3,size,12,3),{{{0,0},reg(12,1)},{{2,0},memory(o::AccessKind::Sc,size,0,3,o::AccessResult::ScFailure)}});
  f.emit(atomic(0,size,12,3),{{{0,0},reg(12,3)},{{2,0},memory(o::AccessKind::Amo,size,3,6)}});
  // A fault changes privilege state; the attempted address must retain the
  // pre-instruction PMM/MPRV policy rather than being recomputed after the trap.
  f.emit(0x00150513,{{{0,0},reg(10,tag|(data+1))}});
  auto fault = memory(o::AccessKind::Lr,size,0,0,o::AccessResult::Fault);
  fault.virtual_address++;
  f.emit(atomic(2,size),{{{2,0},fault}},o::Trap{4,base+4*f.order,data+1,0,{3,false},false,0,0});
}
template<class F> void rejects(F action) {
  try { action(); } catch (const std::runtime_error& error) {
    if (std::string(error.what()).find("cosim mismatch:") == 0) return;
    throw;
  }
  throw std::runtime_error("atomic mutation was not detected");
}
}
int main() {
  for (unsigned size : {4,8}) {
    for (unsigned op : {0,1,4,8,12,16,20,24,28})
      for (unsigned flags = 0; flags != 4; ++flags)
        for (bool zero_rd : {false,true}) amo(op,size,flags,zero_rd);
    for (unsigned scenario = 0; scenario != 9; ++scenario) lrsc(size,scenario);
  }
  for (unsigned mutation = 2; mutation != 10; ++mutation)
    rejects([&] { amo(0,8,0,false,mutation); });
  for (unsigned scenario : {2,3,4,5,6,8}) rejects([&] { lrsc(8,scenario,1); });
  for (unsigned mutation : {2,3,4,5,6,8}) rejects([&] { lrsc(8,1,mutation); });
  rejects([] { lrsc(8,0,7); });
  for (unsigned op : {0,2,3}) for (bool observed : {false,true}) fault(op,observed);
  for (unsigned mutation : {1,2,3}) rejects([&] { fault(0,true,mutation); });
  zero_destination(false); zero_destination(true);
  for (unsigned size : {4,8}) for (unsigned pmlen : {7,16}) {
    pointer_masked(size,pmlen);
    for (unsigned mutation : {1,2}) rejects([&] { pointer_masked(size,pmlen,mutation); });
  }
  std::cout << "Atomic checker: AMO/LR/SC outcomes, stores, traps, and corruptions passed\n";
}
