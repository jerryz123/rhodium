// Checks settled RV2Wide ownership through fixed writes, variable returns, and recovery.
// SPDX-License-Identifier: Apache-2.0
#include "../../rv2wide/adapter.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <random>
#include <sstream>
using namespace rhodium::cosim::observation;
using namespace rhodium::cosim::rv2wide;
void require(bool value) { if (!value) throw std::runtime_error("RV2Wide adapter test failed"); }
struct Sample {
  std::array<LaneSample,3> lanes{};
  std::array<CompletionSample,4> completions{};
  std::array<FpSample,2> fp{};
  BoundarySample boundary{}; PhysicalSample physical{};
  Sample() {
    boundary.privilege=3; boundary.next_privilege=3; boundary.target_privilege=3;
    for (unsigned i=0;i<3;++i) { lanes[i].index=i; lanes[i].encoding=0x13; }
    for (unsigned i=0;i<4;++i) completions[i].index=i;
    for (unsigned i=0;i<2;++i) fp[i].index=i;
  }
  LaneSample& retire(unsigned slot, Word pc, Word rd=0, Word data=0) {
    auto& l=lanes[slot]; l.retired=1; l.pc=pc; l.next_pc=pc+4; l.rd=rd; l.write=rd!=0; l.data=data;
    return l;
  }
};
std::vector<Record> sample(Collector& c, DpiBinding& binding, Word cycle, const Sample& s, std::mt19937& random) {
  c.begin_sample(cycle);
  std::vector<std::function<void()>> calls;
  auto add=[&](auto event) { calls.push_back([&,event] {
    dpi_receive_adapter<HartAdapter>([&](HartAdapter& a, Collector&) { a.capture(cycle,0,c.epoch(0),event); });
  }); };
  for (auto l:s.lanes) add(l);
  for (auto v:s.completions) add(v);
  for (auto v:s.fp) add(v);
  add(s.boundary); add(s.physical);
  std::shuffle(calls.begin(),calls.end(),random);
  for (auto& call:calls) call();
  binding.check(); return c.end_sample();
}
std::string run(unsigned seed) {
  Collector c; c.reset(0,0,{0,0x8000,{3,false},64,0}); DpiBinding binding(c);
  std::mt19937 random(seed); std::vector<Record> records;
  for (Word cycle=0;cycle<16;++cycle) {
    Sample s;
    if (cycle==0) { s.retire(0,0x8000,1,11); s.retire(1,0x8004,2,22); }
    if (cycle==1) {
      auto& l=s.retire(0,0x8008,3); l.deferred=1; l.memory=1; l.access=1; l.width=3; l.address=0x1000;
      s.physical.request_valid=1; s.physical.request_address=0x9000;
      s.retire(1,0x800c,5,55);
    }
    if (cycle==2) { auto& l=s.retire(0,0x8010,4); l.deferred=1; l.service=1; }
    if (cycle==3) s.retire(0,0x8014);
    // A multiply writes directly while an unrelated variable result enters RR.
    if (cycle==5) { s.completions[0]={0,1,0x8008,3,1,33}; s.completions[1]={1,1,0x8010,4,1,44}; s.completions[3]={3,1,0x8010,4,1,44}; }
    if (cycle==8) s.completions[3]={3,1,0x8008,3,1,33};
    if (cycle==8) {
      s.retire(0,0x8018,6,66);
      s.lanes[2].pc=0x801c; s.lanes[2].encoding=0;
      s.boundary.trap=1; s.boundary.cause=2; s.boundary.epc=0x801c; s.boundary.target=0x9000;
    }
    if (cycle==9) {
      s.boundary.interrupt=1; s.boundary.cause=(Word{1}<<63)|7;
      s.boundary.epc=0x9000; s.boundary.target=0xa000;
    }
    if (cycle==10) {
      auto& l=s.lanes[0]; l.split=1; l.pc=0xa000; l.memory=1; l.access=2; l.width=3; l.address=0x1ffd;
    }
    if (cycle==11) {
      s.physical.fragment_valid=1; s.physical.fragment_address=0x4ff8; s.physical.fragment_virtual=0x1ffd;
      s.physical.fragment_mask=0xe0; s.physical.fragment_data=0x3322110000000000;
    }
    if (cycle==12) s.physical.fragment_response=1;
    if (cycle==13) {
      s.lanes[2].pc=0xa000;
      s.boundary.trap=1; s.boundary.cause=15; s.boundary.epc=0xa000; s.boundary.tval=0x2000;
    }
    // A replay/idle edge cannot allocate a record, even with stale slot payloads.
    if (cycle==14) s.lanes[0].pc=0xdead;
    auto emitted=sample(c,binding,cycle,s,random);
    if (cycle==3 || cycle==6) require(emitted.empty());
    records.insert(records.end(),emitted.begin(),emitted.end());
  }
  require(records.size()==10);
  require(records[0].sample==records[1].sample);
  for (Word i=0;i<records.size();++i) require(records[i].id.order==i);
  require(std::get<RegisterWrite>(records[2].effects.at({0,0})).value==33);
  require(std::get<RegisterWrite>(records[4].effects.at({0,0})).value==44);
  const auto& load=std::get<MemoryEffect>(records[2].effects.at({1,0}));
  require(load.physical_address==0x9000 && load.read_data==33);
  require(std::get<Trap>(records[7].outcome).epc==0x801c);
  require(std::holds_alternative<Interrupt>(records[8].event));
  const auto& prefix=std::get<MemoryEffect>(records[9].effects.at({1,0}));
  const auto& fault=std::get<MemoryEffect>(records[9].effects.at({1,1}));
  require(prefix.physical_address==0x4ffd && prefix.byte_mask==7 && prefix.write_data==0x332211);
  require(fault.virtual_address==0x2000 && fault.fragment_offset==3 && fault.byte_mask==31 && fault.result==AccessResult::Fault);
  c.finish();
  std::ostringstream out;
  for (const auto& record:records) out<<record.id.order<<':'<<record.effects.size()<<':'<<record.sample<<';';
  return out.str();
}
void epoch_and_drain() {
  Collector c; c.reset(0,0,{0,0x8000,{3,false},64,0}); DpiBinding binding(c); std::mt19937 random(0);
  Sample s; auto& l=s.retire(0,0x8000,7); l.deferred=1; l.service=2;
  require(sample(c,binding,0,s,random).empty());
  c.reset(0,1,{0,0x8000,{3,false},64,0});
  s=Sample(); auto& store=s.retire(0,0x8000); store.memory=1; store.deferred=1; store.access=2; store.width=0; store.address=0x1000; store.store_data=0xab;
  s.physical.request_valid=1; s.physical.request_address=0x9000;
  require(sample(c,binding,1,s,random).empty()); require(!c.drain());
  s=Sample(); s.completions[0]={0,1,0x8000,0,0,0}; sample(c,binding,2,s,random);
  for (Word cycle=3;cycle<5;++cycle) sample(c,binding,cycle,Sample(),random);
  s=Sample(); s.completions[3]={3,1,0x8000,0,0,0};
  auto returned=sample(c,binding,5,s,random);
  require(returned.size()==1 && returned[0].id.epoch==1 && returned[0].id.order==0);
  require(std::get<MemoryEffect>(returned[0].effects.at({1,0})).write_data==0xab);
  require(c.drain()); c.finish();
}
void fp_owners(unsigned seed) {
  Collector c; c.reset(0,0,{0,0x8000,{3,false},64,0}); DpiBinding binding(c); std::mt19937 random(seed);
  std::vector<Record> records;
  for (Word cycle=0;cycle<7;++cycle) {
    Sample s;
    if (cycle==0) {
      auto& l=s.retire(0,0x8000,0); l.fp=1; l.fp_destination=2; l.fp_delay=3; l.deferred=1;
      s.retire(1,0x8004,1,7);
    }
    if (cycle==1) {
      auto& l=s.retire(0,0x8008,2); l.fp=1; l.fp_destination=1; l.fp_delay=0;
      s.fp[0]={0,1,0x8008,2,1,8,1,1};
    }
    if (cycle==2) {
      auto& l=s.retire(0,0x800c,3); l.fp=2; l.fp_destination=2; l.write=0;
      l.deferred=1; l.memory=1; l.access=1; l.width=3; l.address=0x1000;
      s.physical.request_valid=1; s.physical.request_address=0x9000;
    }
    if (cycle==3) {
      s.fp[0]={0,1,0x8000,0,2,0x3ff0000000000000,1,0};
      s.fp[1]={1,1,0x800c,3,2,0x4000000000000000,0,0};
      auto& l=s.retire(0,0x8010,4); l.fp=1; l.fp_destination=2; l.fp_delay=UINT64_MAX; l.deferred=1; l.write=0;
      auto& late=s.retire(1,0x8012,9); late.fp=1; late.fp_destination=1; late.fp_delay=2; late.deferred=1;
      s.physical.pipeline_valid=1; s.physical.pipeline_address=0xa000;
    }
    if (cycle==5) { s.fp[0]={0,1,0x8012,9,1,19,0,0}; s.completions[3]={3,1,0x8012,9,1,19}; }
    if (cycle==4) {
      auto& l=s.retire(0,0x8014,5); l.fp=2; l.fp_destination=2; l.write=0;
      l.memory=1; l.access=1; l.width=3; l.address=0x2000; l.data=0x4008000000000000;
      s.fp[1]={1,1,0x8014,5,2,l.data,0,0};
    }
    if (cycle==6) s.fp[0]={0,1,0x8010,4,2,0x4010000000000000,1,16};
    auto emitted=sample(c,binding,cycle,s,random);
    records.insert(records.end(),emitted.begin(),emitted.end());
  }
  require(records.size()==7);
  for (Word i=0;i<records.size();++i) require(records[i].id.order==i);
  const auto& f0=std::get<RegisterWrite>(records[0].effects.at({0,0}));
  require(f0.bank==Bank::FloatingPoint && f0.index==0 && f0.value==0x3ff0000000000000);
  require(std::get<RegisterWrite>(records[2].effects.at({0,0})).bank==Bank::Integer);
  require(std::get<MemoryEffect>(records[3].effects.at({1,0})).physical_address==0x9000);
  require(std::get<RegisterWrite>(records[5].effects.at({0,0})).value==19);
  require(std::get<MemoryEffect>(records[6].effects.at({1,0})).physical_address==0xa000);
  c.finish();
}
void reject_bad_fixed_return(bool early) {
  Collector c; c.reset(0,0,{0,0x8000,{3,false},64,0}); DpiBinding binding(c); std::mt19937 random(0);
  Sample admitted; auto& l=admitted.retire(0,0x8000,7); l.deferred=1; l.service=1;
  sample(c,binding,0,admitted,random);
  bool rejected=false;
  try {
    for (Word cycle=1;cycle<=3;++cycle) {
      Sample s;
      if (early && cycle==2) { s.completions[1]={1,1,0x8000,7,1,42}; s.completions[3]={3,1,0x8000,7,1,42}; }
      sample(c,binding,cycle,s,random);
    }
  } catch (const std::runtime_error& error) {
    rejected=std::string(error.what()).find(early ? "fixed RF write cycle" : "missing scheduled multiply")!=std::string::npos;
  }
  require(rejected);
}
void block_zero(bool fault) {
  Collector c; c.reset(0,0,{0,0x8000,{3,false},64,0}); DpiBinding binding(c); std::mt19937 random(0);
  std::vector<Record> records;
  for (Word cycle=0;cycle<5;++cycle) {
    Sample s;
    if (cycle==0) {
      auto& l=fault ? s.lanes[2] : s.retire(0,0x8000);
      l.pc=0x8000; l.encoding=0x0040a00f; l.memory=1; l.access=6; l.width=3; l.address=0x103f;
      if (fault) { s.boundary.trap=1; s.boundary.cause=15; s.boundary.epc=l.pc; s.boundary.tval=l.address; }
      else { l.deferred=1; s.physical.request_valid=1; s.physical.request_address=0x903f; }
    }
    if (!fault && cycle==1) s.completions[0]={0,1,0x8000,0,0,0};
    if (!fault && cycle==4) s.completions[3]={3,1,0x8000,0,0,0};
    auto emitted=sample(c,binding,cycle,s,random);
    records.insert(records.end(),emitted.begin(),emitted.end());
  }
  require(records.size()==1 && records[0].effects.size()==(fault ? 1 : 8));
  for (Word i=0;i<(fault ? 1 : 8);++i) {
    const auto& effect=std::get<MemoryEffect>(records[0].effects.at({1,i}));
    require(effect.kind==AccessKind::Store && effect.byte_mask==255 && !effect.read_valid && effect.write_valid==!fault && effect.write_data==0);
    require(effect.virtual_address==(fault ? 0x103f : 0x1000+8*i));
    if (!fault) require(effect.physical_address==0x9000+8*i);
    require(effect.result==(fault ? AccessResult::Fault : AccessResult::Success));
  }
  c.finish();
}
int main() {
  block_zero(false); block_zero(true);
  const auto expected=run(0);
  for (unsigned seed=1;seed<100;++seed) require(run(seed)==expected);
  epoch_and_drain();
  for (unsigned seed=0;seed<100;++seed) fp_owners(seed);
  reject_bad_fixed_return(true); reject_bad_fixed_return(false);
  std::cout<<"RV2Wide adapter ownership checks passed\n";
}
