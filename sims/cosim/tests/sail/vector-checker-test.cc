// SPDX-License-Identifier: Apache-2.0
#include "../../sail/checker.h"
#include "vector-config.h"
#include "vector-program.h"
#include "vector-records.h"
#include <fstream>
#include <memory>
#include <iostream>
#include <stdexcept>
using namespace rhodium::cosim;
void load(SailChecker& checker, const vector_test::Program& program) {
  std::vector<std::uint8_t> initial(4096);
  for(unsigned i=2048;i<initial.size();++i) initial[i]=(i*37+11)&255;
  unsigned index=0;
  for(const auto& r:program.records) {
    auto insn=std::get<observation::Instruction>(r.event).encoding;
    for(unsigned b=0;b<4;++b) initial[index++]=insn>>(8*b);
  }
  if(index>=2048) throw std::runtime_error("test code overlaps data");
  checker.load(vector_test::Program::base,initial);
}
void run(unsigned mutation) {
  constexpr std::uint64_t base=0x80000000;
  SailChecker checker(vector_configuration(),base,{{base,4096}});
  vector_test::Program program(true);
  load(checker,program);
  for (auto& r:program.records) {
    std::get<observation::Instruction>(r.event).pc+=base;
    std::get<observation::Retirement>(r.outcome).next_pc+=base;
  }
  auto& record=program.records[7]; // vadd.vv, all active.
  auto& effect=std::get<observation::RegisterWrite>(record.effects.at({5,0}));
  if(mutation==1) effect.value^=1;
  if(mutation==2) effect.mask&=~2ULL; // Drop a bit that differs from the old state.
  if(mutation==3) effect.index++;
  if(mutation==4) effect.bit_offset+=64;
  if(mutation==5) record.effects.erase({5,0});
  if(mutation==6) record.effects[{5,99}]=effect;
  if(mutation==7) std::get<observation::CsrUpdate>(record.effects.at({1,0})).value++;
  if(mutation==8) std::get<observation::CsrUpdate>(record.effects.at({1,1})).value^=8;
  if(mutation==9) std::get<observation::CsrUpdate>(record.effects.at({1,2})).value=1;
  if(mutation==10) std::get<observation::CsrUpdate>(record.effects.at({1,3})).value=1;
  // A write into an inactive element is not hidden by a permissive data mask.
  if(mutation==11) program.records[10].effects[{5,99}]=observation::RegisterWrite{observation::Bank::Vector,8,8,255,0};
  if(mutation==13) std::swap(program.records[5].effects,program.records[6].effects);
  if(mutation>=14) {
    auto it=std::find_if(program.records.begin(),program.records.end(),[](const auto& r) {
      return (std::get<observation::Instruction>(r.event).encoding&127)==39;
    });
    auto found=std::find_if(it->effects.begin(),it->effects.end(),[](const auto& item) { return std::holds_alternative<observation::MemoryEffect>(item.second); });
    auto& m=std::get<observation::MemoryEffect>(found->second);
    if(mutation==14) m.write_data^=1;
    if(mutation==15) m.virtual_address++;
    if(mutation==16) it->effects.erase(found);
    if(mutation==17) { m.physical_valid=true; m.physical_address=m.virtual_address+1; }
  }
  for(const auto& r:program.records) checker.check(r);
  if(checker.checked()!=program.records.size()) throw std::runtime_error("vector stream did not drain");
}
void illegal_memory(unsigned encoding, unsigned mutation) {
  namespace o = observation;
  constexpr auto base = vector_test::Program::base;
  SailChecker checker(vector_configuration(),base,{{base,4096}});
  vector_test::Program program;
  program.records.resize(4); // VS enabled, VL=16, e8,m1.
  auto& illegal = program.add(encoding);
  illegal.outcome = o::Trap{2,base+16,encoding,0,{3,false},false,0,0};
  if (mutation == 1) std::get<o::Trap>(illegal.outcome).cause = 5;
  if (mutation == 2) illegal.effects[{5,0}] = o::RegisterWrite{o::Bank::Vector,4,0,255,1};
  if (mutation == 3) illegal.outcome = o::Retirement{20,{3,false}};
  load(checker,program);
  for (auto& record : program.records) {
    std::get<o::Instruction>(record.event).pc += base;
    if (auto* retired = std::get_if<o::Retirement>(&record.outcome)) retired->next_pc += base;
    checker.check(record);
  }
}
int main(int argc, char** argv) {
  if(argc==2) {
    std::ifstream input(argv[1]);
    if(!input) throw std::runtime_error("missing RTL vector records");
    std::unique_ptr<SailChecker> checker;
    std::uint64_t epoch=UINT64_MAX, count=0, total=0;
    vector_test::Program program(true);
    observation::Record record;
    while(vector_test::read(input,record)) {
      if(record.id.epoch!=epoch) {
        if(checker && count!=program.records.size()) throw std::runtime_error("incomplete RTL epoch");
        checker.reset();
        checker=std::make_unique<SailChecker>(vector_configuration(),vector_test::Program::base,std::vector<MemoryRange>{{vector_test::Program::base,4096}});
        load(*checker,program); epoch=record.id.epoch; count=0;
      }
      if(record.id.order!=count) throw std::runtime_error("RTL stream order");
      checker->check(record); ++count; ++total;
    }
    if(count!=program.records.size() || total!=2*program.records.size()) throw std::runtime_error("incomplete RTL stream");
    std::cout<<"RTL-to-Sail: "<<total<<" records, two reset epochs passed\n";
    return 0;
  }
  run(0);
  // Reserved MEW and a segmented-store group extending beyond v31 must be
  // checked as IllegalInstruction, not rejected by cosim's execution geometry.
  for (unsigned encoding : {0x12000007U,0xe2000fa7U}) {
    illegal_memory(encoding,0);
    for (unsigned mutation : {1,2,3}) {
      bool rejected = false;
      try { illegal_memory(encoding,mutation); }
      catch (const std::runtime_error& error) { rejected = std::string(error.what()).starts_with("cosim mismatch:"); }
      if (!rejected) throw std::runtime_error("illegal vector outcome corruption accepted");
    }
  }
  for(unsigned m : {1,2,3,4,5,6,7,8,9,10,11,13,14,15,16,17}) {
    bool rejected=false;
    try { run(m); } catch(const std::runtime_error& e) { rejected=std::string(e.what()).starts_with("cosim mismatch:"); }
    if(!rejected) throw std::runtime_error("vector corruption accepted: "+std::to_string(m));
  }
  std::cout<<"Vector checker: integer and memory post-state, widths/masks/tails/vstart and 16 mutations passed\n";
}
