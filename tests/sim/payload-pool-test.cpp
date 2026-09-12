// Replays shared payload handles, concurrent ingress, previews and reset against original FIFOs.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sys/wait.h>
#include <unistd.h>
using namespace rds;
static void check(bool x,const std::string&s){if(!x)throw std::runtime_error(s);}
static int callback(void*context,const uint64_t*,size_t,uint64_t*out,size_t){out[0]=0;return *static_cast<bool*>(context)?-1:0;}
int main(int argc,char**argv){try{check(argc==2,"supply artifact directory");std::string dir=argv[1];std::filesystem::create_directories(dir);
 Model m;m.widths={1,1,1,1,1,244,244};for(auto key:{"ports","objects","registers","memories","reads","writes","assertions","origins","inventory"})m.metadata[key]=Json::array();
 const char*names[]={"reset","push0","push1","push2","select","data0","data1"};for(unsigned i=0;i<7;++i)m.metadata["ports"].push_back({0,i,names[i]});
 auto emit=[&](unsigned code,unsigned w,std::vector<Id>a,std::vector<uint64_t>im=std::vector<uint64_t>{}){Id id=m.widths.size();m.widths.push_back(w);m.ops.push_back({code,id,std::move(a),std::move(im)});return id;};
 Id one=emit(0,1,{}, {1});std::vector<Id> data;for(unsigned i=0;i<3;++i)data.push_back(emit(29,244,{}, {i,3}));
 Id selected=emit(15,244,{4,data[0],data[1]}, {1});
 m.metadata["objects"]={{1,244,1,0,{0,1,5,one},"left"},{1,244,1,0,{0,2,6,one},"right"},{1,244,1,0,{0,3,selected,one},"selected"}};
 Id zero=emit(0,1,{}, {0});m.metadata["objects"].push_back({8,1,1,0,{zero,zero,zero,zero,zero},"host"});
 for(unsigned i=0;i<3;++i){m.metadata["ports"].push_back({1,data[i],"out"+std::to_string(i)});m.metadata["ports"].push_back({1,emit(29,1,{}, {i,2}),"valid"+std::to_string(i)});}
 m.validate();m.write_binary(dir+"/original.rsim");auto pooled=m;pool_payloads(pooled,15);pooled.validate();pooled.write_binary(dir+"/pool.rsim");
 check(pooled.metadata["payload_pool_summary"]["groups"].size()==1,"missing shared pool");
 check(pooled.metadata["payload_pool_summary"]["groups"][0]["boundary_writers"]==2,"concurrent writers not exercised");
 for(unsigned size:{1u,32u}){auto bad=m;bool rejected=false;try{pool_payloads(bad,size);}catch(const std::exception&){rejected=true;}check(rejected,"invalid pool group bound accepted");}
 bool fail=false;
 std::vector<rds_sim*> sims;
 for(unsigned mode=0;mode<5;++mode){char error[512];rds_options opt{mode==4?4u:1u,mode<2?RDS_REFERENCE:4290056208u};auto*s=rds_load_with_options((dir+(mode?"/pool.rsim":"/original.rsim")).c_str(),&opt,error,sizeof error);check(s,error);
  if(mode>=2){std::string source=dir+"/compiled-"+std::to_string(mode)+".c";check(!rds_emit_c(s,source.c_str(),0),rds_error(s));pid_t p=fork();check(p>=0,"fork");if(!p){execlp("clang","clang","-O3","-march=native",mode==2?"-UNDEBUG":"-DNDEBUG","-shared","-fPIC",source.c_str(),"-o",(source+".so").c_str(),nullptr);_exit(127);}int status;check(waitpid(p,&status,0)==p&&WIFEXITED(status)&&!WEXITSTATUS(status),"compile");check(!rds_use_compiled(s,(source+".so").c_str()),rds_error(s));}
  check(!rds_bind_host(s,"host",callback,&fail),rds_error(s));sims.push_back(s);}
 std::mt19937_64 random(55511);
 for(unsigned cycle=0;cycle<5000;++cycle){uint64_t controls[]={cycle%37==0,random()&1,random()&1,random()&1,random()&1};uint64_t payload[2][4];for(auto&row:payload){for(auto&w:row)w=random();row[3]&=(UINT64_C(1)<<52)-1;}
  for(auto*s:sims){for(unsigned j=0;j<5;++j)check(!rds_set_u64(s,rds_find_port(s,names[j]),controls[j]),rds_error(s));for(unsigned j=0;j<2;++j)check(!rds_set(s,rds_find_port(s,names[5+j]),payload[j],4),rds_error(s));check(!rds_eval(s),rds_error(s));check(!rds_eval(s),rds_error(s));}
  for(unsigned j=0;j<3;++j)for(std::string prefix:{"out","valid"}){std::string name=prefix+std::to_string(j);unsigned words=prefix=="out"?4:1;uint64_t expected[4]={},actual[4]={};check(!rds_get(sims[0],rds_find_port(sims[0],name.c_str()),expected,words),"get reference");for(unsigned k=1;k<sims.size();++k){check(!rds_get(sims[k],rds_find_port(sims[k],name.c_str()),actual,words),"get candidate");check(std::equal(expected,expected+words,actual),"payload mismatch cycle "+std::to_string(cycle)+" mode "+std::to_string(k));}}
  if(cycle%53==0){fail=true;for(auto*s:sims)check(rds_advance(s)!=0,"failed host published pool state");fail=false;for(auto*s:sims)check(!rds_eval(s),rds_error(s));}
  for(auto*s:sims)check(!rds_advance(s),rds_error(s));
  if(cycle==2500)for(unsigned mode=2;mode<5;++mode)check(!rds_use_compiled(sims[mode],(dir+"/compiled-"+std::to_string(mode)+".c.so").c_str()),rds_error(sims[mode]));
 }
 for(auto*s:sims)rds_free(s);
 std::cout<<"PASS: concurrent ingress, shared selection, stale previews, reset, repeated evaluation, failed publication and reattachment\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
