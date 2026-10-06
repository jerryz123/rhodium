// SPDX-License-Identifier: Apache-2.0
#include "../../sail/checker.h"
#include "config_utils.h"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace rhodium::cosim;
namespace o = rhodium::cosim::observation;
constexpr std::uint64_t base = 0x80000000;
constexpr std::array backing{MemoryRange{base,4096}};
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(std::string(__func__) + ": " #condition); } while (false)
unsigned csr(unsigned address, unsigned op = 2, unsigned rd = 13, unsigned source = 0) {
  return address << 20 | source << 15 | op << 12 | rd << 7 | 0x73;
}
std::string config(unsigned xlen) {
  auto value = jsoncons::json::parse(xlen == 32 ? get_default_rv32_config() : get_default_config());
  value["platform"]["clint"]["supported"] = false;
  value["platform"]["simple_interrupt_generator"]["supported"] = false;
  value["memory"]["pmp"]["count"] = 0;
  value["memory"]["pmp"]["usable_count"] = 0;
  value["extensions"]["Sscofpmf"]["supported"] = true;
  value["base"]["writable_hpm_counters"]["value"] = "0x80000008";
  value["base"]["hpm_events"]["restricted"] = true;
  value["base"]["hpm_events"]["supported"] = jsoncons::json::parse(
      R"([{"len":32,"value":"0x0"},{"len":32,"value":"0x1"},{"len":32,"value":"0x2"}])");
  return value.to_string();
}
std::vector<std::uint8_t> bytes(std::initializer_list<unsigned> words) {
  std::vector<std::uint8_t> result;
  for (auto word : words) for (unsigned i=0;i<4;++i) result.push_back(word>>(8*i));
  return result;
}
void reads(unsigned xlen, unsigned index) {
  SailReference model(config(xlen),base,backing);
  model.load(base,bytes({csr(0xb00+index),csr(0xc00+index),csr(0xb80+index),csr(0xc80+index)}));
  StepInputs inputs;
  inputs.hpm_counters[index]=UINT64_C(0x1234567887654321);
  CHECK(model.step(inputs).retired);
  CHECK(model.integer_register(13)==(xlen==32 ? 0x87654321 : inputs.hpm_counters[index]));
  CHECK(model.step(inputs).retired);
  CHECK(model.integer_register(13)==(xlen==32 ? 0x87654321 : inputs.hpm_counters[index]));
  if (xlen==32) {
    CHECK(model.step(inputs).retired && model.integer_register(13)==0x12345678);
    CHECK(model.step(inputs).retired && model.integer_register(13)==0x12345678);
  } else CHECK(model.step(inputs).trap->cause==2);
}
void rmw(unsigned xlen) {
  SailReference model(config(xlen),base,backing);
  model.load(base,bytes({csr(0xb03,6,0,1),csr(0xb03,7,0,2),csr(0xb03,1),csr(0xb03,1,0),csr(0xb04)}));
  StepInputs inputs;
  inputs.hpm_counters[3]=42;
  CHECK(model.step(inputs).retired && model.csr(0xb03)==43); // rd=x0 still reads for CSRRSI.
  inputs.hpm_counters[3]=47;
  CHECK(model.step(inputs).retired && model.csr(0xb03)==45);
  inputs.hpm_counters[3]=51;
  CHECK(model.step(inputs).retired && model.integer_register(13)==51 && model.csr(0xb03)==0);
  CHECK(model.step(inputs).retired && model.csr(0xb03)==0); // CSRRW rd=x0 suppresses the read.
  CHECK(model.step(inputs).retired && model.integer_register(13)==0); // Unimplemented stays zero.
}
void permissions(unsigned xlen) {
  for (bool user : {false,true}) {
    SailReference model(config(xlen),base,backing);
    if (user) {
      // Enter U with mcounteren=0; a supplied value cannot make the alias legal.
      model.load(base,bytes({0x00000297,0x01828293,csr(0x341,1,0,5),csr(0x300,1,0),0x30200073,0x13,csr(0xc03)}));
      for (unsigned i=0;i<5;++i) CHECK(model.step().retired);
    } else model.load(base,bytes({csr(0xc03,6,13,1)})); // Write to read-only alias.
    StepInputs inputs; inputs.hpm_counters[3]=42;
    const auto result=model.step(inputs);
    CHECK(result.trap && result.trap->cause==2 && !result.retired);
    CHECK(model.integer_register(13)==0 && model.csr(0xb03)==0);
  }
}
void overflow(unsigned xlen) {
  SailReference model(config(xlen),base,backing);
  const unsigned selector=xlen==32 ? 0x723 : 0x323;
  model.load(base,bytes({csr(selector),0x000020b7,csr(0x344,3,0,1),csr(0xda0),
                        csr(0x344),csr(selector,1,0),csr(0xda0),0xfff00093,
                        csr(0x304,1,0,1),csr(0x300,6,0,8),0x13}));
  StepInputs inputs; inputs.hpm_counters[3]=0; inputs.hpm_overflows=8;
  CHECK(model.step(inputs).retired);
  CHECK(model.integer_register(13)==(UINT64_C(1)<<(xlen-1)));
  CHECK((model.csr(0x344)&8192)!=0);
  inputs.hpm_overflows=0;
  CHECK(model.step(inputs).retired && model.step(inputs).retired);
  CHECK((model.csr(0x344)&8192)==0); // Clearing pending does not clear OF.
  CHECK(model.step(inputs).retired && model.integer_register(13)==8);
  inputs.hpm_overflows=8;
  CHECK(model.step(inputs).retired && model.integer_register(13)==0); // Old OF blocks a new request.
  inputs.hpm_overflows=0;
  CHECK(model.step(inputs).retired && model.csr(selector)==0);
  inputs.hpm_overflows=8;
  CHECK(model.step(inputs).retired && model.integer_register(13)==8);
  inputs.hpm_overflows=0;
  CHECK(model.step(inputs).retired && model.step(inputs).retired && model.step(inputs).retired);
  inputs.interrupt_boundary=false;
  CHECK(model.step(inputs).retired); // Pending IRQ waits for a drained boundary.
  inputs.interrupt_boundary=true;
  const auto irq=model.step(inputs);
  CHECK(irq.trap && irq.trap->interrupt && irq.trap->cause==13);
}
void supervisor_visibility(unsigned xlen, bool enabled) {
  SailReference model(config(xlen),base,backing);
  model.load(base,bytes({csr(0x306,5,0,enabled ? 8 : 0),0x00100093,0x00b09093,
                        csr(0x300,1,0,1),0x00000297,0x01028293,csr(0x341,1,0,5),
                        0x30200073,csr(0xda0),csr(0xc03)}));
  StepInputs inputs; inputs.hpm_counters[3]=77; inputs.hpm_overflows=8;
  CHECK(model.step(inputs).retired);
  inputs.hpm_overflows=0;
  for (unsigned i=0;i<7;++i) CHECK(model.step(inputs).retired);
  CHECK(model.step(inputs).retired && model.integer_register(13)==(enabled ? 8 : 0));
  const auto read=model.step(inputs);
  if (enabled) CHECK(read.retired && model.integer_register(13)==77);
  else CHECK(read.trap && read.trap->cause==2);
}
void checker(unsigned xlen, bool corrupt) {
  SailChecker checker(config(xlen),base,{{base,4096}});
  const auto encoding=csr(0xb03);
  checker.load(base,bytes({encoding}));
  o::Record record{0,0,0,{0,0},o::Instruction{base,encoding,4,4,{3,false},0},o::Retirement{base+4,{3,false}},{},{}};
  record.environment.hpm_counters[3]=37;
  record.effects[{0,0}]=o::RegisterWrite{o::Bank::Integer,13,0,xlen==32 ? UINT32_MAX : UINT64_MAX,corrupt ? 36U : 37U};
  checker.check(record);
}
int main() {
  for (unsigned xlen : {32,64}) {
    for (unsigned index : {3,31}) reads(xlen,index);
    rmw(xlen); permissions(xlen); overflow(xlen); checker(xlen,false);
    for (bool enabled : {false,true}) supervisor_visibility(xlen,enabled);
    bool rejected=false;
    try { checker(xlen,true); } catch (const std::runtime_error&) { rejected=true; }
    CHECK(rejected);
    for (bool overflow_input : {false,true}) {
      SailReference model(config(xlen),base,backing);
      model.load(base,bytes({0x13}));
      StepInputs inputs;
      if (overflow_input) inputs.hpm_overflows=16; else inputs.hpm_counters[4]=42;
      rejected=false;
      try { model.step(inputs); } catch (const std::runtime_error&) { rejected=true; }
      CHECK(rejected);
    }
  }
  std::cout<<"HPM: raw reads, RV32 halves, x0/RMW, permissions, zero slots, OF/pending/IRQ and corruptions passed\n";
}
