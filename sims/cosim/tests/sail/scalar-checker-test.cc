// SPDX-License-Identifier: Apache-2.0
#include "../../sail/checker.h"
#include "config_utils.h"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace rhodium::cosim;
namespace o = rhodium::cosim::observation;
constexpr std::uint64_t base = 0x80000000;

std::string configuration(unsigned xlen = 64) {
  auto config = jsoncons::json::parse(xlen == 32 ? get_default_rv32_config() : get_default_config());
  config["platform"]["clint"]["supported"] = false;
  config["platform"]["simple_interrupt_generator"]["supported"] = false;
  return config.to_string();
}
o::Record record(unsigned order, unsigned encoding) {
  return {0, 0, order, {0, order},
          o::Instruction{base + order * 4, encoding, 4, 4, {3, false}, 0},
          o::Retirement{base + (order + 1) * 4, {3, false}}, {}, {}};
}
void run(int mutation, unsigned xlen = 64) {
  const std::uint64_t mask = xlen == 32 ? UINT32_MAX : UINT64_MAX;
  const unsigned byte_mask = xlen == 32 ? 15 : 255;
  SailChecker checker(configuration(xlen), base, {{base, 4096}});
  const std::array<unsigned, 5> words{0x00000117, 0x00700093, xlen == 32 ? 0x02112023U : 0x02113023U, xlen == 32 ? 0x02012183U : 0x02013183U, 0x00000073};
  std::vector<std::uint8_t> code;
  for (auto word : words) for (unsigned i = 0; i != 4; ++i) code.push_back(word >> (8 * i));
  checker.load(base, code);
  auto r = record(0, words[0]);
  r.effects[{0, 0}] = o::RegisterWrite{o::Bank::Integer, 2, 0, mask, base};
  r.effects[{1, 0}] = o::CsrUpdate{0xb02, o::CsrOperation::AssignMasked, mask, mutation == 9 ? 2ULL : 1ULL};
  if (mutation == 1) std::get<o::RegisterWrite>(r.effects.at({0, 0})).value ^= 1;
  if (mutation == 2) std::get<o::Retirement>(r.outcome).next_pc += 4;
  if (mutation == 3) r.effects.clear();
  checker.check(r);
  r = record(1, words[1]);
  r.effects[{0, 0}] = o::RegisterWrite{o::Bank::Integer, 1, 0, mask, 7};
  checker.check(r);
  r = record(2, words[2]);
  r.effects[{2, 0}] = o::MemoryEffect{0, 0, o::AccessKind::Store, base + 32, false, 0, byte_mask, false, true, 0, 7, o::AccessResult::Success};
  if (mutation == 4) std::get<o::MemoryEffect>(r.effects.at({2, 0})).virtual_address += 8;
  if (mutation == 5) std::get<o::MemoryEffect>(r.effects.at({2, 0})).write_data = 8;
  if (mutation == 7) std::get<o::MemoryEffect>(r.effects.at({2, 0})).result = o::AccessResult::Fault;
  if (mutation == 8) std::get<o::MemoryEffect>(r.effects.at({2, 0})).physical_valid = true;
  checker.check(r);
  // A completed external write belongs before the later load, not before the older store.
  const std::array<std::uint8_t, 8> nine{9, 0, 0, 0, 0, 0, 0, 0};
  checker.host_write(3, base + 32, nine);
  r = record(3, words[3]);
  r.effects[{0, 0}] = o::RegisterWrite{o::Bank::Integer, 3, 0, mask, 9};
  r.effects[{2, 0}] = o::MemoryEffect{0, 0, o::AccessKind::Load, base + 32, false, 0, byte_mask, true, false, 9, 0, o::AccessResult::Success};
  checker.check(r);
  r = record(4, words[4]);
  r.outcome = o::Trap{11, base + 16, 0, 0, {3, false}, false, 0, 0};
  if (mutation == 6) std::get<o::Trap>(r.outcome).tval = 1;
  checker.check(r);
  if (checker.checked() != 5) throw std::runtime_error("wrong checked count");
}
void cache_case(unsigned op, unsigned xlen, unsigned mutation = 0) {
  auto config = jsoncons::json::parse(configuration(xlen));
  config["extensions"]["Zicboz"]["supported"] = true;
  config["extensions"]["Zicbom"]["supported"] = true;
  SailChecker checker(config.to_string(), base, {{base, 4096}});
  const std::array<unsigned,3> code{0x00000517,0x43f50513,(op<<20)|0x5200f};
  std::vector<std::uint8_t> initial(4096,255);
  for (unsigned n=0;n<code.size();++n)
    for (unsigned b=0;b<4;++b) initial[4*n+b]=code[n]>>(8*b);
  checker.load(base,initial);
  for (unsigned n=0;n<code.size();++n) {
    auto r=record(n,code[n]);
    if (n<2) r.effects[{0,0}]=o::RegisterWrite{o::Bank::Integer,10,0,xlen==32 ? UINT32_MAX : UINT64_MAX,base+(n ? 0x43f : 0)};
    else for (unsigned i=0;i<(op==4 ? 8U : 1U);++i)
      r.effects[{2,i}]=o::MemoryEffect{0,8*i,op==4 ? o::AccessKind::Store : o::AccessKind::CacheOperation,
        base+0x400+8*i,true,base+0x400+8*i,255,false,op==4,0,0,o::AccessResult::Success};
    if (n==2) {
      auto& effect=std::get<o::MemoryEffect>(r.effects.at({2,0}));
      if (mutation==1) effect.write_data=1;
      if (mutation==2) effect.physical_address+=64;
      if (mutation==3) r.effects.erase({2,0});
      if (mutation==4) effect.write_valid=true;
    }
    checker.check(r);
  }
}
void interrupt_case(unsigned mutation, unsigned xlen = 64) {
  const std::uint64_t mask = xlen == 32 ? UINT32_MAX : UINT64_MAX;
  SailChecker checker(configuration(xlen), base, {{base, 4096}});
  const std::array<unsigned, 7> words{0x00000517, 0x10050293, 0x30529073,
                                    0xfff00093, 0x30409073, 0x30046073, 0x02100393};
  std::vector<std::uint8_t> code(260);
  for (unsigned n = 0; n != words.size(); ++n)
    for (unsigned i = 0; i != 4; ++i) code[4*n+i] = words[n] >> (8*i);
  const unsigned mret = 0x30200073;
  for (unsigned i = 0; i != 4; ++i) code[256+i] = mret >> (8*i);
  checker.load(base, code);
  for (unsigned n = 0; n != 6; ++n) {
    auto r = record(n, words[n]);
    r.environment = {0x880, 100+n, n, false};
    if (n == 0) r.effects[{0,0}] = o::RegisterWrite{o::Bank::Integer,10,0,mask,base};
    if (n == 1) r.effects[{0,0}] = o::RegisterWrite{o::Bank::Integer,5,0,mask,base+256};
    if (n == 3) r.effects[{0,0}] = o::RegisterWrite{o::Bank::Integer,1,0,mask,mask};
    if (n == 2) {
      r.effects[{1,0}] = o::CsrUpdate{0x305,o::CsrOperation::AssignMasked,mask,base+256};
      if (mutation == 5) std::get<o::CsrUpdate>(r.effects.at({1,0})).value = 0;
    }
    checker.check(r);
  }
  auto r = record(6, 0);
  o::Trap trap{11, base+24, 0, base+256, {3,false}, false,0,0};
  r.event = o::Interrupt{trap, 0}; r.outcome = trap;
  r.environment = {0x880, 200, 30, true};
  r.effects[{1,0}] = o::CsrUpdate{0x341,o::CsrOperation::AssignMasked,mask,base+24};
  r.effects[{1,1}] = o::CsrUpdate{0x342,o::CsrOperation::AssignMasked,mask,(UINT64_C(1) << (xlen-1)) | 11};
  if (mutation == 1) std::get<o::Trap>(r.outcome).cause = 7;
  if (mutation == 2) std::get<o::Trap>(r.outcome).target_pc += 4;
  if (mutation == 3) std::get<o::Trap>(r.outcome).tval = 1;
  if (mutation == 4) r.environment.interrupt_inputs = 0;
  if (mutation == 6) std::get<o::CsrUpdate>(r.effects.at({1,0})).value += 4;
  if (mutation == 7) r.environment.interrupt_boundary = false;
  checker.check(r);
  r = record(7,mret);
  std::get<o::Instruction>(r.event).pc = base+256;
  std::get<o::Retirement>(r.outcome).next_pc = base+24;
  r.environment = {0,201,31,false};
  checker.check(r);
}
void rv32_cycle_high() {
  SailChecker checker(configuration(32),base,{{base,4096}});
  const std::array<unsigned,4> code{0x32001073,0x00100293,0xb8029073,0xb0002573};
  for (unsigned n=0;n<code.size();++n) {
    std::array<std::uint8_t,4> bytes{};
    for (unsigned i=0;i<4;++i) bytes[i]=code[n]>>(8*i);
    checker.load(base+4*n,bytes);
    auto r=record(n,code[n]); r.environment.cycle=n;
    if(n==1) r.effects[{0,0}]=o::RegisterWrite{o::Bank::Integer,5,0,UINT32_MAX,1};
    if(n==2) r.effects[{1,0}]=o::CsrUpdate{0xb80,o::CsrOperation::AssignMasked,UINT32_MAX,1};
    if(n==3) r.effects[{0,0}]=o::RegisterWrite{o::Bank::Integer,10,0,UINT32_MAX,2};
    checker.check(r);
  }
}
// No CSR snapshots: execute state changes independently and observe only results.
void guest_case(bool user, bool guest_fault, unsigned mutation = 0) {
  auto config = jsoncons::json::parse(configuration());
  config["extensions"]["H"]["supported"] = true;
  config["extensions"]["Svade"]["supported"] = true;
  config["extensions"]["Svadu"]["supported"] = false;
  config["memory"]["pmp"]["count"] = 0;
  config["memory"]["pmp"]["usable_count"] = 0;
  SailChecker checker(config.to_string(), base, {{base, 32768}});
  unsigned order = 0, constant = 0;
  std::uint64_t pc = base;
  o::Privilege privilege{3, false};
  auto put = [&](std::uint64_t address, std::uint64_t value, unsigned size) {
    std::vector<std::uint8_t> data(size);
    for (unsigned i = 0; i < size; ++i) data[i] = value >> (8*i);
    checker.load(address, data);
  };
  auto emit = [&](unsigned insn, unsigned rd = 0, std::uint64_t value = 0,
                  std::optional<o::Retirement> retirement = {}, std::optional<o::Trap> trap = {}) {
    put(pc, insn, 4);
    const auto next = retirement.value_or(o::Retirement{pc+4, privilege});
    o::Record r{0,0,order,{0,order},o::Instruction{pc,insn,4,4,privilege,0},next,{}, {}};
    if (rd) r.effects[{0,0}] = o::RegisterWrite{o::Bank::Integer,rd,0,UINT64_MAX,value};
    if (trap) r.outcome = *trap;
    checker.check(r); ++order;
    pc = trap ? trap->target_pc : next.next_pc;
    privilege = trap ? trap->privilege : next.privilege;
  };
  auto csr = [&](unsigned address, std::uint64_t value) {
    const auto offset = 0x400 + 8*constant++;
    put(base+offset,value,8);
    emit((offset<<20)|(31<<15)|(3<<12)|(5<<7)|3,5,value);
    emit((address<<20)|(5<<15)|(1<<12)|0x73);
  };
  emit(0x00000f97,31,base);
  csr(0x305,base+0x200); csr(0x105,base+0x204); csr(0x205,base+0x208);
  csr(0x302,(1U<<8)|(1U<<10)|(1U<<20));
  csr(0x602,user ? 1U<<8 : 0);
  if (guest_fault) csr(0x680,(UINT64_C(8)<<60)|((base+0x4000)>>12));
  csr(0x341,base+0x100);
  csr(0x300,(UINT64_C(1)<<39)|(user ? 0 : 0x800));
  emit(0x30200073,0,0,o::Retirement{base+0x100,{user ? 0U : 1U,mutation == 1 ? false : true}});
  if (guest_fault) {
    o::Trap trap{20,pc,pc,base+0x204,{1,false},true,pc>>2,0};
    if (mutation == 2) trap.cause = 12;
    if (mutation == 3) trap.tval += 4;
    o::Record r{0,0,order,{0,order},o::Instruction{pc,0,0,0,privilege,0},trap,{}, {}};
    checker.check(r);
    return;
  }
  emit(0x00700393,7,mutation == 2 ? 8 : 7);
  o::Trap trap{user ? 8U : 10U,pc,0,base+(user ? 0x208 : 0x204),{1,user},false,0,0};
  if (mutation == 3) trap.privilege.virtualized = !user;
  emit(0x73,0,0,{},trap);
  // The guest sepc alias, or host sepc, must contain the independently computed EPC.
  emit(0x14102373,6,base+0x104);
}
void mailbox_case(unsigned xlen, bool observed_new, unsigned mutation = 0) {
  auto config = configuration(xlen);
  SailChecker checker(config, base, {{base, 4096}});
  checker.external_memory({base + 32, 8});
  const std::uint64_t mask = xlen == 32 ? UINT32_MAX : UINT64_MAX;
  const std::array<unsigned, 3> words{0x00000117, 0x02010183, 0x02010203}; // auipc; lb x3; lb x4
  std::vector<std::uint8_t> code;
  for (auto word : words) for (unsigned i = 0; i != 4; ++i) code.push_back(word >> (8 * i));
  checker.load(base, code);
  auto r = record(0, words[0]);
  r.effects[{0, 0}] = o::RegisterWrite{o::Bank::Integer, 2, 0, mask, base};
  checker.check(r);
  // A load admitted at sample 1 may complete after the host ACK at sample 2.
  // Both the old byte and the new byte are legal environmental observations.
  const std::array<std::uint8_t, 1> ack{0x80};
  checker.host_write(2, base + 32, ack);
  r = record(1, words[1]);
  const std::uint64_t data = observed_new ? 0x80 : 0;
  const std::uint64_t extended = observed_new ? mask - 127 : 0;
  r.effects[{0, 0}] = o::RegisterWrite{o::Bank::Integer, 3, 0, mask, extended ^ (mutation == 1 ? 1U : 0U)};
  r.effects[{2, 0}] = o::MemoryEffect{0, 0, o::AccessKind::Load, base + 32, true, base + 32, 1, true, false, data, 0, o::AccessResult::Success};
  auto& effect = std::get<o::MemoryEffect>(r.effects.at({2, 0}));
  if (mutation == 2) ++effect.physical_address;
  if (mutation == 3) effect.byte_mask = 3;
  if (mutation == 4) effect.physical_address += 8;
  if (mutation == 5) r.effects[{2, 1}] = effect;
  checker.check(r);
  r = record(2, words[2]);
  r.effects[{0, 0}] = o::RegisterWrite{o::Bank::Integer, 4, 0, mask, mask - 127};
  r.effects[{2, 0}] = o::MemoryEffect{0, 0, o::AccessKind::Load, base + 32, true, base + 32, 1, true, false, 0x80, 0, o::AccessResult::Success};
  checker.check(r);
}

void absent_tselect_case(unsigned xlen, unsigned mutation = 0) {
  auto config = jsoncons::json::parse(configuration(xlen));
  config["base"]["tselect_present"] = false;
  SailChecker checker(config.to_string(), base, {{base, 4096}});
  const unsigned encoding = 0x7a0027f3; // csrr a5, tselect, as used by OpenSBI.
  std::array<std::uint8_t, 4> code{};
  for (unsigned i = 0; i != 4; ++i) code[i] = encoding >> (8 * i);
  checker.load(base, code);
  auto r = record(0, encoding);
  r.outcome = o::Trap{2, base, config["base"]["xtval_nonzero"]["illegal_instruction"].as<bool>() ? encoding : 0U, 0, {3, false}, false, 0, 0};
  if (mutation == 1) r.outcome = o::Retirement{base + 4, {3, false}};
  if (mutation == 2) std::get<o::Trap>(r.outcome).cause = 3;
  checker.check(r);
}

int main() {
  for (unsigned xlen : {32, 64}) {
    absent_tselect_case(xlen);
    for (unsigned mutation : {1, 2}) {
      bool failed = false;
      try { absent_tselect_case(xlen, mutation); } catch (const std::runtime_error&) { failed = true; }
      if (!failed) throw std::runtime_error("tselect outcome corruption was not rejected");
    }
    mailbox_case(xlen, false); mailbox_case(xlen, true);
    for (unsigned mutation = 1; mutation <= 5; ++mutation) {
      bool failed = false;
      try { mailbox_case(xlen, true, mutation); } catch (const std::runtime_error&) { failed = true; }
      if (!failed) throw std::runtime_error("mailbox corruption was not rejected");
    }
  }
  for (unsigned xlen : {32,64}) {
    for (unsigned op : {0,1,2,4}) cache_case(op,xlen);
    for (unsigned mutation : {1,2,3,4}) {
      bool failed=false;
      try { cache_case(mutation==4 ? 1 : 4,xlen,mutation); }
      catch (const std::runtime_error&) { failed=true; }
      if (!failed) throw std::runtime_error("cache operation corruption was not rejected");
    }
  }
  for (const bool user : {false,true}) {
    guest_case(user,false);
    for (unsigned mutation=1;mutation<=3;++mutation) {
      bool failed=false;
      try { guest_case(user,false,mutation); } catch (const std::runtime_error&) { failed=true; }
      if (!failed) throw std::runtime_error("guest outcome corruption was not rejected");
    }
  }
  guest_case(false,true);
  for (unsigned mutation : {2,3}) {
    bool failed=false;
    try { guest_case(false,true,mutation); } catch (const std::runtime_error&) { failed=true; }
    if (!failed) throw std::runtime_error("guest-page fault corruption was not rejected");
  }
  rv32_cycle_high();
  run(0,32);
  interrupt_case(0,32);
  for (int mutation = 1; mutation <= 9; ++mutation) {
    bool rejected = false;
    try { run(mutation,32); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) throw std::runtime_error("RV32 mutation was not detected");
  }
  run(0);
  {
    SailChecker checker(configuration(), base, {{base, 4096}});
    const std::array<std::uint8_t, 8> code{0x73, 0x00, 0x50, 0x10, 0x93, 0x00, 0x70, 0x00};
    checker.load(base, code);
    checker.check(record(0, 0x10500073));
    auto next = record(1, 0x00700093);
    next.effects[{0, 0}] = o::RegisterWrite{o::Bank::Integer, 1, 0, UINT64_MAX, 7};
    checker.check(next);
    if (checker.checked() != 2) throw std::runtime_error("WFI must occupy exactly one record");
  }
  for (int mutation = 1; mutation <= 9; ++mutation) {
    bool rejected = false;
    try { run(mutation); }
    catch (const std::runtime_error& error) {
      rejected = std::string(error.what()).find("cosim mismatch:") != std::string::npos;
    }
    if (!rejected) throw std::runtime_error("mutation was not detected");
  }
  interrupt_case(0);
  for (unsigned mutation = 1; mutation <= 7; ++mutation) {
    bool rejected = false;
    try { interrupt_case(mutation); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) throw std::runtime_error("interrupt/CSR mutation was not detected");
  }
  std::cout << "Scalar checker: independent effects, interrupts, counters, and sixteen corruptions passed\n";
}
