// SPDX-License-Identifier: Apache-2.0
#include "../../rv5stage/adapter.h"
#include "../sail/vector-records.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <random>
#include <sstream>
using namespace rhodium::cosim;
using namespace rhodium::cosim::observation;
using namespace rhodium::cosim::rv5stage;
void require(bool ok) { if (!ok) throw std::runtime_error("hart adapter test failed"); }
struct Sample {
  ScalarCycle cycle{};
  HeaderSample header{};
  BoundarySample boundary{};
  ResponseSample response{};
  PhysicalSample physical{};
  std::array<RequestSample,2> request{};
  std::array<ArithmeticSample,2> arithmetic{};
  FpSample fp{};
  Sample() {
    cycle.xlen=64; cycle.flen=64;
    header.pc=0x8000; header.instruction=0x13;
    boundary.privilege=3; boundary.next_pc=0x8004; boundary.next_privilege=3;
    boundary.hpm_enabled=1; boundary.hpm_counter=123;
    request[1].index=1; arithmetic[1].index=1;
  }
};
void capture(std::vector<std::function<void()>>& calls, const Sample& s, Word instance, Word epoch, Word cycle) {
  auto add = [&](auto value) {
    calls.push_back([=] {
      dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, Collector&) {
        adapter.capture(cycle,instance,epoch,value);
      });
    });
  };
  add(s.cycle); add(s.header); add(s.boundary); add(s.response); add(s.physical);
  for (auto request:s.request) add(request);
  for (auto arithmetic:s.arithmetic) add(arithmetic);
  add(s.fp);
}
std::string run(unsigned seed) {
  Collector collector;
  collector.reset(0,0,{0,0x8000,{3,false},64,0});
  collector.reset(1,0,{1,0x8000,{3,false},64,0});
  DpiBinding binding(collector);
  std::mt19937 random(seed);
  std::ostringstream out;
  unsigned count=0;
  const std::array<Word,7> values{11,22,33,44,55,66,77};
  const std::array<Word,7> registers{1,2,3,4,5,4,6};
  for (Word cycle=0;cycle<13;++cycle) {
    Sample s;
    auto instruction = [&](Word order) {
      s.cycle.wb_commit_valid=1; s.boundary.csr_commit=1; s.boundary.csr_retired=1;
      s.header.pc=0x8000+4*order; s.boundary.next_pc=s.header.pc+4;
    };
    if (cycle==0 || cycle==9) {
      instruction(cycle==0?0:6);
      s.cycle.write_valid=1; s.cycle.write_rd=cycle==0?1:6; s.cycle.write_data=cycle==0?11:77;
    }
    if (cycle==1) {
      s.boundary.hpm_overflow=1;
      s.header.pc=0x8004; s.cycle.wb_retained_memory_accepted=1;
      s.cycle.wb_memory_accepted=1; s.cycle.wb_attempt=1; s.cycle.commit_rd=2;
      s.request[0]={0,0x1000,1,3,0,0,2,1,0,2};
      s.request[1]=s.request[0]; s.request[1].index=1;
    }
    if (cycle==2) {
      s.cycle.wb_memory_pending=1; s.boundary.csr_retired=1; s.boundary.csr_commit=1;
      s.boundary.next_pc=0x8008;
    }
    if (cycle==3) {
      instruction(2); s.cycle.wb_commit_has_multiply=1;
      s.arithmetic[0].accepted=1; s.arithmetic[0].issue_rd=3;
      s.boundary.hpm_overflow=1;
    }
    if (cycle==4 || cycle==5 || cycle==7) {
      instruction(cycle==4?3:cycle==5?4:5);
      s.fp.issued=1; s.fp.issue_fp=1; s.fp.issue_rd=cycle==5?5:4;
    }
    if (cycle==5) {
      s.cycle.response_fire=1; s.response.writeback=2; s.response.data=22;
      s.arithmetic[0].completed=1; s.arithmetic[0].rd=3; s.arithmetic[0].data=33;
    }
    if (cycle>=6 && cycle<=8) {
      s.fp.completed=1; s.fp.complete_fp=1; s.fp.rd=cycle==6?5:4;
      s.fp.fp_value=cycle==6?55:cycle==7?44:66;
      s.fp.flags_valid=1; s.fp.exception_flags=cycle==6?16:cycle==7?1:0;
    }
    if (cycle==11) {
      s.boundary.csr_interrupt_take=1; s.boundary.cause=(1ULL<<63)|7;
      s.boundary.target_privilege=3; s.boundary.target_pc=0x9000; s.boundary.epc=0x801c;
    }
    if (cycle==9) s.boundary.hpm_overflow=1;
    collector.begin_sample(cycle);
    std::vector<std::function<void()>> calls;
    capture(calls,s,0,0,cycle); capture(calls,s,1,0,cycle);
    std::shuffle(calls.begin(),calls.end(),random);
    for (auto& call:calls) call();
    binding.check(); binding.check();
    for (const auto& record:collector.end_sample()) {
      if (record.id.order<7) {
        require(record.environment.hpm_counters.at(3)==123);
        require(record.environment.hpm_overflows==(record.id.order==2 || record.id.order==3 ? 8 : 0));
        const bool fp=record.id.order>=3 && record.id.order<=5;
        const auto& write=std::get<RegisterWrite>(record.effects.at({fp?3ULL:0ULL,0}));
        require(write.index==registers[record.id.order] && write.value==values[record.id.order]);
        require(write.bank==(fp?Bank::FloatingPoint:Bank::Integer));
        vector_test::save(out,record);
      } else {
        require(record.environment.hpm_overflows==8);
        require(std::holds_alternative<Interrupt>(record.event));
        require(std::get<Trap>(record.outcome).cause==7 && record.effects.empty());
        out<<"interrupt "<<record.instance<<' '<<record.id.order<<'\n';
      }
      ++count;
    }
  }
  require(count==16);
  // Warm reset one hart without rebuilding the binding or disturbing its sibling.
  collector.reset(0,1,{0,0x8000,{3,false},64,0});
  for (Word cycle=13;cycle<15;++cycle) {
    Sample s;
    if (cycle==13) { s.cycle.wb_commit_valid=1; s.boundary.csr_retired=1; s.boundary.csr_commit=1; }
    collector.begin_sample(cycle);
    std::vector<std::function<void()>> calls;
    capture(calls,s,0,1,cycle);
    std::shuffle(calls.begin(),calls.end(),random);
    for (auto& call:calls) call();
    binding.check();
    for (const auto& record:collector.end_sample()) {
      require(record.id.epoch==1 && record.id.order==0);
      require(record.environment.hpm_overflows==0);
      vector_test::save(out,record); ++count;
    }
  }
  require(count==17); collector.finish();
  return out.str();
}
void vector_identity(unsigned seed) {
  Collector collector; collector.reset(0,0,{0,0x8000,{3,false},64,128}); DpiBinding binding(collector);
  std::mt19937 random(seed);
  unsigned count=0;
  for (Word cycle=0;cycle<3;++cycle) {
    Sample s; s.cycle.vector=1;
    if (cycle<2) {
      s.cycle.wb_commit_valid=1; s.boundary.csr_commit=1; s.boundary.csr_retired=1;
      s.header.pc+=4*cycle; s.boundary.next_pc+=4*cycle;
      if(cycle==1) s.header.instruction=0x57;
    }
    collector.begin_sample(cycle);
    std::vector<std::function<void()>> calls;
    capture(calls,s,0,0,cycle);
    calls.push_back([] { rhodium_rv5stage_vector_cycle(0,64,128,2,0,0,0,0,0); });
    if(cycle==1) {
      // Fast vector enqueue is not the retained-memory WB admission signal.
      calls.push_back([] { rhodium_rv5stage_vector_admission(0); });
      calls.push_back([] { rhodium_rv5stage_vector_dispatch(0,0,0,0,0x57,0); });
    }
    if(cycle==2) {
      calls.push_back([] { rhodium_rv5stage_vector_write(0,0,UINT64_MAX,9,1,0,0,0,0,0); });
      calls.push_back([] { rhodium_rv5stage_vector_drain(0,0); });
    }
    std::shuffle(calls.begin(),calls.end(),random);
    for(auto& call:calls) call();
    binding.check();
    for(const auto& record:collector.end_sample()) {
      if(record.id.order==1) require(std::get<RegisterWrite>(record.effects.at({5,0})).value==9);
      ++count;
    }
  }
  require(count==2); collector.finish();
}
void vector_scalar_return(unsigned xlen, bool floating) {
  Collector collector; collector.reset(0,0,{0,0x8000,{3,false},xlen,64}); DpiBinding binding(collector);
  unsigned count=0;
  for (Word cycle=0;cycle<3;++cycle) {
    Sample s; s.cycle.xlen=xlen; s.cycle.flen=0; s.cycle.vector=1;
    s.header.instruction=0x423022d7;
    if(cycle==0) {
      s.cycle.wb_commit_valid=1;
      s.boundary.csr_commit=1; s.boundary.csr_retired=1;
    }
    collector.begin_sample(cycle);
    std::vector<std::function<void()>> calls; capture(calls,s,0,0,cycle);
    for(auto& call:calls) call();
    rhodium_rv5stage_vector_cycle(0,xlen,64,2,0,0,0,0,0);
    if(cycle==0) {
      rhodium_rv5stage_vector_admission(0);
      rhodium_rv5stage_vector_dispatch(0,0,0,0,s.header.instruction,0);
    }
    if(cycle==1) {
      rhodium_rv5stage_vector_scalar_write(0,0,floating,floating ? 0 : 5,15);
      rhodium_rv5stage_vector_drain(0,0);
    }
    binding.check();
    for(const auto& record:collector.end_sample()) {
      const auto& write=std::get<RegisterWrite>(record.effects.at({5,0}));
      require(record.id.order==0 && write.index==(floating ? 0 : 5) && write.value==15);
      require(write.bank==(floating ? Bank::FloatingPoint : Bank::Integer));
      require(write.mask==(xlen==32 ? UINT32_MAX : UINT64_MAX)); ++count;
    }
  }
  require(count==1); collector.finish();
}
void vector_fp_flags(unsigned seed) {
  Collector collector; collector.reset(0,0,{0,0x8000,{3,false},64,128}); DpiBinding binding(collector);
  std::mt19937 random(seed); unsigned count=0;
  constexpr Word code=(1U<<25)|(8<<20)|(12<<15)|(1<<12)|(16<<7)|0x57;
  for(Word cycle=0;cycle<4;++cycle) {
    Sample s; s.cycle.vector=1;
    if(cycle<2) {
      s.cycle.wb_commit_valid=1; s.boundary.csr_commit=1; s.boundary.csr_retired=1;
      s.header.pc+=4*cycle; s.boundary.next_pc+=4*cycle;
      s.header.instruction=cycle==0?code:0x00000253;
    }
    if(cycle==1) { s.fp.issued=1; s.fp.issue_fp=1; s.fp.issue_rd=4; }
    if(cycle==2) {
      s.fp.completed=1; s.fp.complete_fp=1; s.fp.rd=4; s.fp.fp_value=7;
      s.fp.flags_valid=1; s.fp.exception_flags=1;
    }
    collector.begin_sample(cycle);
    std::vector<std::function<void()>> calls;
    capture(calls,s,0,0,cycle);
    calls.push_back([]{rhodium_rv5stage_vector_cycle(0,64,128,2,0,0,0,0,0);});
    calls.push_back([&]{rhodium_rv5stage_vector_fp(0,cycle==0,0,0,cycle==0,0,cycle==2,0,1,16);});
    if(cycle==0) {
      calls.push_back([]{rhodium_rv5stage_vector_admission(0);});
      calls.push_back([]{rhodium_rv5stage_vector_dispatch(0,0,0,0,code,16);});
    }
    if(cycle==2) {
      calls.push_back([]{rhodium_rv5stage_vector_write(0,32,UINT64_MAX,9,1,0,0,0,0,0);});
      calls.push_back([]{rhodium_rv5stage_vector_drain(0,0);});
    }
    std::shuffle(calls.begin(),calls.end(),random);
    for(auto& call:calls) call();
    binding.check();
    for(const auto& r:collector.end_sample()) {
      const auto producer=r.id.order==0?5U:4U;
      const auto effect=r.effects.find({producer,r.id.order==0?1U:0U});
      require(effect!=r.effects.end() && std::get<CsrUpdate>(effect->second).value==(r.id.order==0?16U:1U));
      ++count;
    }
  }
  require(count==2); collector.finish();
}
void cache_operations(unsigned xlen, Word op, bool fault) {
  Collector collector; collector.reset(0,0,{0,0x8000,{3,false},xlen,0}); DpiBinding binding(collector);
  unsigned records=0;
  for (Word cycle=0;cycle<2;++cycle) {
    Sample s; s.cycle.xlen=xlen; s.cycle.flen=0;
    s.physical.pipeline_valid=1; s.physical.pipeline_address=0x203f;
    if (cycle==1) {
      s.cycle.wb_commit_valid=1; s.cycle.wb_attempt=1;
      s.cycle.wb_data_fault=fault; s.cycle.fault_address=0x103f;
      s.boundary.csr_retired=!fault; s.boundary.csr_synchronous_trap=fault;
      s.boundary.cause=7; s.boundary.epc=0x8000; s.boundary.tval=0x103f;
      s.request[1].address=0x103f; s.request[1].access=op; s.request[1].width=3;
    }
    collector.begin_sample(cycle);
    std::vector<std::function<void()>> calls; capture(calls,s,0,0,cycle);
    for (auto& call:calls) call();
    binding.check();
    for (const auto& record:collector.end_sample()) {
      ++records;
      require(record.effects.size()==(op==6 && !fault ? 8 : 1));
      for (const auto& [id,effect]:record.effects) {
        const auto& m=std::get<MemoryEffect>(effect);
        require(m.kind==(op==6 ? AccessKind::Store : AccessKind::CacheOperation));
        require(m.byte_mask==255 && !m.read_valid && m.write_valid==(op==6 && !fault) && m.write_data==0);
        require(m.virtual_address==0x1000+id.index*8 && m.physical_valid==!fault);
        if (!fault) require(m.physical_address==0x2000+id.index*8);
      }
    }
  }
  collector.finish(); require(records==1);
}
void invalid(unsigned mutation) {
  Collector collector; collector.reset(0,0,{0,0x8000,{3,false},64,0}); DpiBinding binding(collector);
  Sample s;
  if (mutation==0) s.cycle.response_fire=1;
  if (mutation==1) { s.fp.completed=1; s.fp.complete_fp=1; s.fp.rd=4; }
  collector.begin_sample(0);
  std::vector<std::function<void()>> calls;
  capture(calls,s,0,0,0); for (auto& call:calls) call();
  bool failed=false; try { binding.check(); } catch(const std::exception&) { failed=true; }
  require(failed);
}
int main() {
  for (unsigned xlen : {32,64}) for (Word op : {6,7,8,9}) for (bool fault : {false,true}) cache_operations(xlen,op,fault);
  auto baseline=run(0);
  for (unsigned seed=1;seed<64;++seed) require(run(seed)==baseline);
  for (unsigned seed=0;seed<64;++seed) vector_identity(seed);
  for (unsigned xlen : {32,64}) for (bool floating : {false,true}) vector_scalar_return(xlen,floating);
  for (unsigned seed=0;seed<64;++seed) vector_fp_flags(seed);

  for (unsigned mutation=0;mutation<2;++mutation) invalid(mutation);
  std::cout<<"Hart adapter: shuffled scalar/FP callbacks, delayed/reordered returns, same-edge reuse, two harts, reset, interrupt, invalid owner passed\n";
}
