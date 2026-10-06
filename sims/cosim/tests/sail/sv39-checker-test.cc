// SPDX-License-Identifier: Apache-2.0
#include "../../sail/checker.h"
#include "config_utils.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace rhodium::cosim;
namespace o = rhodium::cosim::observation;
namespace {
constexpr std::uint64_t base = 0x80000000, va = 0x40000000, code = va + 4096;
constexpr std::uint64_t root = base+0x1000, middle = base+0x2000, leaf = base+0x3000;
constexpr std::uint64_t data = base+0x4000, text = base+0x5000, other = base+0x6000;
std::vector<std::uint8_t> bytes(std::uint64_t value, unsigned size) {
  std::vector<std::uint8_t> result(size);
  for (unsigned i = 0; i != size; ++i) result[i] = value >> (8*i);
  return result;
}
std::string configuration(bool deny_data_write = false) {
  auto config = jsoncons::json::parse(get_default_config());
  config["platform"]["clint"]["supported"] = false;
  config["platform"]["simple_interrupt_generator"]["supported"] = false;
  config["memory"]["pmp"]["count"] = deny_data_write ? 16 : 0;
  config["memory"]["pmp"]["usable_count"] = deny_data_write ? 16 : 0;
  config["extensions"]["Svade"]["supported"] = true;
  config["extensions"]["Svadu"]["supported"] = false;
  config["memory"]["misaligned"]["exceptions"]["load_store"] = jsoncons::json::parse(R"({"None":null})");
  return config.to_string();
}
o::RegisterWrite reg(unsigned index, std::uint64_t value) {
  return {o::Bank::Integer,index,0,UINT64_MAX,value};
}
o::MemoryEffect memory(o::AccessKind kind, std::uint64_t virtual_address,
                       std::uint64_t physical_address, std::uint64_t value) {
  const bool read = kind == o::AccessKind::Load || kind == o::AccessKind::Lr;
  return {0,0,kind,virtual_address,true,physical_address,255,read,!read,
          read ? value : 0,read ? 0 : value,o::AccessResult::Success};
}
struct Fixture {
  SailChecker checker;
  unsigned order = 0, constant = 0, privilege = 3;
  std::uint64_t pc = base;
  Fixture(unsigned data_flags = 0xc7, unsigned code_flags = 0x4b, std::uint64_t status = 0x800, bool deny_data_write = false)
      : checker(configuration(deny_data_write),base,{{base,65536}}) {
    checker.load(root+8,bytes((middle >> 2)|1,8));
    checker.load(root+16,bytes((base >> 2)|0xcf,8));
    checker.load(middle,bytes((leaf >> 2)|1,8));
    checker.load(leaf,bytes((data >> 2)|data_flags,8));
    checker.load(leaf+8,bytes((text >> 2)|code_flags,8));
    checker.load(data,bytes(42,8));
    checker.load(other,bytes(99,8));
    emit(0x00000f97,{{{0,0},reg(31,base)}});
    value(10,va); value(11,9);
    value(5,base+0x400); csr(0x305);
    value(5,code); csr(0x341);
    value(5,(UINT64_C(8)<<60)|(root>>12)); csr(0x180);
    if (deny_data_write) {
      value(5,data>>2); csr(0x3b0);
      value(5,(data+4096)>>2); csr(0x3b1);
      value(5,UINT64_MAX>>2); csr(0x3b2);
      value(5,0x0f090f); csr(0x3a0);
    }
    value(5,status); csr(0x300);
    emit(0x30200073,{}, {}, code, 1);
  }
  void emit(unsigned encoding, std::map<o::EffectId,o::Effect> effects = {},
            std::optional<o::Trap> trap = {}, std::uint64_t next = 0, unsigned mode = 99,
            bool fetch_fault = false) {
    const auto pa = pc >= code && pc < code+4096 ? text+pc-code : pc;
    checker.load(pa,bytes(encoding,4));
    o::Record r{0,0,order,{0,order},o::Instruction{pc,encoding,fetch_fault ? 0U : 4U,
                fetch_fault ? 0U : 4U,{privilege,false},0},
                o::Retirement{next ? next : pc+4,{mode == 99 ? privilege : mode,false}}, {},std::move(effects)};
    if (trap) r.outcome = *trap;
    checker.check(r); ++order;
    pc = next ? next : pc+4;
    if (mode != 99) privilege = mode;
  }
  void value(unsigned rd, std::uint64_t value) {
    const unsigned offset = 0x800-8*(++constant);
    checker.load(base+offset,bytes(value,8));
    emit(offset<<20 | 31<<15 | 3<<12 | rd<<7 | 3,
         {{{0,0},reg(rd,value)},{{2,0},memory(o::AccessKind::Load,base+offset,base+offset,value)}});
  }
  void csr(unsigned address) { emit(address<<20 | 5<<15 | 1<<12 | 0x73); }
  void read(std::uint64_t pa, std::uint64_t value, unsigned mutation = 0) {
    auto effect = memory(o::AccessKind::Load,va,pa,value);
    emit(0x00053603,{{{0,0},reg(12,value ^ (mutation == 4))},{{2,0},effect}});
  }
};
void mapped(unsigned mutation = 0) {
  Fixture f;
  f.read(data,42,mutation);
  auto store = memory(o::AccessKind::Store,va,data,9);
  if (mutation == 1) store.physical_address += 4096;
  if (mutation == 2) store.physical_valid = false;
  if (mutation == 3) store.write_data ^= 1;
  f.emit(0x00b53023,{{{2,0},store}});
  f.read(data,9);
  f.emit(0x1005362f,{{{0,0},reg(12,9)},{{2,0},memory(o::AccessKind::Lr,va,data,9)}});
  auto sc = memory(o::AccessKind::Sc,va,data,9);
  if (mutation == 5) sc.physical_address += 8;
  if (mutation == 6) sc.physical_valid = false;
  const bool failed_sc = mutation >= 7;
  if (failed_sc) { sc.result = o::AccessResult::ScFailure; sc.write_valid = false; }
  if (mutation == 7) sc.physical_valid = false;
  if (mutation == 8) sc.physical_address += 8;
  f.emit(0x18b5362f,{{{0,0},reg(12,failed_sc ? 1 : 0)},{{2,0},sc}});
  // A real supervisor PTE store and SFENCE change the private mapping.
  f.value(13,leaf); f.value(14,(other>>2)|0xc7);
  f.emit(0x00e6b023,{{{2,0},memory(o::AccessKind::Store,leaf,leaf,(other>>2)|0xc7)}});
  f.emit(0x12000073);
  f.read(other,99);
}

void translated_sc_fault(bool reservation, unsigned mutation = 0) {
  Fixture f(0xc7,0x4b,0x800,true);
  if (reservation)
    f.emit(0x1005362f,{{{0,0},reg(12,42)},{{2,0},memory(o::AccessKind::Lr,va,data,42)}});
  auto effect = memory(o::AccessKind::Sc,va,data,0);
  effect.result = o::AccessResult::Fault;
  effect.physical_valid = effect.read_valid = effect.write_valid = false;
  if (mutation == 1) effect.virtual_address += 8;
  if (mutation == 2) { effect.physical_valid = true; effect.physical_address += 8; }
  if (mutation == 3) effect.write_valid = true;
  auto trap = o::Trap{7,f.pc,va,base+0x400,{3,false},false,0,0};
  if (mutation == 4) trap.cause = 15;
  f.emit(0x18b5362f,{{{2,0},effect}},trap);
}

void external_mapping(bool mailbox, bool corrupt = false) {
  Fixture f;
  // Page walks and instruction fetch remain independent even when their
  // backing addresses also have an environmental data-read policy.
  for (auto address : {root + 8, middle, leaf, leaf + 8, text})
    f.checker.external_memory({address, 8});
  if (mailbox) {
    f.checker.external_memory({data, 8});
    f.checker.host_write(f.order + 1, data, bytes(99, 8));
  }
  f.read(data, mailbox ? 99 : 42, corrupt ? 4 : 0);
}

void external_page_fault(bool reported_success = false) {
  Fixture f(0xc9); // Execute-only page with MXR clear: a load must fault.
  f.checker.external_memory({data, 8});
  if (reported_success) { f.read(data, 99); return; }
  auto effect = memory(o::AccessKind::Load, va, 0, 0);
  effect.result = o::AccessResult::Fault;
  effect.physical_valid = effect.read_valid = effect.write_valid = false;
  o::Trap trap{13, f.pc, va, base + 0x400, {3, false}, false, 0, 0};
  f.emit(0x00053603, {{{2, 0}, effect}}, trap);
}
void page_fault(unsigned flags, bool store, unsigned mutation = 0) {
  Fixture f(flags);
  o::Trap trap{store ? 15ULL : 13ULL,f.pc,va,base+0x400,{3,false},false,0,0};
  auto effect = memory(store ? o::AccessKind::Store : o::AccessKind::Load,va,0,0);
  effect.result = o::AccessResult::Fault;
  effect.physical_valid = effect.read_valid = effect.write_valid = false;
  if (mutation == 1) trap.tval += 8;
  if (mutation == 2) { trap.cause = store ? 7 : 5; }
  f.emit(store ? 0x00b53023 : 0x00053603,{{{2,0},effect}},trap);
}
void fetch_fault() {
  Fixture f(0xc7,0x43);
  f.emit(0x00000013,{},o::Trap{12,f.pc,f.pc,base+0x400,{3,false},false,0,0},0,99,true);
}
void device(unsigned mutation = 0) {
  Fixture f;
  f.value(13,leaf); f.value(14,(UINT64_C(0x02000000)>>2)|0xc7);
  f.emit(0x00e6b023,{{{2,0},memory(o::AccessKind::Store,leaf,leaf,(UINT64_C(0x02000000)>>2)|0xc7)}});
  f.emit(0x12000073);
  auto write = memory(o::AccessKind::Store,va,0x02000000,9);
  write.byte_mask = 1;
  if (mutation == 2) write.physical_address += 1;
  if (mutation == 3) write.write_data ^= 1;
  std::map<o::EffectId,o::Effect> effects{{{2,0},write}};
  if (mutation == 4) effects[{2,1}] = write;
  f.emit(0x00b50023,effects);
  auto effect = memory(o::AccessKind::Load,va,0x02000000,0x80);
  effect.byte_mask = 1;
  if (mutation == 1) effect.physical_address += 8;
  f.emit(0x00054603,{{{0,0},reg(12,0x80)},{{2,0},effect}});
}
void split(unsigned offset, unsigned size, bool store, unsigned fault_flags = 0xc7, unsigned mutation = 0,
           bool byte_fragments = false) {
  Fixture f;
  const auto start = va+0x2000+offset;
  f.checker.load(leaf+16,bytes((data>>2)|0xc7,8));
  f.checker.load(leaf+24,bytes((other>>2)|fault_flags,8));
  f.checker.load(data,std::vector<std::uint8_t>(4096,0x5a));
  f.checker.load(other,std::vector<std::uint8_t>(4096,0xa5));
  f.value(10,start); f.value(11,UINT64_C(0x8877665544332211));
  const auto width = size == 2 ? 1U : size == 4 ? 2U : 3U;
  const auto encoding = store ? 11<<20 | 10<<15 | width<<12 | 0x23 :
                               10<<15 | (size == 8 ? 3 : width+4)<<12 | 12<<7 | 3;
  std::map<o::EffectId,o::Effect> effects;
  if (fault_flags != 0xc7) {
    const unsigned remaining = offset+size-4096;
    auto effect = memory(store ? o::AccessKind::Store : o::AccessKind::Load,va+0x3000,0,0);
    effect.result = o::AccessResult::Fault;
    effect.physical_valid = effect.read_valid = effect.write_valid = false;
    effect.fragment_offset = 4096-offset;
    effect.byte_mask = (1U<<remaining)-1;
    o::Trap trap{store ? 15ULL : 13ULL,f.pc,va+0x3000,base+0x400,{3,false},false,0,0};
    if (mutation == 1) trap.tval = start;
    auto prefix = memory(store ? o::AccessKind::Store : o::AccessKind::Load,start,data+offset,
                         store ? UINT64_C(0x8877665544332211) : UINT64_C(0x5a5a5a5a5a5a5a5a));
    prefix.byte_mask = (1U << (4096-offset))-1;
    effects[{2,0}] = prefix;
    effects[{2,1}] = effect;
    if (mutation == 3) effects.erase({2,0});
    f.emit(encoding,effects,trap,base+0x400,3);
    // The independently executed store prefix survives the second-page fault.
    const auto after = store ? UINT64_C(0x3322115a5a5a5a5a) : UINT64_C(0x5a5a5a5a5a5a5a5a);
    f.value(10,data+4088);
    f.emit(0x00053603,{{{0,0},reg(12,after)},
      {{2,0},memory(o::AccessKind::Load,data+4088,data+4088,after)}});
    return;
  }
  std::uint64_t value = 0;
  for (unsigned i = 0; i != size; ++i) value |= std::uint64_t(offset+i < 4096 ? 0x5a : 0xa5) << (8*i);
  unsigned n = 0;
  for (unsigned i = 0; i < size;) {
    const unsigned part = byte_fragments ? 1 : std::min(size-i,8-((offset+i)&7));
    auto effect = memory(store ? o::AccessKind::Store : o::AccessKind::Load,start+i,
                         offset+i < 4096 ? data+offset+i : other+offset+i-4096,
                         (store ? UINT64_C(0x8877665544332211) : value) >> (8*i));
    effect.fragment_offset = i;
    effect.byte_mask = (1U<<part)-1;
    effects[{2,n++}] = effect;
    i += part;
  }
  if (!store) effects[{0,0}] = reg(12,value);
  if (mutation == 1) std::get<o::MemoryEffect>(effects[{2,1}]).physical_address += 4096;
  if (mutation == 3) std::get<o::MemoryEffect>(effects[{2,1}]).byte_mask ^= 1;
  if (mutation == 4) std::get<o::RegisterWrite>(effects[{0,0}]).value ^= 1;
  if (mutation == 5) effects.erase({2,1});
  f.emit(encoding,effects);
  if (store) {
    for (auto& [id,effect] : effects) {
      (void)id;
      auto& mem = std::get<o::MemoryEffect>(effect);
      mem.kind = o::AccessKind::Load;
      mem.read_valid = true; mem.write_valid = false;
      mem.read_data = mem.write_data;
    }
    effects[{0,0}] = reg(12,UINT64_C(0x8877665544332211) & (UINT64_MAX >> (8*(8-size))));
    f.emit(10<<15 | (size == 8 ? 3 : width+4)<<12 | 12<<7 | 3,effects);
  }
}
void sv32(bool corrupt_fault = false) {
  auto config = jsoncons::json::parse(get_default_rv32_config());
  config["platform"]["clint"]["supported"] = false;
  config["platform"]["simple_interrupt_generator"]["supported"] = false;
  config["memory"]["pmp"]["count"] = 0;
  config["memory"]["pmp"]["usable_count"] = 0;
  config["extensions"]["Svade"]["supported"] = true;
  config["extensions"]["Svadu"]["supported"] = false;
  SailChecker checker(config.to_string(),base,{{base,65536}});
  // Two-level Sv32 mapping of one virtual page; its adjacent PTE stays invalid.
  checker.load(root+4*(va>>22),bytes((leaf>>2)|1,4));
  checker.load(leaf,bytes((data>>2)|0xc7,4));
  checker.load(data,bytes(0x87654321,4));
  unsigned order = 0;
  auto emit = [&](unsigned encoding, std::optional<std::pair<unsigned,std::uint64_t>> write = {},
                  std::optional<o::Trap> trap = {}, std::optional<o::MemoryEffect> access = {}) {
    const auto pc = base+4*order;
    checker.load(pc,bytes(encoding,4));
    o::Record r{0,0,order,{0,order},o::Instruction{pc,encoding,4,4,{3,false},0},
                o::Retirement{pc+4,{3,false}}, {},{}};
    if (write) r.effects[{0,0}] = o::RegisterWrite{o::Bank::Integer,write->first,0,UINT32_MAX,write->second};
    if (trap) r.outcome = *trap;
    if (access) r.effects[{2,0}] = *access;
    checker.check(r); ++order;
  };
  emit(0x800002b7,{{5,base}});                 // lui t0, 0x80000
  emit(0x40028293,{{5,base+0x400}});
  emit(0x30529073);                          // csrw mtvec, t0
  emit(0x800802b7,{{5,0x80080000}});
  emit(0x00128293,{{5,0x80080001}});
  emit(0x18029073);                          // csrw satp, t0
  emit(0x40000537,{{10,va}});
  emit(0x000212b7,{{5,0x21000}});
  emit(0x80028293,{{5,0x20800}});
  emit(0x30029073);                          // MPRV, MPP=S
  emit(0x00052603,{{12,0x87654321}});         // lw a2, 0(a0)
  auto store = memory(o::AccessKind::Store,va,data,0x87654321);
  store.byte_mask = 15;
  emit(0x00c52023,{}, {},store);
  emit(0x000015b7,{{11,4096}});
  emit(0x00b50533,{{10,va+4096}});
  emit(0x00052603,{},o::Trap{13,base+4*order,va+4096+(corrupt_fault ? 4 : 0),base+0x400,{3,false},false,0,0});
}
template<class F> void rejects(F action) {
  try { action(); } catch (const std::runtime_error& error) {
    if (std::string(error.what()).starts_with("cosim mismatch:")) return;
    throw;
  }
  throw std::runtime_error("Sv39 corruption was not detected");
}
}
int main() {
  external_page_fault();
  bool denied = false;
  try { external_page_fault(true); } catch (const std::runtime_error&) { denied = true; }
  if (!denied) throw std::runtime_error("external-memory replay bypassed page permissions");
  for (bool mailbox : {false, true}) {
    external_mapping(mailbox);
    bool failed = false;
    try { external_mapping(mailbox, true); } catch (const std::runtime_error&) { failed = true; }
    if (!failed) throw std::runtime_error("translated external-memory corruption was not rejected");
  }
  sv32();
  rejects([] { sv32(true); });
  mapped();
  mapped(9); // A translated, spuriously failed SC still carries its PA.
  for (unsigned mutation = 1; mutation <= 8; ++mutation) rejects([&] { mapped(mutation); });
  for (bool reservation : {false,true}) {
    translated_sc_fault(reservation);
    for (unsigned mutation = 1; mutation <= 4; ++mutation)
      rejects([&] { translated_sc_fault(reservation,mutation); });
  }
  page_fault(0,false); page_fault(7,false); page_fault(0x47,true);
  page_fault(0x43,true); page_fault(0xd7,false); page_fault(0x49,false);
  { Fixture f(0xd7,0x4b,0x40800); f.read(data,42); }
  { Fixture f(0x49,0x4b,0x80800); f.read(data,42); }
  fetch_fault();
  device();
  for (unsigned mutation : {1,2,3,4}) rejects([&] { device(mutation); });
  rejects([] { page_fault(0,false,1); });
  rejects([] { page_fault(0,false,2); });
  rejects([] { Fixture f(0x43); f.emit(0x00b53023,{{{2,0},memory(o::AccessKind::Store,va,data,9)}}); });
  for (unsigned size : {2,4,8}) for (unsigned offset : {1,7,4095}) for (bool store : {false,true})
    split(offset,size,store);
  split(4093,8,false,0xc7,0,true);
  for (unsigned flags : {0,7}) for (bool store : {false,true}) split(4093,8,store,flags);
  for (unsigned flags : {0x43,0x47}) split(4093,8,true,flags);
  for (unsigned mutation : {1,3,5}) rejects([&] { split(4093,8,true,0xc7,mutation); });
  rejects([] { split(4093,8,false,0xc7,4); });
  for (unsigned mutation : {1,3}) rejects([&] { split(4093,8,true,0,mutation); });
  std::cout << "Sv32/Sv39 checker: translated fetch/data/MMIO, LR/SC, remapping, traps and corruptions passed\n";
  std::cout << "Split checker: load results, physical stores, partial-access traps and retained prefixes passed\n";
}
