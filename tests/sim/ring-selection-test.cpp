// Checks direct ring-head selections and aligned views through wraparound, invalid selectors and reattachment.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
using namespace rds;
static void check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
static Model fixture(bool mixed){
 Model m;m.widths={1,3,3,3,150};
 for(auto k:{"objects","registers","memories","writes","reads","assertions","origins","inventory"})m.metadata[k]=Json::array();
 m.metadata["ports"]={{0,0,"reset"},{0,1,"enqueue"},{0,2,"dequeue"},{0,3,"select"},{0,4,"payload"}};
 auto emit=[&](unsigned code,unsigned width,std::vector<Id> args,std::vector<uint64_t> imm=std::vector<uint64_t>{}){Id out=m.widths.size();m.widths.push_back(width);m.ops.push_back({code,out,args,imm});return out;};
 std::vector<Id> full{3},view{3};unsigned sizes[]={mixed?1u:2u,3,mixed?257u:7u};
 for(unsigned i=0;i<3;++i){Id en=emit(17,1,{1},{i}),de=emit(17,1,{2},{i});
  m.metadata["objects"].push_back({1,150,sizes[i],0,{0,en,4,de},"queue"+std::to_string(i)});
  Id data=emit(29,150,{}, {i,3});full.push_back(data);view.push_back(emit(17,70,{data},{64}));
  unsigned bits=1;while((1u<<bits)<=sizes[i])++bits;
  m.metadata["ports"].push_back({1,emit(29,bits,{}, {i,0}),"count"+std::to_string(i)});
 }
 m.metadata["ports"].push_back({1,emit(16,150,full),"selected"});
 m.metadata["ports"].push_back({1,emit(16,70,view),"view"});m.validate();return m;
}
static rds_sim*load(const std::string&path,unsigned workers,unsigned flags){char error[512];rds_options o{workers,flags};auto*s=rds_load_with_options(path.c_str(),&o,error,sizeof error);if(!s)throw std::runtime_error(error);return s;}
static void compile(const std::string&base,bool release){pid_t p=fork();check(p>=0,"fork");if(!p){const char*cc=std::getenv("CC");if(!cc)cc="cc";execlp(cc,cc,"-O3","-march=native",release?"-DNDEBUG":"-UNDEBUG","-fPIC","-shared",(base+".c").c_str(),"-o",(base+".so").c_str(),nullptr);_exit(127);}int status;check(waitpid(p,&status,0)==p&&WIFEXITED(status)&&!WEXITSTATUS(status),"compile");}
int main(int argc,char**argv){try{check(argc==2,"artifact directory");std::filesystem::create_directories(argv[1]);
 for(bool mixed:{false,true})for(unsigned workers:{1,4})for(bool release:{false,true}){
  std::string base=std::string(argv[1])+"/"+std::to_string(mixed)+"-"+std::to_string(workers)+"-"+std::to_string(release);fixture(mixed).write_binary(base+".rsim");
  auto*a=load(base+".rsim",1,RDS_REFERENCE),*b=load(base+".rsim",workers,RDS_PARALLEL_STATE|RDS_PARALLEL_PUBLISH|RDS_SPIN|RDS_NO_PARTITION|RDS_LIFT_PRIMITIVES);
  check(!rds_emit_c(b,(base+".c").c_str(),0),"emit");compile(base,release);check(!rds_use_compiled(b,(base+".so").c_str()),"attach");std::mt19937_64 random(8237);
  for(unsigned cycle=0;cycle<2400;++cycle){bool strict=cycle%3==0;uint64_t selector=random()&7,payload[]={random(),random(),random()&((1u<<22)-1)};
   uint64_t controls[]={uint64_t(cycle<2||cycle%701==0),random()&7,random()&7,selector};
   for(auto*s:{a,b}){rds_set_strict(s,strict);for(unsigned p=0;p<4;++p)check(!rds_set_u64(s,p,controls[p]),"control");check(!rds_set(s,4,payload,3),"payload");}
   int x=rds_eval(a),y=rds_eval(b);check(bool(x)==bool(y),"invalid selector mismatch");
   if(x){for(auto*s:{a,b})check(!rds_set_u64(s,3,1),"retry selector");check(!rds_eval(a)&&!rds_eval(b),"retry");}
   for(unsigned p=5;p<10;++p){unsigned count=(rds_port_width(a,p)+63)/64;uint64_t expected[3],actual[3];check(!rds_get(a,p,expected,count)&&!rds_get(b,p,actual,count),"get");check(std::equal(expected,expected+count,actual),"ring selection mismatch");}
   check(!rds_advance(a)&&!rds_advance(b),"advance");if(cycle%397==0)check(!rds_use_compiled(b,(base+".so").c_str()),"reattach");
  }
  rds_free(a);rds_free(b);std::cout<<"PASS ring mixed="<<mixed<<" workers="<<workers<<" release="<<release<<'\n';
 }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
