// Replays enqueue-cached fields through FIFO feedback, stale previews and reset-edge writes.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace rds;
static void check(bool x, const char *s) { if (!x) throw std::runtime_error(s); }
static Model fixture(unsigned depth, unsigned flags) {
  Model m;
  m.widths = {1,1,1,1,64};
  m.metadata = {{"format","rhodium-simulation-ir-v1"},{"objects",Json::array()},
    {"registers",Json::array()},{"memories",Json::array()},{"writes",Json::array()},
    {"reads",Json::array()},{"assertions",Json::array()},{"origins",Json::array()},
    {"inventory",Json::array()},
    {"ports",{{0,0,"reset"},{0,1,"push"},{0,2,"pop"},{0,3,"fresh"},{0,4,"input"}}}};
  auto emit = [&](unsigned code, unsigned width, std::vector<Id> args,
                  std::vector<uint64_t> imm = {}) {
    Id id = m.widths.size(); m.widths.push_back(width);
    m.ops.push_back({code,id,args,imm}); return id;
  };
  Id a = emit(29,96,{}, {0,3}), b = emit(29,96,{}, {1,3});
  Id input = emit(18,96,{4}), incoming = emit(15,96,{3,b,input},{1});
  m.metadata["objects"] = {{1,96,depth,flags,{0,1,incoming,2},"ring/a"},
                             {1,96,depth,flags,{0,1,a,2},"ring/b"}};
  for (Id q : {a,b}) {
    Id x = emit(17,16,{q},{8}), y = emit(17,16,{q},{24});
    Id c = emit(0,16,{}, {17}), d = emit(0,16,{}, {91});
    Id less = emit(13,1,{x,c}), more = emit(13,1,{d,y});
    Id sum = emit(6,16,{x,y}), mix = emit(5,16,{sum,c});
    Id bits = emit(3,16,{mix,d}), eq = emit(12,1,{bits,c});
    Id no = emit(2,1,{eq}), any = emit(4,1,{less,more});
    Id decoded = emit(25,16,{x},{3,13,0,65535,7,1,1,22,1,3,19});
    Id tag = emit(17,1,{decoded},{0});
    Id mixed = emit(5,1,{no,any});
    Id answer = emit(5,1,{mixed,tag});
    Id dynamic = emit(3,1,{answer,3});
    m.metadata["ports"].push_back({1,q,"payload"+std::to_string(q)});
    m.metadata["ports"].push_back({1,answer,"derived"+std::to_string(q)});
    m.metadata["ports"].push_back({1,dynamic,"dynamic"+std::to_string(q)});
    m.metadata["ports"].push_back({1,decoded,"decoded"+std::to_string(q)});
  }
  m.validate(); return m;
}
static rds_sim *load(const std::string &path) {
  char error[512]; rds_options options{1,RDS_REFERENCE};
  auto *s = rds_load_with_options(path.c_str(),&options,error,sizeof error);
  if (!s) throw std::runtime_error(error); return s;
}
int main(int argc,char **argv) {
  try {
    check(argc==2,"test directory"); std::string dir=argv[1];
    std::filesystem::create_directories(dir);
    for (unsigned depth : {1,2,3}) {
      auto original=fixture(depth,0), candidate=original;
      cache_fifo_derived(candidate); candidate.validate();
      check(candidate.metadata["fifo_derived_summary"].size()==2,"missed derived fields");
      std::ofstream(dir+"/candidate.json")<<candidate.json();
      candidate=Model::read(dir+"/candidate.json");
      original.write_binary(dir+"/original.rsim"); candidate.write_binary(dir+"/candidate.rsim");
      auto *a=load(dir+"/original.rsim"), *b=load(dir+"/candidate.rsim");
      std::mt19937_64 random(9173+depth);
      for (unsigned cycle=0;cycle<6000;++cycle) {
        uint64_t values[]={uint64_t(cycle<2||cycle%101==0),random()&1,
                           random()&1,random()&1,random()};
        for (unsigned p=0;p<5;++p)
          check(!rds_set_u64(a,p,values[p])&&!rds_set_u64(b,p,values[p]),"set");
        check(!rds_eval(a)&&!rds_eval(b),"eval");
        for (unsigned p=5;p<original.metadata["ports"].size();++p) {
          unsigned count=(rds_port_width(a,p)+63)/64; uint64_t x[2],y[2];
          check(!rds_get(a,p,x,count)&&!rds_get(b,p,y,count),"get");
          check(std::equal(x,x+count,y),"derived/payload mismatch");
        }
        check(!rds_advance(a)&&!rds_advance(b),"advance");
      }
      rds_free(a);rds_free(b);
      std::cout<<"PASS derived depth="<<depth<<'\n';
    }
    // Pipelined enqueue/dequeue is supported by the simulator, but has a
    // different storage contract and must keep its original transition here.
    auto pipeline=fixture(2,2);cache_fifo_derived(pipeline);pipeline.validate();
    check(pipeline.metadata["fifo_derived_summary"].empty(),"changed bypass contract");
  } catch (const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
