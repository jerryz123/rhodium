// Checks cross-object snapshot caches across publication, speculative retry, and scratch reuse.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sys/wait.h>
#include <unistd.h>
using namespace rds;
static void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static void compile(const std::string &source,bool debug){
  pid_t child=fork();check(child>=0,"fork");
  if(!child){auto so=source+".so";execlp("clang","clang","-std=c17","-O3","-march=native",debug?"-UNDEBUG":"-DNDEBUG","-shared","-fPIC",source.c_str(),"-o",so.c_str(),nullptr);_exit(127);}
  int status;check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status),"compile");
}
struct Host {bool fail=false;unsigned calls=0;};
static int host(void *context,const uint64_t*,size_t,uint64_t*,size_t){auto*h=static_cast<Host*>(context);++h->calls;return h->fail?-1:0;}
static Model fixture(bool ambient){
 Model m;m.widths={1,1,1,64,1,1,64,2,64};
 m.metadata={{"format","rhodium-simulation-ir-v1"},{"registers",Json::array()},{"memories",Json::array()},{"writes",Json::array()},{"reads",Json::array()},{"assertions",Json::array()},{"objects",Json::array()},{"origins",Json::array()},{"inventory",Json::array()},{"occurrences",{"cache"}},
 {"ports",{{0,0,"reset"},{0,1,"enqueue0"},{0,2,"dequeue0"},{0,3,"data0"},{0,4,"enqueue1"},{0,5,"dequeue1"},{0,6,"data1"},{0,7,"accepts"},{0,8,"ambient"}}}};
 auto emit=[&](unsigned code,unsigned width,std::vector<Id> args,std::vector<uint64_t> imm=std::vector<uint64_t>{}){Id id=m.widths.size();m.widths.push_back(width);m.ops.push_back({code,id,std::move(args),std::move(imm)});return id;};
 m.metadata["objects"].push_back({1,64,1,0,std::vector<Id>{0,1,3,2},"fifo0"});
 m.metadata["objects"].push_back({1,64,1,0,std::vector<Id>{0,4,6,5},"fifo1"});
 Id v0=emit(29,1,{}, {0,2}),v1=emit(29,1,{}, {1,2});
 Id data0=emit(29,64,{}, {0,3}),data1=emit(29,64,{}, {1,3});
 Id req0=emit(20,2,{v0,v1}),req1=emit(20,2,{v1,v0}),zero=emit(0,2,{}, {0});
 Id grant0=emit(29,2,{zero,req0},{2,2}),grant1=emit(29,2,{grant0,req1},{2,3});
 m.metadata["objects"].push_back({10,2,2,32,std::vector<Id>{0,req0,req1,7},"matcher"});
 Id derived_constant=emit(6,64,{emit(0,64,{}, {41}),emit(0,64,{}, {1})});
 Id x=emit(6,64,{emit(19,64,{grant1}),derived_constant});if(ambient)x=emit(5,64,{x,8});
 for(unsigned i=0;i<96;++i){x=emit(6,64,{x,data0});x=emit(5,64,{x,data1});}
 // A 257-bit dynamic shift forces a direct command inside the cached region.
 x=emit(17,64,{emit(9,257,{emit(18,257,{x}),emit(17,8,{data1},{0})})},{13});
 Id result=emit(6,64,{x,8});m.metadata["ports"].push_back({1,result,"result"});
 m.metadata["ports"].push_back({1,grant0,"grant"});m.metadata["ports"].push_back({1,req0,"valid"});
 Id bitzero=emit(0,1,{}, {0});m.metadata["objects"].push_back({8,1,1,0,std::vector<Id>{bitzero,bitzero,bitzero,bitzero,bitzero},"host"});
 m.validate();return m;
}
int main(int argc,char**argv){try{
 check(argc==2,"supply output directory");std::filesystem::create_directories(argv[1]);std::mt19937_64 rng(0x6469727479);
 for(bool ambient:{false,true}){
  std::string stem=std::string(argv[1])+"/flow-"+std::to_string(ambient);auto m=fixture(ambient);m.write_binary(stem+".rsim");
  std::vector<rds_sim*> sims;std::vector<std::string> libraries;Host context;
  for(unsigned mode=0;mode<10;++mode){rds_options options{mode==4?4u:1u,mode==0?1u:1387597840u|(mode>=2?unsigned(RDS_FLOW_CACHE):0u)};char error[512];
   if(mode>=5)options.flags|=RDS_TYPED_SCRATCH;
   if(mode>=7)options.flags|=RDS_FLOW_PREPARE;
   if(mode==9)options.flags|=RDS_INLINE_BODIES;
   auto*s=rds_load_with_options((stem+".rsim").c_str(),&options,error,sizeof error);check(s,error);check(!rds_bind_host(s,"host",host,&context),rds_error(s));
   std::string library;
   if(mode){std::string source=stem+"-"+std::to_string(mode)+".c",plan=source+".json";check(!rds_emit_c(s,source.c_str(),mode==3?1:4096),rds_error(s));check(!rds_emit_plan(s,plan.c_str()),rds_error(s));Json p;std::ifstream(plan)>>p;
    if(mode==2)check(p["flow_cache_objects"].size()==(ambient?0u:3u),"cache closure selection mismatch");
    if(mode>=2&&mode<9&&mode!=3&&mode!=4&&!ambient){std::ifstream generated(source);std::string text{std::istreambuf_iterator<char>(generated),{}};check(text.find("static RDS_CACHE_NOINLINE void ")!=std::string::npos,"missing cache-domain outline");}
    if(mode==3){std::ifstream generated(source);std::string text{std::istreambuf_iterator<char>(generated),{}};check(text.find("if(direct_")!=std::string::npos,"missing direct fallback coverage");}
    compile(source,mode==2||mode==6||mode==8);library=source+".so";check(!rds_use_compiled(s,library.c_str()),rds_error(s));}
   sims.push_back(s);libraries.push_back(library);
  }
  auto set=[&](rds_sim*s,const std::vector<uint64_t>&input){const char*names[]={"reset","enqueue0","dequeue0","data0","enqueue1","dequeue1","data1","accepts","ambient"};for(unsigned i=0;i<input.size();++i)check(!rds_set_u64(s,rds_find_port(s,names[i]),input[i]),rds_error(s));};
  auto evaluate=[&](const std::vector<uint64_t>&input){std::vector<uint64_t> expected;unsigned before=context.calls;
    for(unsigned i=0;i<sims.size();++i){auto*s=sims[i];set(s,input);check(!rds_eval(s),rds_error(s));std::vector<uint64_t> actual;for(const char*name:{"result","grant","valid"}){uint64_t value;check(!rds_get_u64(s,rds_find_port(s,name),&value),rds_error(s));actual.push_back(value);}if(!i)expected=actual;else check(actual==expected,"cached output diverged");}check(context.calls==before,"eval invoked host");};
  for(unsigned cycle=0;cycle<512;++cycle){std::vector<uint64_t> input={cycle%53==0,rng()&1,rng()&1,rng(),rng()&1,rng()&1,rng(),rng()&3,rng()};
   if(cycle%4==0){input[1]=input[2]=input[4]=input[5]=input[7]=0;}
   evaluate(input);input[8]^=UINT64_MAX;evaluate(input);evaluate(input);
   if(cycle%11==0){context.fail=true;for(unsigned i=0;i<sims.size();++i){check(rds_advance(sims[i])!=0,"failed host published state");if(cycle%22==0&&!libraries[i].empty())check(!rds_use_compiled(sims[i],libraries[i].c_str()),rds_error(sims[i]));}context.fail=false;input[3]^=UINT64_MAX;input[0]=!input[0];evaluate(input);}
   for(auto*s:sims)check(!rds_advance(s),rds_error(s));
  }
  for(auto*s:sims)rds_free(s);
  std::cout<<"flow cache ambient="<<ambient<<" ten engines, resets, independent publications, speculative retry and reattachment passed\n";
 }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
