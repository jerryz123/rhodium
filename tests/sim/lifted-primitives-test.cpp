// Replays word-control boundaries and transactional semantic proposals against reference execution.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
using namespace rds;
static void check(bool ok,const std::string&why){if(!ok)throw std::runtime_error(why);}
static int host(void*context,const uint64_t*,size_t,uint64_t*,size_t){return *static_cast<bool*>(context)?-1:0;}
static void compile(const std::string&source,bool release){
  pid_t pid=fork();check(pid>=0,"fork failed");
  if(!pid){auto library=source+".so";execlp("clang","clang","-std=c17","-O2",release?"-DNDEBUG":"-UNDEBUG","-shared","-fPIC",source.c_str(),"-o",library.c_str(),nullptr);_exit(127);}
  int status;check(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&!WEXITSTATUS(status),"compile failed");
}
int main(int argc,char**argv){try{
  check(argc==2,"supply artifact directory");std::string dir=argv[1];std::filesystem::create_directories(dir);
  Model m;m.widths={1,1,64,64};
  m.metadata={{"format","rhodium-simulation-ir-v1"},{"registers",Json::array()},{"memories",Json::array()},
    {"writes",Json::array()},{"reads",Json::array()},{"assertions",Json::array()},{"objects",Json::array()},
    {"origins",Json::array()},{"inventory",Json::array()},{"occurrences",Json::array({"lifted"})},
    {"ports",Json::array({{0,0,"reset"},{0,1,"valid"},{0,2,"x"},{0,3,"y"}})}};
  auto emit=[&](unsigned code,unsigned width,std::vector<Id> args,std::vector<uint64_t> imm=std::vector<uint64_t>{}){
    Id v=m.widths.size();m.widths.push_back(width);m.ops.push_back({code,v,std::move(args),std::move(imm)});return v;};
  auto bit=[&](Id v,unsigned i){return emit(17,1,{v},{i%64});};
  auto output=[&](Id v){m.metadata["ports"].push_back({1,v,"out"+std::to_string(v)});};
  for(unsigned depth:{1u,3u,64u,65u})for(unsigned kind:{2u,9u}){
    Id id=m.metadata["objects"].size();std::vector<Id> ready;
    for(unsigned j=0;j<(kind==9?depth:1);++j)ready.push_back(bit(3,j));
    std::vector<Id> inputs{0,1,2};inputs.insert(inputs.end(),ready.begin(),ready.end());
    m.metadata["objects"].push_back({kind,64,depth,0,inputs,"control"+std::to_string(id)});
    output(emit(29,1,ready,{id,kind==9?0u:1u}));
    output(emit(29,64,{}, {id,kind==9?1u:3u}));
    for(unsigned j=0;j<(kind==9?depth:1);++j)output(emit(29,1,{}, {id,2+j}));
  }
  for(unsigned flags:{2u,18u})for(unsigned depth:{3u,64u,65u}){
    Id id=m.metadata["objects"].size();std::vector<Id> inputs{0,1},requests,payloads;
    for(unsigned j=0;j<depth;++j){Id request=bit(3,j);requests.push_back(request);inputs.push_back(request);inputs.push_back(2);payloads.push_back(2);}
    if(flags&16)for(unsigned j=0;j<depth;++j)inputs.push_back(bit(2,j));
    m.metadata["objects"].push_back({6,64,depth,flags,inputs,"arbiter"+std::to_string(id)});
    output(emit(29,1,requests,{id,1}));
    unsigned width=1;while((uint64_t(1)<<width)<depth)++width;
    output(emit(29,width,requests,{id,0}));
    requests.insert(requests.end(),payloads.begin(),payloads.end());output(emit(29,64,requests,{id,2}));
  }
  // Legal set/clear traffic includes same-index simultaneous updates, for which
  // clear wins. Reads keep observing the old busy word until a successful edge.
  Id sb=m.metadata["objects"].size(),busy=emit(29,64,{}, {sb,0}),index=emit(17,6,{2},{0});
  Id oldbit=emit(17,1,{emit(10,64,{busy,index})},{0});
  Id clear=emit(3,1,{1,oldbit}),set=emit(3,1,{1,emit(4,1,{emit(2,1,{oldbit}),bit(3,0)})});
  m.metadata["objects"].push_back({4,64,64,0,{0,set,index,clear,index},"scoreboard"});output(busy);
  Id zero=emit(0,1,{}, {0});m.metadata["objects"].push_back({8,1,1,0,{zero,zero,zero,zero,zero},"host"});
  m.validate();m.write_binary(dir+"/model.rsim");
  std::vector<rds_sim*> sims;std::vector<std::string> libraries;bool fail=false;
  for(unsigned mode=0;mode<5;++mode){char error[512];rds_options options{mode>=3?4u:1u,mode?RDS_LIFT_PRIMITIVES|RDS_LIFT_TRANSITIONS|RDS_AUTO_INLINE|RDS_FLOW_PREPARE: RDS_REFERENCE};
    auto*s=rds_load_with_options((dir+"/model.rsim").c_str(),&options,error,sizeof error);check(s,error);
    check(!rds_bind_host(s,"host",host,&fail),rds_error(s));std::string library;
    if(mode){auto source=dir+"/model-"+std::to_string(mode)+".c";check(!rds_emit_c(s,source.c_str(),4096),rds_error(s));compile(source,mode%2==0);library=source+".so";check(!rds_use_compiled(s,library.c_str()),rds_error(s));}
    sims.push_back(s);libraries.push_back(library);
  }
  std::mt19937_64 rng(0x4c494654);
  for(unsigned cycle=0;cycle<1000;++cycle){uint64_t inputs[]={cycle%73==0,cycle%3!=0,rng(),cycle%11?rng():0};
    for(auto*s:sims){const char*names[]={"reset","valid","x","y"};for(unsigned j=0;j<4;++j)check(!rds_set_u64(s,rds_find_port(s,names[j]),inputs[j]),rds_error(s));check(!rds_eval(s),rds_error(s));}
    for(const auto&p:m.metadata["ports"])if(p[0]==1){std::string name=p[2];uint64_t expected=0;
      for(unsigned i=0;i<sims.size();++i){uint64_t actual;check(!rds_get_u64(sims[i],rds_find_port(sims[i],name.c_str()),&actual),rds_error(sims[i]));if(!i)expected=actual;else check(actual==expected,"output diverged at cycle "+std::to_string(cycle)+" "+name);}}
    if(cycle%19==0){fail=true;for(auto*s:sims)check(rds_advance(s)!=0,"failed host published state");fail=false;
      for(unsigned i=1;i<sims.size();++i)check(!rds_use_compiled(sims[i],libraries[i].c_str()),rds_error(sims[i]));}
    for(auto*s:sims)check(!rds_advance(s),rds_error(s));
  }
  for(auto*s:sims)rds_free(s);
  std::cout<<"PASS: mask boundaries 1/3/64/65, packet ownership, scoreboard, debug/release, owner schedules and failed-host reattachment\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
