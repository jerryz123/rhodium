// Checks the extracted CHI network against every endpoint of a running eight-hart vvadd SoC and records delta traces.
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include "vvadd-loader.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
using Json = nlohmann::json;
struct Sim {
  std::unique_ptr<rds_sim, decltype(&rds_free)> value{nullptr,rds_free};
  Sim(const std::string &model,const std::string &library,unsigned workers,unsigned flags) {
    char error[512];rds_options options{workers,flags};value.reset(rds_load_with_options(model.c_str(),&options,error,sizeof error));
    if(!value)throw std::runtime_error(error);
    rds_set_strict(value.get(),0);check(rds_use_compiled(value.get(),library.c_str()));
  }
  void check(int code) {if(code)throw std::runtime_error(rds_error(value.get()));}
  int port(const std::string &name) {int p=rds_find_port(value.get(),name.c_str());if(p<0)throw std::runtime_error("missing port "+name);return p;}
};
struct Boundary {std::string name;unsigned source,target,offset,words;};
static int host_tick(void *context,const uint64_t *in,size_t ni,uint64_t *out,size_t no) {return static_cast<VvaddLoader*>(context)->tick_current(in,ni,out,no);}
static void write32(std::ostream &out,uint32_t n) {for(unsigned i=0;i<4;++i)out.put(char(n>>(8*i)));}
static void write64(std::ostream &out,uint64_t n) {for(unsigned i=0;i<8;++i)out.put(char(n>>(8*i)));}
int main(int argc,char **argv) {try {
  if(argc<6||argc>8)throw std::runtime_error("soc-noc-replay DIR PROGRAM.bin SOC.so NOC.so WORKERS [ROUNDS] [TRACE.bin]");
  std::string dir=argv[1];unsigned workers=std::stoul(argv[5]),rounds=argc>6?std::stoul(argv[6]):32;
  bool bulk=std::getenv("RDS_NOC_BULK") && std::string(std::getenv("RDS_NOC_BULK"))=="1";
  Json manifest;std::ifstream(dir+"/manifest.json")>>manifest;
  VvaddLoader host;host.configure(argv[2],8,rounds);
  Sim soc(dir+"/observed-soc.rsim",argv[3],1,4290056208u),noc(dir+"/model.rsim",argv[4],workers,4092941424u);
  soc.check(rds_bind_host(soc.value.get(),nullptr,host_tick,&host));
  std::vector<Boundary> inputs,outputs;unsigned words=0,input_words=0;
  auto bind=[&](const char *section,const char *prefix,std::vector<Boundary> &ports) {
    for(const auto &p:manifest.at(section)){std::string name=p[2];unsigned count=(p[1].get<unsigned>()+63)/64;
      unsigned a=soc.port(std::string(prefix)+name),b=noc.port(name);
      if(rds_port_width(soc.value.get(),a)!=p[1]||rds_port_width(noc.value.get(),b)!=p[1])throw std::runtime_error("boundary width mismatch");
      ports.push_back({name,a,b,words,count});words+=count;}
  };
  bind("input_boundary","noc_input/",inputs);input_words=words;bind("output_boundary","noc_output/",outputs);
  std::vector<uint64_t> sample(words),previous(words),actual(words);
  std::ofstream trace;
  if(argc==8){trace.open(argv[7],std::ios::binary);if(!trace)throw std::runtime_error("cannot open trace");trace.write("RDSNOCT1",8);write32(trace,input_words);write32(trace,words-input_words);}
  unsigned reset=soc.port("reset"),uart=soc.port("uart_in"),exit=soc.port("exit"),hart_count_port=soc.port("hart_count");
  uint64_t changed=0,measured=0;
  for(uint64_t cycle=0;cycle<2000000+uint64_t(rounds)*4096;++cycle){
    soc.check(rds_set_u64(soc.value.get(),reset,cycle<8));soc.check(rds_set_u64(soc.value.get(),uart,1));soc.check(rds_eval(soc.value.get()));
    for(const auto &p:inputs){soc.check(rds_get(soc.value.get(),p.source,sample.data()+p.offset,p.words));noc.check(rds_set(noc.value.get(),p.target,sample.data()+p.offset,p.words));}
    noc.check(rds_eval(noc.value.get()));
    for(const auto &p:outputs){soc.check(rds_get(soc.value.get(),p.source,sample.data()+p.offset,p.words));noc.check(rds_get(noc.value.get(),p.target,actual.data()+p.offset,p.words));
      for(unsigned w=0;w<p.words;++w)if(sample[p.offset+w]!=actual[p.offset+w])throw std::runtime_error("NoC mismatch cycle="+std::to_string(cycle)+" port="+p.name+" word="+std::to_string(w)+" expected="+std::to_string(sample[p.offset+w])+" actual="+std::to_string(actual[p.offset+w]));}
    unsigned changes=0;for(unsigned w=0;w<words;++w)changes+=sample[w]!=previous[w];changed+=changes;
    bool active=host.measuring&&!host.finished_measurement;measured+=active;
    if(trace.is_open()){write32(trace,changes);trace.put(char(active));for(unsigned w=0;w<words;++w)if(sample[w]!=previous[w]){write32(trace,w);write64(trace,sample[w]);}if(!trace)throw std::runtime_error("trace write failed");}
    previous=sample;
    uint64_t status=0,model_harts=0;soc.check(rds_get_u64(soc.value.get(),exit,&status));soc.check(rds_get_u64(soc.value.get(),hart_count_port,&model_harts));
    if(model_harts != 8)throw std::runtime_error("unexpected hart count="+std::to_string(model_harts));
    if(status){if(status!=1||!host.error.empty())throw std::runtime_error(host.error.empty()?"vvadd failed":host.error);
      Json details=Json::array();for(unsigned h=0;h<8;++h)details.push_back({{"hart",h},{"progress",host.hart[h].progress},{"retired",host.hart[h].retired},{"start",host.hart[h].start},{"finish",host.hart[h].finish},{"heartbeat",host.hart[h].heartbeat}});
      std::cout<<Json({{"reference","eight-hart RV5Stage CHI NoC"},{"cycles",cycle+1},{"measured_cycles",measured},{"workers",rds_get_stats(noc.value.get()).workers},{"bulk",bulk},{"rounds",rounds},{"input_words",input_words},{"output_words",words-input_words},{"changed_words",changed},{"compared_output_words",(cycle+1)*(words-input_words)},{"hart_details",details},{"timing_claim",false}}).dump(2)<<'\n';return 0;}
    // A cached evaluation consumes the first bulk edge via ordinary advance.
    // Rewrite an identical input to invalidate it and exercise offer emission.
    if(bulk){const auto &p=inputs.front();noc.check(rds_set(noc.value.get(),p.target,sample.data()+p.offset,p.words));}
    noc.check(bulk?rds_advance_cycles(noc.value.get(),1):rds_advance(noc.value.get()));soc.check(rds_advance(soc.value.get()));
  }
  throw std::runtime_error("vvadd timeout");
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
