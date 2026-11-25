// SPDX-License-Identifier: Apache-2.0
#include "../../rv5stage/adapter.h"
#include "../sail/vector-records.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
using namespace rhodium::cosim::observation;
using namespace rhodium::cosim;
using namespace rhodium::cosim::rv5stage;
void test_rv5stage_vector_cycle(std::int64_t instance, std::int64_t epoch, std::int64_t xlen, std::int64_t vlen, std::int64_t slots, std::int64_t pipeline_valid, std::int64_t pipeline_address, std::int64_t split, std::int64_t has_fragments, std::int64_t replay) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), Word(epoch), Cycle{Word(xlen), Word(vlen), Word(slots), Word(slots), 2, Word(pipeline_valid), Word(pipeline_address), Word(split), Word(has_fragments), Word(replay)});
  });
}
void test_rv5stage_vector_allocate(std::int64_t instance, std::int64_t epoch, std::int64_t order, std::int64_t allocated, std::int64_t admitted, std::int64_t instruction) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), Word(epoch), Allocate{Word(order), Word(allocated), Word(admitted), Word(instruction)});
  });
}
void test_rv5stage_vector_dispatch(std::int64_t instance, std::int64_t epoch, std::int64_t owner, std::int64_t base, std::int64_t eew, std::int64_t instruction, std::int64_t vtype) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), Word(epoch), Dispatch{Word(owner), Word(base), Word(eew), Word(instruction), Word(vtype)});
  });
}
void test_rv5stage_vector_issue(std::int64_t instance, std::int64_t epoch, std::int64_t owner, std::int64_t tag, std::int64_t first, std::int64_t destination, std::int64_t packed, std::int64_t enabled, std::int64_t store, std::int64_t address, std::int64_t physical, std::int64_t precertified, std::int64_t mask, std::int64_t data) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), Word(epoch), Issue{Word(owner), Word(tag), Word(first), Word(destination), Word(packed), Word(enabled), Word(store), Word(address), Word(address), Word(physical), Word(precertified), Word(mask), Word(data)});
  });
}
void test_rv5stage_vector_write(std::int64_t instance, std::int64_t epoch, std::int64_t address, std::int64_t mask, std::int64_t data, std::int64_t local, std::int64_t packed, std::int64_t splat, std::int64_t local_owner, std::int64_t packed_owner, std::int64_t splat_owner) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), Word(epoch), Write{Word(address), Word(mask), Word(data), Word(local), Word(packed), Word(splat), Word(local_owner), Word(packed_owner), Word(splat_owner)});
  });
}
void test_rv5stage_vector_direct(std::int64_t instance, std::int64_t epoch, std::int64_t tag, std::int64_t data) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), Word(epoch), Direct{Word(tag), Word(data)});
  });
}
void test_rv5stage_vector_complete(std::int64_t instance, std::int64_t epoch, std::int64_t tag, std::int64_t data) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), Word(epoch), Complete{Word(tag), Word(data)});
  });
}
void test_rv5stage_vector_store_complete(std::int64_t instance, std::int64_t epoch, std::int64_t tag) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), Word(epoch), StoreComplete{Word(tag)});
  });
}
void test_rv5stage_vector_translation(std::int64_t instance, std::int64_t epoch, std::int64_t tag, std::int64_t physical) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), Word(epoch), Translation{Word(tag), Word(physical)});
  });
}
void test_rv5stage_vector_fragment_start(std::int64_t instance, std::int64_t epoch, std::int64_t physical) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), Word(epoch), FragmentStart{Word(physical)});
  });
}
void test_rv5stage_vector_fragment(std::int64_t instance, std::int64_t epoch, std::int64_t tag, std::int64_t address, std::int64_t mask, std::int64_t data, std::int64_t store_data) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), Word(epoch), Fragment{Word(tag), Word(address), Word(mask), Word(data), Word(store_data)});
  });
}
void test_rv5stage_vector_fault(std::int64_t instance, std::int64_t epoch, std::int64_t tag, std::int64_t address) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), Word(epoch), Fault{Word(tag), Word(address)});
  });
}
void test_rv5stage_vector_decision(std::int64_t instance, std::int64_t epoch, std::int64_t tag, std::int64_t disposition) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), Word(epoch), Decision{Word(tag), Word(disposition)});
  });
}
void test_rv5stage_vector_drain(std::int64_t instance, std::int64_t epoch, std::int64_t owner) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), Word(epoch), Drain{Word(owner)});
  });
}

// Model simulator bindings that remain alive until process shutdown.
std::unique_ptr<Collector> shutdown_collector;
std::unique_ptr<DpiBinding> shutdown_binding;
void require(bool condition) { if (!condition) throw std::runtime_error("vector adapter test failed"); }
constexpr Word load = (1U<<25)|(6<<15)|(6<<12)|(8<<7)|7;
constexpr Word segmented = (2U<<29)|load;
ResetState reset_state() { return {0,0x8000,{3,false},64,128}; }
Instruction instruction(Word pc, Word code=load) { return {pc,code,4,4,{3,false},32}; }
std::string run(unsigned seed) {
  Collector collector;
  collector.reset(0,0,reset_state()); collector.reset(1,0,reset_state());
  DpiBinding binding(collector);
  std::mt19937 random(seed);
  std::ostringstream stream;
  Word cycle=0, epoch=0;
  auto frame = [&](std::vector<std::function<void()>> calls, bool translated=false, Word physical=0) {
    collector.begin_sample(cycle++);
    calls.push_back([&]{test_rv5stage_vector_cycle(0,epoch,64,128,2,translated,physical,0,1,0);});
    std::shuffle(calls.begin(),calls.end(),random);
    for(auto& call:calls) call();
    binding.check(); binding.check(); // Idempotent settled checks cannot duplicate effects.
    auto records=collector.end_sample();
    for(const auto& record:records) {
      if(record.instance==0 && record.id.epoch==0 && record.id.order==0) {
        require(record.effects.size()==5);
        const auto& write=std::get<RegisterWrite>(record.effects.at({5,0}));
        require(write.index==9 && write.value==0x44332211);
        for(unsigned byte=0;byte<4;++byte) {
          const auto& memory=std::get<MemoryEffect>(record.effects.at({5,1+byte}));
          require(memory.access_id==1 && memory.fragment_offset==byte && memory.physical_address==0x2004+byte);
        }
      }
      if(record.instance==0 && record.id.epoch==0 && record.id.order==1) {
        require(record.effects.size()==4);
        const auto& memory=std::get<MemoryEffect>(record.effects.at({5,0}));
        require(memory.access_id==4 && memory.physical_address==0x2010 && memory.read_data==0x88);
      }
      vector_test::save(stream,record);
    }
  };
  frame({
    [&]{collector.instruction(0,{0,0},instruction(0x8000,segmented));},
    []{test_rv5stage_vector_dispatch(0,0,0,0x1000,2,segmented,16);},
    []{test_rv5stage_vector_allocate(0,0,0,1,1,segmented);},
    [&]{collector.instruction(1,{0,0},instruction(0x8000)); collector.retire(1,{0,0},{0x8004,{3,false}});},
    []{test_rv5stage_vector_cycle(1,0,64,128,2,0,0,0,0,0);},
    []{test_rv5stage_vector_allocate(1,0,0,1,1,load);},
    []{test_rv5stage_vector_dispatch(1,0,0,0x3000,2,load,16);}
  });
  frame({
    []{test_rv5stage_vector_issue(0,0,0,0,0,9,0,1,0,0x1004,0,0,0xf0,0);},
    []{test_rv5stage_vector_cycle(1,0,64,128,2,0,0,0,0,0);},
    []{test_rv5stage_vector_drain(1,0,0);}
  });
  frame({[]{test_rv5stage_vector_decision(0,0,0,0);}, []{test_rv5stage_vector_translation(0,0,0,0x2004);}});
  // A younger replay must not cancel an accepted, delayed older response.
  collector.begin_sample(cycle++);
  test_rv5stage_vector_cycle(0,0,64,128,2,0,0,0,1,1);
  binding.check(); require(collector.end_sample().empty());
  frame({
    [&]{collector.retire(0,{0,0},{0x8004,{3,false}}); collector.instruction(0,{0,1},instruction(0x8004));},
    []{test_rv5stage_vector_write(0,0,18,UINT32_MAX,0x44332211,1,0,0,0,0,0);},
    []{test_rv5stage_vector_complete(0,0,0,0x44332211);},
    []{test_rv5stage_vector_drain(0,0,0);},
    []{test_rv5stage_vector_allocate(0,0,1,1,1,load);},
    []{test_rv5stage_vector_dispatch(0,0,1,0x1000,2,load,16);},
    []{test_rv5stage_vector_issue(0,0,1,0,2,8,0,1,0,0x1008,0,0,15,0);}
  });
  collector.begin_sample(cycle++);
  test_rv5stage_vector_cycle(0,0,64,128,2,0,0,0,1,1);
  test_rv5stage_vector_decision(0,0,0,1);
  binding.check(); require(collector.end_sample().empty());
  frame({[]{test_rv5stage_vector_issue(0,0,1,0,4,8,0,1,0,0x1010,0,0,15,0);}},true,0x2010);
  frame({
    []{test_rv5stage_vector_direct(0,0,0,0x88);},
    []{test_rv5stage_vector_decision(0,0,0,0);},
    []{test_rv5stage_vector_drain(0,0,1);},
    [&]{collector.retire(0,{0,1},{0x8008,{3,false}});}
  });
  collector.reset(0,1,reset_state()); epoch=1;
  frame({
    [&]{collector.instruction(0,{1,0},instruction(0x8000));},
    []{test_rv5stage_vector_allocate(0,1,0,1,1,load);},
    []{test_rv5stage_vector_dispatch(0,1,0,0x1000,2,load,16);}
  });
  frame({[]{test_rv5stage_vector_drain(0,1,0);}, [&]{collector.retire(0,{1,0},{0x8004,{3,false}});}});
  collector.finish();
  return stream.str();
}
void invalid(unsigned mutation) {
  Collector collector; collector.reset(0,0,reset_state()); DpiBinding binding(collector);
  collector.begin_sample(0);
  collector.instruction(0,{0,0},instruction(0x8000));
  test_rv5stage_vector_cycle(0,0,64,128,2,0,0,0,1,0);
  test_rv5stage_vector_allocate(0,0,0,1,1,load);
  test_rv5stage_vector_dispatch(0,0,0,0x1000,2,load,16);
  test_rv5stage_vector_issue(0,0,0,0,0,8,0,1,0,0x1000,0,0,15,0);
  binding.check(); collector.end_sample();
  collector.begin_sample(1);
  test_rv5stage_vector_cycle(0,mutation==4?1:0,64,128,2,0,0,0,1,0);
  if(mutation==0) test_rv5stage_vector_issue(0,0,0,0,1,8,0,1,0,0x1004,0,0,0xf0,0);
  if(mutation==1) { test_rv5stage_vector_decision(0,0,0,0); test_rv5stage_vector_drain(0,0,0); }
  if(mutation==2) { test_rv5stage_vector_complete(0,0,0,0); test_rv5stage_vector_complete(0,0,0,0); }
  if(mutation==3) test_rv5stage_vector_complete(0,0,1,0);
  bool failed=false;
  try { binding.check(); } catch(const std::exception&) { failed=true; }
  require(failed);
  failed=false;
  try { binding.check(); } catch(const std::exception&) { failed=true; }
  require(failed); // Adapter failures poison the generic DPI session too.
}
// Exercise the settled service lifecycle independently of simulator callback
// order, including old result/new tag reuse and queued work surviving replay.
std::string services(unsigned seed, unsigned mutation=0) {
  Collector collector; collector.reset(0,0,reset_state()); DpiBinding binding(collector);
  std::mt19937 random(seed); std::ostringstream output; Word cycle=0,epoch=0;
  auto service=[](Service event) {
    observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& a, Collector& c) { a.vector.capture(c.sample(),0,c.epoch(0),event); });
  };
  auto frame=[&](std::vector<std::function<void()>> calls, bool replay=false) {
    collector.begin_sample(cycle++);
    calls.push_back([&]{test_rv5stage_vector_cycle(0,epoch,64,128,2,0,0,0,0,replay);});
    std::shuffle(calls.begin(),calls.end(),random);
    for(auto& call:calls) call();
    binding.check();
    for(const auto& r:collector.end_sample()) vector_test::save(output,r);
  };
  auto allocate=[&](Word order,Word owner) {
    const Word code=(37U<<26)|(1U<<25)|(8<<20)|(12<<15)|(2<<12)|(16<<7)|0x57;
    collector.instruction(0,{epoch,order},instruction(0x8000+4*order,code));
    collector.retire(0,{epoch,order},{0x8004+4*order,{3,false}});
    test_rv5stage_vector_allocate(0,epoch,order,1,1,code);
    test_rv5stage_vector_dispatch(0,epoch,owner,0,0,code,0);
  };
  frame({[&]{allocate(0,0);},[&]{service({1,0,0,0,0,0,0,0,0,0,0,0});}});
  frame({[&]{allocate(1,1);},[&]{service({1,1,1,1,mutation==1?0U:1U,0,1,1,0,0,0,0});}},true);
  if(mutation==2) frame({[&]{service({1,0,0,0,0,0,0,0,0,0,0,0});}});
  if(mutation==3) frame({[]{test_rv5stage_vector_drain(0,0,0);}});
  if(mutation==8) frame({[&]{service({0,0,0,0,1,0,0,0,0,0,0,0});}});
  // Younger division completes first; publication must still wait for multiply.
  frame({[&]{service({0,0,0,0,0,0,0,0,0,0,1,mutation==9?2U:1U});},
         []{test_rv5stage_vector_write(0,0,34,255,9,1,0,0,1,0,0);},
         []{test_rv5stage_vector_drain(0,0,1);}});
  frame({
    // A packed beat allocates and launches atomically while an old scalar
    // product returns on the same tag.
    [&]{service({1,0,0,0,0,0,0,0,mutation==4?0U:1U,0,mutation==4?1U:0U,0,1});},
    [&]{if(mutation!=5) test_rv5stage_vector_write(0,0,32,255,7,1,0,0,mutation==6?1:0,0,0);},
    []{test_rv5stage_vector_drain(0,0,0);},
    [&]{allocate(2,0);}
  });
  frame({[&]{service({0,0,0,0,0,0,0,0,1,0,0,0});},
         []{test_rv5stage_vector_write(0,0,36,255,11,1,0,0,0,0,0);},
         []{test_rv5stage_vector_drain(0,0,0);}});
  if(mutation==7) frame({[&]{service({0,0,0,0,0,0,0,0,1,0,0,0});}});
  // Reset abandons a launched service without letting its slot generation
  // contaminate an identically tagged instruction in the next epoch.
  frame({[&]{allocate(3,0);},[&]{service({1,0,0,0,1,0,0,0,0,0,0,0});}});
  collector.reset(0,++epoch,reset_state());
  frame({[&]{allocate(0,0);},[&]{service({1,0,0,0,1,0,0,0,0,0,0,0});}});
  frame({[&]{service({0,0,0,0,0,0,0,0,1,0,0,0});},
         [&]{test_rv5stage_vector_write(0,epoch,32,255,13,1,0,0,0,0,0);},
         [&]{test_rv5stage_vector_drain(0,epoch,0);}});
  collector.finish();
  return output.str();
}
std::string fp_services(unsigned seed,unsigned mutation=0,bool reduction=false) {
  Collector collector; collector.reset(0,0,reset_state()); DpiBinding binding(collector);
  std::mt19937 random(seed); std::ostringstream output; Word cycle=0,epoch=0;
  const Word code=(reduction?3U<<26:0)|(1U<<25)|(8<<20)|(12<<15)|(1<<12)|(16<<7)|0x57;
  auto frame=[&](FpService fp,std::vector<std::function<void()>> calls) {
    collector.begin_sample(cycle++);
    calls.push_back([]{rhodium_vector_cycle(0,64,128,2,2,2,0,0,0,0,1);});
    calls.push_back([&]{rhodium_vector_fp(0,fp.allocated,fp.owner,fp.tag,fp.launch,fp.launch_tag,fp.returned,fp.return_tag,fp.flags_valid,fp.flags);});
    std::shuffle(calls.begin(),calls.end(),random);
    for(auto& call:calls) call();
    binding.check();
    for(const auto& r:collector.end_sample()) {
      const auto& flags=std::get<CsrUpdate>(r.effects.rbegin()->second);
      require(flags.address==1 && flags.operation==CsrOperation::SetBits && flags.value==(r.id.epoch?4U:r.id.order==0?17U:8U));
      if(reduction) require(r.effects.size()==2); // Only final VRF write plus accumulated flags.
      vector_test::save(output,r);
    }
  };
  auto allocate=[&](Word order,Word owner) {
    collector.instruction(0,{epoch,order},instruction(0x8000+4*order,code));
    collector.retire(0,{epoch,order},{0x8004+4*order,{3,false}});
    test_rv5stage_vector_allocate(0,epoch,order,1,1,code);
    rhodium_vector_dispatch(0,owner,0,0,code,16);
  };
  frame({1,0,0,0,0,0,0,0,0},{[&]{allocate(0,0);}});
  frame({1,1,1,mutation==1?0U:1U,0,0,0,0,0},{[&]{allocate(1,1);}});
  frame({0,0,0,1,1,0,0,0,0},{});
  if(mutation==2) frame({1,0,0,0,0,0,0,0,0},{});
  if(mutation==3) frame({},{[]{rhodium_vector_drain(0,0);}});
  if(mutation==4) frame({0,0,0,1,0,0,0,0,0},{});
  // Younger service returns first; older flags are ORed across two results.
  frame({0,0,0,0,0,1,1,1,8},{
    []{rhodium_vector_write(0,48,UINT64_MAX,9,1,0,0,1,0,0);},
    []{rhodium_vector_drain(0,1);}});
  frame({1,0,0,1,0,1,mutation==5?1U:0U,1,16},{
    [&]{if(!reduction && mutation!=8) rhodium_vector_write(0,32,UINT32_MAX,7,1,0,0,mutation==9?1:0,0,0);}});
  frame({0,0,0,0,0,1,0,1,1},{
    [&]{rhodium_vector_write(0,32,reduction?UINT32_MAX:UINT64_MAX-UINT32_MAX,reduction?11:11ULL<<32,1,0,0,0,0,0);},
    []{rhodium_vector_drain(0,0);}});
  if(mutation==10) frame({0,0,0,0,0,1,0,1,1},{});
  frame({1,0,0,1,0,0,0,0,0},{[&]{allocate(2,0);}});
  // Reset drops an in-flight result and previously accumulated macro flags.
  frame({1,0,0,1,0,1,0,1,16},{[&]{if(!reduction) rhodium_vector_write(0,32,UINT32_MAX,1,1,0,0,0,0,0);}});
  collector.reset(0,++epoch,reset_state());
  frame({1,0,0,1,0,0,0,0,0},{[&]{allocate(0,0);}});
  frame({0,0,0,0,0,1,0,1,4},{
    []{rhodium_vector_write(0,32,UINT32_MAX,2,1,0,0,0,0,0);},
    []{rhodium_vector_drain(0,0);}});
  collector.finish(); return output.str();
}
void address_domains(Word raw_base, Word effective_base, bool packed, bool fault,
                     bool store, bool corrupt_fault = false) {
  Collector collector; collector.reset(0,0,reset_state()); DpiBinding binding(collector);
  Word cycle=0;
  auto begin = [&] {
    collector.begin_sample(cycle++);
    test_rv5stage_vector_cycle(0,0,64,128,2,0,0,fault,1,0);
  };
  auto end = [&] { binding.check(); return collector.end_sample(); };
  const Word code = store ? (load & ~Word{127})|0x27 : load;
  begin();
  collector.instruction(0,{0,0},instruction(0x8000,code));
  test_rv5stage_vector_allocate(0,0,0,1,1,code);
  test_rv5stage_vector_dispatch(0,0,0,raw_base,2,code,16);
  end();
  const Word offset = fault ? 7 : 8;
  begin();
  rhodium_vector_issue(0,0,0,2,8,packed,1,store,raw_base+offset,
                              effective_base+offset,0x2000+offset,!fault,packed ? 255 : 15,0x8877665544332211);
  test_rv5stage_vector_decision(0,0,0,0);
  if (fault) test_rv5stage_vector_fragment_start(0,0,0x2007);
  end();
  if (fault) {
    begin();
    test_rv5stage_vector_fragment(0,0,0,effective_base+7,1,0x11,0x11);
    end();
  }
  begin();
  if (fault) {
    test_rv5stage_vector_fault(0,0,0,effective_base+(corrupt_fault ? 11 : 8));
    collector.exception(0,{0,0},{store ? 7U : 5U,0x8000,effective_base+8,0,{3,false},false,0,0});
  } else {
    test_rv5stage_vector_complete(0,0,0,0x8877665544332211);
    collector.retire(0,{0,0},{0x8004,{3,false}});
  }
  test_rv5stage_vector_drain(0,0,0);
  const auto records = end();
  require(records.size()==1 && records.front().effects.size()==(fault ? 2U : packed ? 8U : 4U));
  unsigned byte=0;
  for (const auto& [id,effect] : records.front().effects) {
    (void)id;
    const auto& memory = std::get<MemoryEffect>(effect);
    require(memory.virtual_address==effective_base+offset+byte);
    require(memory.access_id==(packed ? (offset+byte)/4 : 2));
    require(memory.fragment_offset==(packed ? (offset+byte)%4 : byte));
    require(memory.result==(fault && byte ? AccessResult::Fault : AccessResult::Success));
    if (memory.result==AccessResult::Success) require(memory.physical_address==0x2000+offset+byte);
    ++byte;
  }
  collector.finish();
}

// Independent bookkeeping must survive a simultaneous load return and shuffled callbacks.
std::string store_and_load_completion(unsigned seed) {
  Collector collector; collector.reset(0,0,reset_state()); DpiBinding binding(collector);
  std::mt19937 random(seed); std::ostringstream output; Word cycle=0;
  auto frame=[&](std::vector<std::function<void()>> calls) {
    collector.begin_sample(cycle++);
    calls.push_back([]{test_rv5stage_vector_cycle(0,0,64,128,2,0,0,0,0,0);});
    std::shuffle(calls.begin(),calls.end(),random);
    for(auto& call:calls) call();
    binding.check();
    for(const auto& record:collector.end_sample()) vector_test::save(output,record);
  };
  for(Word owner=0;owner<2;++owner) {
    const Word code=owner ? load : (load & ~Word{127}) | 39;
    frame({[&,owner,code]{
      collector.instruction(0,{0,owner},instruction(0x8000+4*owner,code));
      collector.retire(0,{0,owner},{0x8004+4*owner,{3,false}});
      test_rv5stage_vector_allocate(0,0,owner,1,1,code);
      test_rv5stage_vector_dispatch(0,0,owner,0x1000+8*owner,2,code,16);
      test_rv5stage_vector_issue(0,0,owner,owner,0,8,0,1,!owner,0x1000+8*owner,0x2000+8*owner,1,15,0x11223344);
    }});
    frame({[owner]{test_rv5stage_vector_decision(0,0,owner,0);}});
  }
  frame({[]{test_rv5stage_vector_store_complete(0,0,0);},
         []{test_rv5stage_vector_complete(0,0,1,0x55667788);},
         []{test_rv5stage_vector_write(0,0,16,UINT32_MAX,0x55667788,1,0,0,1,0,0);},
         []{test_rv5stage_vector_drain(0,0,0);}});
  frame({[]{test_rv5stage_vector_drain(0,0,1);}});
  collector.finish();
  return output.str();
}

void native_row(unsigned xlen, bool store, bool execute = false, Word low_mask = UINT64_MAX, Word high_mask = UINT64_MAX, unsigned enabled = 0xffff, bool floating = false) {
  Collector collector; collector.reset(0,0,{0,0x8000,{3,false},xlen,256}); DpiBinding binding(collector);
  auto begin = [&](Word cycle) {
    collector.begin_sample(cycle);
    test_rv5stage_vector_cycle(0,0,xlen,256,2,0,0,0,0,0);
  };
  begin(0);
  const Word insn = floating ? 0x02209457 : execute ? 0x02208457 : store ? (load & ~Word{127}) | 0x27 : load;
  collector.instruction(0,{0,0},instruction(0x8000,insn));
  test_rv5stage_vector_allocate(0,0,0,1,1,insn);
  test_rv5stage_vector_dispatch(0,0,0,0x1000,2,insn,16);
  binding.check(); collector.end_sample();
  begin(1);
  constexpr Word lo = 0x7766554433221100, hi = 0xffeeddccbbaa9988;
  if (execute) {
    rhodium_vector_row_write(0,0,17,1,hi,high_mask,floating,17);
    rhodium_vector_row_write(0,0,17,0,lo,low_mask,floating,17);
  }
  else {
    rhodium_vector_row_transfer(0,0,store,0,0,16,0x1010,0xf010,1,hi,enabled >> 8);
    rhodium_vector_row_transfer(0,0,store,0,0,16,0x1010,0xf010,0,lo,enabled & 255);
    low_mask = high_mask = 0;
    for (unsigned i = 0; i < 16; ++i)
      if (enabled & (1u << i)) (i < 8 ? low_mask : high_mask) |= Word{255} << (8*(i%8));
    if (!store) {
      binding.check(); require(collector.end_sample().empty());
      begin(2);
      rhodium_vector_row_write(0,0,17,1,hi,high_mask,0,0);
      rhodium_vector_row_write(0,0,17,0,lo,low_mask,0,0);
    }
  }
  collector.retire(0,{0,0},{0x8004,{3,false}});
  test_rv5stage_vector_drain(0,0,0);
  binding.check(); const auto records = collector.end_sample();
  const unsigned expected_writes = store ? 0 : unsigned(low_mask != 0) + unsigned(high_mask != 0);
  require(records.size() == 1 && records.front().effects.size() == (execute ? 0 : __builtin_popcount(enabled)) + expected_writes + unsigned(floating));
  unsigned bytes = 0, writes = 0;
  for (const auto& [id, effect] : records.front().effects) {
    if (const auto* w = std::get_if<RegisterWrite>(&effect)) {
      const bool upper = writes != 0 || !low_mask;
      require(w->index == 8 && w->bit_offset == 128 + 64*upper && w->mask == (upper ? high_mask : low_mask));
      require(w->value == (upper ? hi : lo));
      ++writes;
    } else if (const auto* csr = std::get_if<CsrUpdate>(&effect)) {
      require(floating && csr->address == 1 && csr->operation == CsrOperation::SetBits && csr->value == 17);
    } else {
      const auto& m = std::get<MemoryEffect>(effect);
      while (bytes < 16 && !(enabled & (1u << bytes))) ++bytes;
      require(bytes < 16 && m.access_id == 4 + bytes/4 && m.fragment_offset == bytes%4);
      require(m.virtual_address == 0x1010 + bytes && m.physical_address == 0xf010 + bytes);
      require(m.kind == (store ? AccessKind::Store : AccessKind::Load));
      require(m.write_valid == store && m.read_valid != store);
      require((store ? m.write_data : m.read_data) == 17*bytes++);
    }
  }
  require((execute || (enabled >> bytes) == 0) && writes == expected_writes);
  collector.finish();
}

void simultaneous_native_rows(unsigned xlen, unsigned vlen, bool store_first_callback) {
  const Word dlen=vlen/2, chunk=(dlen-1)/64;
  Collector collector; collector.reset(0,0,{0,0x8000,{3,false},xlen,vlen}); DpiBinding binding(collector);
  auto sample = [&](Word cycle) {
    collector.begin_sample(cycle);
    test_rv5stage_vector_cycle(0,0,xlen,vlen,2,0,0,0,0,0);
  };
  for (Word owner=0;owner<2;++owner) {
    sample(owner);
    const Word insn=owner ? (load & ~Word{127}) | 0x27 : load;
    collector.instruction(0,{0,owner},instruction(0x8000+4*owner,insn));
    collector.retire(0,{0,owner},{0x8004+4*owner,{3,false}});
    test_rv5stage_vector_allocate(0,0,owner,1,1,insn);
    test_rv5stage_vector_dispatch(0,0,owner,0x1000+16*owner,2,insn,16);
    binding.check(); require(collector.end_sample().empty());
  }
  sample(2);
  for (bool store : {store_first_callback,!store_first_callback})
    rhodium_vector_row_transfer(0,store,store,0,0,0,0x1000+16*store,0x4000+16*store,chunk,0x20+store,1);
  rhodium_vector_row_write(0,0,17,chunk,0x20,255,0,0);
  for (unsigned owner=0;owner<2;++owner) {
    if (owner) sample(3);
    test_rv5stage_vector_drain(0,0,owner);
    binding.check(); const auto records=collector.end_sample();
    require(records.size()==1 && records[0].effects.size()==(owner ? 1 : 2));
    bool found=false;
    for (const auto& [id,effect] : records[0].effects) {
      if (const auto* m=std::get_if<MemoryEffect>(&effect)) {
        found=true;
        require(m->physical_address==0x4000+16*owner+8*chunk && m->kind==(owner ? AccessKind::Store : AccessKind::Load));
        require((owner ? m->write_data : m->read_data)==0x20+owner);
      } else {
        const auto& w=std::get<RegisterWrite>(effect);
        require(w.index==8 && w.bit_offset==dlen+chunk*64 && w.mask==255);
      }
    }
    require(found);
  }
  collector.finish();
}

void displaced_native_transfer(unsigned xlen, bool store) {
  const unsigned vlen=4*xlen, beat_bytes=vlen/16, base=0x1ffc;
  Collector collector; collector.reset(0,0,{0,0x8000,{3,false},xlen,vlen}); DpiBinding binding(collector);
  auto sample=[&](unsigned cycle) {
    collector.begin_sample(cycle);
    test_rv5stage_vector_cycle(0,0,xlen,vlen,2,0,0,0,0,0);
  };
  sample(0);
  const Word insn=store ? (load & ~Word{127}) | 0x27 : load;
  collector.instruction(0,{0,0},instruction(0x8000,insn));
  collector.retire(0,{0,0},{0x8004,{3,false}});
  test_rv5stage_vector_allocate(0,0,0,1,1,insn);
  test_rv5stage_vector_dispatch(0,0,0,base,2,insn,16);
  binding.check(); require(collector.end_sample().empty());
  sample(1);
  rhodium_vector_row_transfer(0,0,store,0,0,0,0x2000-beat_bytes,0x61000-beat_bytes,(beat_bytes-1)/8,UINT64_C(0x0403020100000000),0xf0);
  binding.check(); require(collector.end_sample().empty());
  sample(2);
  rhodium_vector_row_transfer(0,0,store,0,0,beat_bytes,0x2000,0x90000,0,0x08070605,0x0f);
  binding.check(); require(collector.end_sample().empty());
  sample(3);
  if (!store) rhodium_vector_row_write(0,0,16,0,UINT64_C(0x0807060504030201),UINT64_MAX,0,0);
  test_rv5stage_vector_drain(0,0,0);
  binding.check(); const auto records=collector.end_sample();
  require(records.size()==1 && records[0].effects.size()==(store ? 8u : 9u));
  unsigned bytes=0;
  for (const auto& [id,effect] : records[0].effects) {
    if (const auto* m=std::get_if<MemoryEffect>(&effect)) {
      require(m->kind==(store ? AccessKind::Store : AccessKind::Load));
      require(m->access_id==bytes/4 && m->fragment_offset==bytes%4 && (store ? m->write_data : m->read_data)==bytes+1);
      require(m->virtual_address==base+bytes && m->physical_address==(bytes<4 ? 0x60ffcu+bytes : 0x90000u+bytes-4));
      ++bytes;
    } else {
      const auto& w=std::get<RegisterWrite>(effect);
      require(w.index==8 && !w.bit_offset && w.value==UINT64_C(0x0807060504030201) && w.mask==UINT64_MAX);
    }
  }
  require(bytes==8); collector.finish();
}

void elementwise_row_transfer(unsigned xlen,bool store,unsigned mop,unsigned eew,unsigned fields=1) {
  Collector collector; collector.reset(0,0,{0,0x8000,{3,false},xlen,4*xlen}); DpiBinding binding(collector);
  const Word base=0x1056, beat_bytes=xlen/4;
  const bool indexed=mop!=2;
  const Word size=1u<<eew;
  const Word insn=(Word(fields-1)<<29)|(mop<<26)|(1u<<20)|(6u<<15)|((indexed ? 0u : 5u)<<12)|(8u<<7)|(store ? 0x27 : 7);
  auto sample=[&](unsigned cycle) {
    collector.begin_sample(cycle); test_rv5stage_vector_cycle(0,0,xlen,4*xlen,2,0,0,0,0,0);
  };
  sample(0);
  collector.instruction(0,{0,0},instruction(0x8000,insn)); collector.retire(0,{0,0},{0x8004,{3,false}});
  test_rv5stage_vector_allocate(0,0,0,1,1,insn); test_rv5stage_vector_dispatch(0,0,0,base,eew,insn,eew<<3);
  binding.check(); require(collector.end_sample().empty());
  // Element 1 is masked. Indexed elements 0 and 2 share an address, not an access ID.
  unsigned cycle=0;
  auto address=[&](unsigned i) {return indexed ? (base/beat_bytes+3)*beat_bytes-1 : base-6*i;};
  for (unsigned i : {0u,2u}) {
    for(unsigned byte=0;byte<size;) {
      sample(++cycle);
      Word va=address(i)+byte, lane=va%beat_bytes;
      unsigned n=std::min(size-byte,beat_bytes-lane);
      Word payload=0;
      for(unsigned b=0;b<n;++b) payload|=Word(0x40+byte+b)<<(8*(lane%8+b));
      rhodium_vector_row_transfer(0,0,store,fields>1,2*(8+i),size*i+byte,va-lane,0xf000+va-lane,lane/8,payload,((1u<<n)-1)<<(lane%8));
      binding.check(); require(collector.end_sample().empty());
      byte+=n;
    }
  }
  sample(++cycle); test_rv5stage_vector_drain(0,0,0); binding.check(); const auto records=collector.end_sample();
  require(records.size()==1 && records[0].effects.size()==2*size);
  unsigned n=0;
  for (const auto& [_,effect]:records[0].effects) {
    const auto& m=std::get<MemoryEffect>(effect);
    const unsigned element=n<size ? 0 : 2, byte=n%size;
    require(m.access_id==element*fields+(fields>1?element:0) && m.fragment_offset==byte && m.virtual_address==address(element)+byte &&
            m.physical_address==0xf000+m.virtual_address && (store ? m.write_data : m.read_data)==0x40+byte);
    ++n;
  }
  collector.finish();
}

static void simultaneous_scalar_banks(bool reverse) {
  Collector collector; collector.reset(0,0,reset_state()); DpiBinding binding(collector);
  auto begin=[&](unsigned cycle) {
    collector.begin_sample(cycle); test_rv5stage_vector_cycle(0,0,64,128,2,0,0,0,0,0);
  };
  for(Word o=0;o<2;++o) {
    begin(o);
    const Word code=o ? 0x42001657 : 0x428822d7;
    collector.instruction(0,{0,o},instruction(0x8000+4*o,code));
    collector.retire(0,{0,o},{0x8004+4*o,{3,false}});
    test_rv5stage_vector_allocate(0,0,o,1,1,code);
    test_rv5stage_vector_dispatch(0,0,o,0,0,code,0);
    binding.check(); require(collector.end_sample().empty());
  }
  begin(2);
  for(Word i=0;i<2;++i) {
    const Word o=i^reverse;
    rhodium_vector_scalar_write(0,o,o,o?12:5,0x45+o);
  }
  binding.check(); require(collector.end_sample().empty());
  for(Word o=0;o<2;++o) {
    begin(3+o); test_rv5stage_vector_drain(0,0,o); binding.check();
    const auto records=collector.end_sample();
    require(records.size()==1 && records[0].effects.size()==1);
    const auto& w=std::get<RegisterWrite>(records[0].effects.begin()->second);
    require(w.bank==(o?Bank::FloatingPoint:Bank::Integer) && w.index==(o?12u:5u) && w.value==0x45+o);
  }
  collector.finish();
}

int main() {
  for(bool reverse:{false,true}) simultaneous_scalar_banks(reverse);
  for (unsigned xlen : {32u,64u}) for (bool store : {false,true}) {
    elementwise_row_transfer(xlen,store,2,1);
    for(unsigned mop:{0u,1u,2u,3u}) elementwise_row_transfer(xlen,store,mop,1,3);
    for(unsigned mop:{1u,3u}) for(unsigned e=0;e<=(xlen==32?2u:3u);++e) elementwise_row_transfer(xlen,store,mop,e);
  }
  for (unsigned xlen : {32u,64u}) for (bool store : {false,true}) displaced_native_transfer(xlen,store);
  for (unsigned xlen : {32u,64u}) for (unsigned vlen : {128u,256u,512u})
    for (bool store_first : {false,true}) simultaneous_native_rows(xlen,vlen,store_first);
  for (unsigned xlen : {32u, 64u}) for (bool store : {false,true})
    for (unsigned enabled : {0xffffu, 0xf0f0u, 0x0f00u}) native_row(xlen, store, false, 0, 0, enabled);
  for (unsigned xlen : {32u, 64u}) for (Word low : {Word{0}, Word{0xff00ff00}}) for (Word high : {Word{0}, Word{0xff000000000000ff}})
    native_row(xlen, false, true, low, high);
  for (unsigned xlen : {32u, 64u}) native_row(xlen, false, true, UINT64_MAX, 0, 0xffff, true);
  const auto simultaneous_baseline=store_and_load_completion(0);
  for(unsigned seed=1;seed<64;++seed) require(store_and_load_completion(seed)==simultaneous_baseline);
  for (const auto& [raw,effective] : {std::pair{Word{0x7f00000000001000},Word{0x1000}},
                                   std::pair{Word{0x1234000000001000},Word{0x1000}},
                                   std::pair{Word{0x1234fffffffff000},Word{0xfffffffffffff000}}}) {
    for (bool store : {false,true}) {
      address_domains(raw,effective,false,false,store);
      address_domains(raw,effective,true,false,store);
      address_domains(raw,effective,false,true,store);
      bool failed=false;
      try { address_domains(raw,effective,false,true,store,true); }
      catch (const std::runtime_error&) { failed=true; }
      require(failed);
    }
  }
  const auto baseline=run(0);
  for(unsigned seed=1;seed<64;++seed) require(run(seed)==baseline);
  for(unsigned mutation=0;mutation<5;++mutation) invalid(mutation);
  const auto service_baseline=services(0);
  for(unsigned seed=1;seed<64;++seed) require(services(seed)==service_baseline);
  for(unsigned mutation=1;mutation<=9;++mutation) {
    bool failed=false;
    try { services(mutation,mutation); } catch(const std::exception&) { failed=true; }
    require(failed);
  }
  const auto fp_baseline=fp_services(0);
  for(unsigned seed=1;seed<64;++seed) require(fp_services(seed)==fp_baseline);
  for(unsigned mutation : {1,2,3,4,5,8,9,10}) {
    bool failed=false;
    try { fp_services(mutation,mutation); } catch(const std::exception&) { failed=true; }
    require(failed);
  }
  const auto reduction_baseline=fp_services(0,0,true);
  for(unsigned seed=1;seed<64;++seed) require(fp_services(seed,0,true)==reduction_baseline);
  for(unsigned mutation : {1U,2U,3U,4U,5U,10U}) {
    bool failed=false;
    try { fp_services(mutation,mutation,true); } catch(const std::exception&) { failed=true; }
    require(failed);
  }
  shutdown_collector=std::make_unique<Collector>();
  shutdown_collector->reset(0,0,reset_state());
  shutdown_binding=std::make_unique<DpiBinding>(*shutdown_collector);
  shutdown_collector->begin_sample(0);
  test_rv5stage_vector_cycle(0,0,64,128,2,0,0,0,1,0);
  shutdown_binding->check(); shutdown_collector->end_sample(); shutdown_collector->finish();
  std::cout<<"Vector adapter: 64 memory/integer/FP/reduction callback permutations, overlap/replay/reuse, two harts, warm reset, 32 invalid streams passed\n";
}
