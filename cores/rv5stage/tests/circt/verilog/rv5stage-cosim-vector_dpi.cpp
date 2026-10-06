// SPDX-License-Identifier: Apache-2.0
#include "../../../../../sims/cosim/events/dpi.h"
#include "../../../../../sims/cosim/tests/sail/vector-program.h"
#include "../../../../../sims/cosim/tests/sail/vector-records.h"
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace rhodium::cosim::observation;
namespace {
Collector collector;
std::unique_ptr<DpiBinding> binding;
vector_test::Program program(true);
std::ofstream records;
Word epoch=0, order=0, sample=0, previous_vector_completion=0;
bool overlapped=false;
void require(bool okay,const char* message) {
  if(!okay) { std::fprintf(stderr,"vector cosim order %llu: %s\n",(unsigned long long)order,message); std::abort(); }
}
}
extern "C" void vector_cosim_reset(std::int64_t e) {
  epoch=e; order=0; overlapped=false; previous_vector_completion=0;
  collector.reset(0,epoch,{0,0,{3,false},64,128});
  if(!binding) binding=std::make_unique<DpiBinding>(collector);
  if(e==0) if(const auto* path=std::getenv("COSIM_VECTOR_RECORDS")) {
    records.open(path);
    require(bool(records),"cannot open RTL record output");
  }
}
extern "C" int vector_cosim_count() { return program.records.size(); }
extern "C" int vector_cosim_instruction(int n) { return std::get<Instruction>(program.records.at(n).event).encoding; }
extern "C" void vector_cosim_begin(std::int64_t value) { sample=value; collector.begin_sample(sample); }
extern "C" void vector_cosim_end() {
  try {
    binding->check();
    for(const auto& r:collector.end_sample()) {
      require(order<program.records.size(),"extra record");
      const auto& expected=program.records[order];
      require(r.id.epoch==epoch && r.id.order==order,"wrong instruction ownership");
      if(std::get<Instruction>(r.event).encoding!=std::get<Instruction>(expected.event).encoding)
        std::fprintf(stderr,"actual PC=%llx insn=%llx expected=%llx\n",(unsigned long long)std::get<Instruction>(r.event).pc,(unsigned long long)std::get<Instruction>(r.event).encoding,(unsigned long long)std::get<Instruction>(expected.event).encoding);
      require(std::get<Instruction>(r.event).encoding==std::get<Instruction>(expected.event).encoding,"wrong instruction");
      require(std::holds_alternative<Retirement>(r.outcome),"unexpected trap");
      require(std::get<Retirement>(r.outcome).next_pc==vector_test::Program::base+4*(order+1),"wrong next PC");
      auto insn=std::get<Instruction>(r.event).encoding;
      if((insn&127)==0x57 && ((insn>>12)&7)!=7) {
        overlapped |= r.sample < previous_vector_completion;
        previous_vector_completion=sample;
      }
      // Normalize physical rows and authored element writes into architectural bits.
      using Key=std::pair<unsigned,unsigned>;
      std::map<Key,bool> actual_bits,expected_bits;
      auto collect=[&](const auto& effects,auto& result) {
        for(const auto& [id,effect]:effects) {
          (void)id;
          if(const auto* w=std::get_if<RegisterWrite>(&effect)) {
            require(w->bank!=Bank::FloatingPoint,"unexpected FPR write");
            for(unsigned b=0;b<64;b++) if(w->mask&(1ULL<<b))
              require(result.emplace(Key{unsigned(w->index)+(w->bank==Bank::Vector?32:0),unsigned(w->bit_offset)+b},bool(w->value&(1ULL<<b))).second,"duplicate bit");
          }
        }
      };
      collect(r.effects,actual_bits); collect(expected.effects,expected_bits);
      require(actual_bits==expected_bits,"register value/mask/destination");
      if(records.is_open()) vector_test::save(records,r);
      order++;
    }
  } catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); std::abort(); }
}
extern "C" void vector_cosim_finish() {
  binding->check();
  if(records.is_open()) records.flush();
  try { if(epoch==1 || order!=program.records.size()) collector.finish(); }
  catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); std::abort(); }
  require(order==program.records.size(),"records failed to drain");
  require(overlapped,"test did not exercise overlapping ownership");
  if(records.is_open()) records.flush();
  std::printf("vector cosim: %llu ordered records, epoch %llu, passive overlap passed\n",(unsigned long long)order,(unsigned long long)epoch);
}
