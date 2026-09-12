// Checks exact-capacity token storage and pool-independent pipeline controls across cycle replay.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <filesystem>
#include <iostream>
#include <random>
#include <sys/wait.h>
#include <unistd.h>
using namespace rds;
static void check(bool ok,const std::string&why){if(!ok)throw std::runtime_error(why);}
static int host(void*context,const uint64_t*,size_t,uint64_t*out,size_t){out[0]=0;return *static_cast<bool*>(context)?-1:0;}
static Model model(unsigned stages,bool observe_mid,unsigned pipeline=0){
 Model m;m.widths={1,1,1,244};for(auto key:{"ports","objects","registers","memories","reads","writes","assertions","origins","inventory"})m.metadata[key]=Json::array();
 const char*names[]={"reset","push","pop","data"};for(unsigned i=0;i<4;++i)m.metadata["ports"].push_back({0,i,names[i]});
 auto emit=[&](unsigned code,unsigned width,std::vector<Id>args,std::vector<uint64_t>imm=std::vector<uint64_t>{}){Id id=m.widths.size();m.widths.push_back(width);m.ops.push_back({code,id,std::move(args),std::move(imm)});return id;};
 std::vector<Id> valid,ready,data;unsigned owners=pipeline==2?1:stages;
 for(unsigned i=0;i<owners;++i){valid.push_back(emit(29,1,{}, {i,2}));ready.push_back(emit(29,1,pipeline?std::vector<Id>{2}:std::vector<Id>{}, {i,1}));data.push_back(emit(29,244,{}, {i,3}));}
 for(unsigned i=0;i<owners;++i){m.metadata["objects"].push_back({pipeline?2:1,244,pipeline==2?stages:1,pipeline?8:0,{0,i?valid[i-1]:1,i?data[i-1]:3,pipeline?none:i+1==owners?2:ready[i+1]},"stage"+std::to_string(i)});
  m.metadata["ports"].push_back({1,valid[i],"v"+std::to_string(i)});m.metadata["ports"].push_back({1,ready[i],"r"+std::to_string(i)});}
 Id zero=emit(0,244,{}, {0,0,0,0}),out=emit(15,244,{valid.back(),zero,data.back()}, {1});m.metadata["ports"].push_back({1,out,"result"});
 if(observe_mid)m.metadata["ports"].push_back({1,emit(17,8,{data[owners/2]}, {13}),"mid"});
 Id z=emit(0,1,{}, {0});m.metadata["objects"].push_back({8,1,1,0,{z,z,z,z,z},"host"});m.validate();return m;
}
int main(int argc,char**argv){try{check(argc==2,"supply artifact directory");std::filesystem::create_directories(argv[1]);
 for(unsigned pipeline=0;pipeline<3;++pipeline)for(unsigned stages:{2u,3u,8u,16u,64u})for(bool observe_mid:{false,true})for(unsigned layout=0;layout<6;++layout){
  if((!pipeline&&layout>=4)||(pipeline&&(stages==2||stages==16||(layout!=0&&layout<3))))continue;
  bool direct=layout!=0,counters=layout==2||layout==5,separate=layout==3,phase=layout>=4;
  auto original=model(stages,observe_mid,pipeline),lifted=original;share_payload_lifetimes(lifted,direct,counters,separate,phase);lifted.validate();
  check(lifted.metadata["payload_lifetime_summary"]["chains"].size()==(observe_mid?0u:1u),"reader eligibility proof");
  if(!observe_mid){
   // A value-equivalent rewrite could still hydrate payloads to compute valid
   // or ready. Trace actual data dependencies, including transition controls.
   std::vector<bool> pooled(lifted.widths.size());unsigned pool_reads=0;
   for(const auto&o:lifted.ops){bool reads=direct?o.code==24:
     o.code==29&&o.imm[1]==3&&lifted.metadata["objects"][o.imm[0]][0]==1;
    pool_reads+=reads;
    for(Id arg:o.args)reads=reads||pooled[arg];
    pooled[o.out]=reads;
   }
   check(pool_reads!=0,"missing terminal payload read");
   auto control=[&](Id v){check(v==none||!pooled[v],"pipeline control depends on pooled data");};
   for(const auto&p:lifted.metadata["ports"])if(p[0]==1&&p[2]!="result")control(p[1]);
   for(const auto&r:lifted.metadata["registers"])for(const auto&v:r)control(v);
   for(const auto&w:lifted.metadata["writes"]){control(w[1]);control(w[3]);control(w[4]);}
   if(direct){check(lifted.metadata["memories"].size()==1,"payload storage count");
    check(lifted.metadata["memories"][0]==Json::array({244,stages}),"pool must mirror pipeline capacity");
   }else for(const auto&o:lifted.metadata["objects"])if(o[0]==1){
    check(o[1]==244&&o[2]==stages,"pool must mirror queue capacity");
    control(o[4][0]);control(o[4][1]);control(o[4][3]);
   }
  }
  auto path=std::string(argv[1])+"/"+std::to_string(stages)+"-pipe"+std::to_string(pipeline)+(observe_mid?"-observed":"-tokens")+(phase?(counters?"-packed-phase":"-phase"):separate?"-split":counters?"-counters":direct?"-sram":"-fifo");original.write_binary(path+"-original.rsim");lifted.write_binary(path+"-lifted.rsim");
  bool fail=false;std::vector<rds_sim*> sims;
  for(unsigned mode=0;mode<5;++mode){char error[512];rds_options options{mode==4?4u:1u,mode<2?RDS_REFERENCE:4290056208u};auto*s=rds_load_with_options((path+(mode?"-lifted.rsim":"-original.rsim")).c_str(),&options,error,sizeof error);check(s,error);
   if(mode>=2){auto c=path+"-"+std::to_string(mode)+".c";check(!rds_emit_c(s,c.c_str(),0),rds_error(s));pid_t p=fork();check(p>=0,"fork");if(!p){execlp("clang","clang","-O3","-march=native",mode==2?"-UNDEBUG":"-DNDEBUG","-shared","-fPIC",c.c_str(),"-o",(c+".so").c_str(),nullptr);_exit(127);}int status;check(waitpid(p,&status,0)==p&&WIFEXITED(status)&&!WEXITSTATUS(status),"compile");check(!rds_use_compiled(s,(c+".so").c_str()),rds_error(s));}
   check(!rds_bind_host(s,"host",host,&fail),rds_error(s));sims.push_back(s);
  }
  std::mt19937_64 random(9447);
  for(unsigned cycle=0;cycle<2000;++cycle){uint64_t reset=cycle==0||cycle%173==0,push=random()%4!=0,pop=cycle%300<130?0:random()&1,data[4];for(auto&w:data)w=random();data[3]&=(UINT64_C(1)<<52)-1;
   for(auto*s:sims){for(auto pair:{std::pair{"reset",reset},std::pair{"push",push},std::pair{"pop",pop}})check(!rds_set_u64(s,rds_find_port(s,pair.first),pair.second),rds_error(s));check(!rds_set(s,rds_find_port(s,"data"),data,4),rds_error(s));check(!rds_eval(s),rds_error(s));check(!rds_eval(s),rds_error(s));}
   for(auto&p:original.metadata["ports"])if(p[0]==1){std::string name=p[2];size_t words=(original.widths[p[1].get<Id>()]+63)/64;uint64_t expected[4]={},actual[4]={};check(!rds_get(sims[0],rds_find_port(sims[0],name.c_str()),expected,words),"get");for(unsigned k=1;k<sims.size();++k){check(!rds_get(sims[k],rds_find_port(sims[k],name.c_str()),actual,words),"get");check(std::equal(expected,expected+words,actual),"mismatch "+name+" at cycle "+std::to_string(cycle)+" mode "+std::to_string(k));}}
   if(cycle%97==0){fail=true;for(auto*s:sims)check(rds_advance(s)!=0,"failed host advanced state");fail=false;for(auto*s:sims)check(!rds_eval(s),rds_error(s));}
   for(auto*s:sims)check(!rds_advance(s),rds_error(s));
   if(cycle==1000)for(unsigned mode=2;mode<5;++mode)check(!rds_use_compiled(sims[mode],(path+"-"+std::to_string(mode)+".c.so").c_str()),rds_error(sims[mode]));
  }
  for(auto*s:sims)rds_free(s);
  std::cout<<"PASS stages="<<stages<<" observed="<<observe_mid<<" sram="<<direct<<" counters="<<counters<<" split="<<separate<<" pipeline="<<pipeline<<" phase="<<phase<<'\n';
 }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
