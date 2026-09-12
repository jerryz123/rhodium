// Replays measured eight-hart SoC boundary traffic with exact fixed-input batches and checks terminal outputs.
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <vector>
using Json=nlohmann::json;
struct Change{unsigned word;uint64_t value;};
struct Run{uint64_t first=0,count=0;bool measured=false;std::vector<Change> changes;std::vector<unsigned> ports;};
struct Port{unsigned id,offset,words;};
static uint32_t read32(std::istream&f){uint32_t n=0;for(unsigned i=0;i<4;++i){int c=f.get();if(c<0)throw std::runtime_error("truncated trace");n|=uint32_t(c)<<(8*i);}return n;}
static uint64_t read64(std::istream&f){uint64_t n=0;for(unsigned i=0;i<8;++i){int c=f.get();if(c<0)throw std::runtime_error("truncated trace");n|=uint64_t(c)<<(8*i);}return n;}
static double now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
int main(int argc,char**argv){try{
 if(argc!=5&&argc!=6)throw std::runtime_error("soc-noc-trace DIR TRACE.bin WORKERS bulk|ordinary [LIBRARY.so]");
 std::string dir=argv[1],mode=argv[4];unsigned workers=std::stoul(argv[3]);if(mode!="bulk"&&mode!="ordinary")throw std::runtime_error("invalid engine");
 auto profile=reinterpret_cast<void(*)(int)>(dlsym(RTLD_DEFAULT,"rds_partition_sample_window"));
 if(profile)profile(0);
 Json manifest;std::ifstream(dir+"/manifest.json")>>manifest;
 rds_options options{workers,4092941424u};char error[512];std::unique_ptr<rds_sim,decltype(&rds_free)> sim(rds_load_with_options((dir+"/model.rsim").c_str(),&options,error,sizeof error),rds_free);if(!sim)throw std::runtime_error(error);auto*s=sim.get();
 auto check=[&](int code){if(code)throw std::runtime_error(rds_error(s));};rds_set_strict(s,0);
 std::string library=argc==6?argv[5]:dir+"/w"+std::to_string(workers)+".so";check(rds_use_compiled(s,library.c_str()));
 if(rds_get_stats(s).workers!=workers)throw std::runtime_error("actual worker mismatch");
 std::vector<Port>inputs,outputs;std::vector<unsigned>word_port;unsigned words=0,input_words=0;
 auto ports=[&](const char*section,std::vector<Port>&list){for(auto&p:manifest[section]){std::string name=p[2];int id=rds_find_port(s,name.c_str());if(id<0||rds_port_width(s,id)!=p[1])throw std::runtime_error("boundary mismatch");unsigned n=(p[1].get<unsigned>()+63)/64;list.push_back({unsigned(id),words,n});words+=n;}};
 ports("input_boundary",inputs);input_words=words;ports("output_boundary",outputs);word_port.resize(input_words);for(unsigned p=0;p<inputs.size();++p)for(unsigned w=0;w<inputs[p].words;++w)word_port[inputs[p].offset+w]=p;
 std::ifstream trace(argv[2],std::ios::binary);char magic[8];trace.read(magic,8);if(!trace||std::string(magic,8)!="RDSNOCT1"||read32(trace)!=input_words||read32(trace)!=words-input_words)throw std::runtime_error("trace header mismatch");
 std::vector<uint64_t> expected(words),values(input_words),actual(words);std::vector<Run> runs;uint64_t frames=0,measured=0,changes=0;
 while(trace.peek()!=std::char_traits<char>::eof()){
  unsigned count=read32(trace);int mark=trace.get();if(mark<0||mark>1||count>words)throw std::runtime_error("invalid frame");
  std::vector<Change> updates;std::set<unsigned> seen,touched;
  for(unsigned i=0;i<count;++i){unsigned w=read32(trace);uint64_t value=read64(trace);if(w>=words||!seen.insert(w).second)throw std::runtime_error("invalid changed word");expected[w]=value;if(w<input_words){updates.push_back({w,value});touched.insert(word_port[w]);}}
  if(runs.empty()||!updates.empty()||runs.back().measured!=bool(mark)){Run r;r.first=frames;r.measured=mark;r.changes=std::move(updates);r.ports.assign(touched.begin(),touched.end());runs.push_back(std::move(r));}
  ++runs.back().count;++frames;measured+=mark;changes+=count;
 }
 if(frames<2||!measured)throw std::runtime_error("empty measured trace");
 uint64_t timed_edges=0,bulk_calls=0;double elapsed=0,start=0;bool timing=false;
 for(const auto&r:runs){
  if(r.measured&&!timing){if(profile)profile(1);start=now();timing=true;}if(!r.measured&&timing){elapsed+=now()-start;if(profile)profile(0);timing=false;}
  for(const auto&c:r.changes)values[c.word]=c.value;
  for(unsigned id:r.ports){const auto&p=inputs[id];check(rds_set(s,p.id,values.data()+p.offset,p.words));}
  uint64_t edges=std::min(r.count,frames-1-r.first);if(r.measured)timed_edges+=edges;
  if(mode=="bulk"){check(rds_advance_cycles(s,edges));++bulk_calls;}else for(uint64_t i=0;i<edges;++i)check(rds_advance(s));
 }
 if(timing){elapsed+=now()-start;if(profile)profile(0);}
 check(rds_eval(s));uint64_t digest=1469598103934665603ull;
 for(const auto&p:outputs){check(rds_get(s,p.id,actual.data()+p.offset,p.words));for(unsigned w=0;w<p.words;++w){uint64_t value=actual[p.offset+w];if(value!=expected[p.offset+w])throw std::runtime_error("terminal trace mismatch");digest=(digest^value)*1099511628211ull;}}
 std::cout<<Json({{"reference","eight-hart RV5Stage CHI NoC recorded endpoints"},{"model",dir},{"workers",workers},{"mode",mode},{"frames",frames},{"measured_cycles",timed_edges},{"seconds",elapsed},{"ns_per_cycle",elapsed*1e9/timed_edges},{"fixed_input_runs",runs.size()},{"bulk_calls",bulk_calls},{"changed_words",changes},{"terminal_digest",digest},{"checks_inside_timing",false},{"input_delivery_inside_timing",true},{"trace_decode_inside_timing",false}}).dump()<<'\n';
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
