// Checks decoder specialization against software over enumerated low bits and randomized high bits.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <tuple>
#include <sys/wait.h>
#include <unistd.h>
using namespace rds;
static void check(bool v,const std::string&s){if(!v)throw std::runtime_error(s);}
struct Case{unsigned bits,width,kind;std::vector<uint64_t>rows;std::string name;};
static uint64_t bitmask(unsigned bits){return bits==64?UINT64_MAX:(UINT64_C(1)<<bits)-1;}
static uint64_t selector(unsigned bits,unsigned kind,uint64_t x){uint64_t mask=bitmask(bits);
 if(kind==0)return 2|((x&((UINT64_C(1)<<(bits-5))-1))<<3)|(UINT64_C(1)<<(bits-2));
 if(kind==1)return x&((UINT64_C(1)<<(bits-3))-1);
 if(kind==2)return 42;
 if(kind==4)return ((~bitmask(bits-4))&mask)|(x&bitmask(bits-5));
 if(kind==5)return (~selector(bits,0,x))&mask;
 return x&mask;
}
int main(int argc,char**argv){try{check(argc==2,"artifact directory");std::string dir=argv[1];std::filesystem::create_directories(dir);Model m;m.widths={64,1,1};
 for(auto key:{"registers","memories","reads","writes","assertions","objects","origins","inventory"})m.metadata[key]=Json::array();
 m.metadata["ports"]={{0,0,"x"},{0,1,"enable"},{0,2,"reset"}};
 auto emit=[&](unsigned code,unsigned width,std::vector<Id>args,std::vector<uint64_t>im=std::vector<uint64_t>{}){Id out=m.widths.size();m.widths.push_back(width);m.ops.push_back({code,out,std::move(args),std::move(im)});return out;};
 std::vector<Case>cases;
 for(unsigned kind=0;kind<6;++kind)for(auto [bits,width,count]:std::vector<std::tuple<unsigned,unsigned,unsigned>>{{8,8,8},{9,64,8},{10,32,8},{11,16,8},{12,8,8},{12,9,8},{13,8,8},{12,8,7},{9,65,8},{9,1,8},{64,64,8}}){
  unsigned words=(width+63)/64;uint64_t mask=width>=64?UINT64_MAX:(UINT64_C(1)<<width)-1;
  std::vector<uint64_t>im{count};for(unsigned w=0;w<words;++w)im.push_back(w?1:mask);
  for(unsigned row=0;row<count;++row){uint64_t matchmask=row%3?bitmask(bits)^1:15,key=selector(bits,kind,row*47+3)&matchmask;if(row==2)key^=2;im.push_back(key);im.push_back(matchmask);
   for(unsigned w=0;w<words;++w)im.push_back(w?row&1:(UINT64_C(0x9e3779b97f4a7c15)*(row+1))&mask);
  }
  Id sel=(kind==0||kind==5)?emit(20,bits,{emit(0,3,{}, {2}),emit(17,bits-5,{0},{0}),emit(0,2,{}, {1})}):
    kind==4?emit(19,bits,{emit(20,bits-3,{emit(17,bits-5,{0},{0}),emit(0,2,{}, {2})})}):
    kind==1?emit(18,bits,{emit(17,bits-3,{0},{0})}):kind==2?emit(0,bits,{}, {42}):emit(17,bits,{0},{0});
  if(kind==5)sel=emit(2,bits,{sel});
  sel=emit(1,bits,{sel});Id decoded=emit(25,width,{sel},im),zero=emit(0,width,{},std::vector<uint64_t>(words));
  std::string name="d"+std::to_string(cases.size());cases.push_back({bits,width,kind,im,name});
  m.metadata["ports"].push_back({1,decoded,name});Id q=m.widths.size();m.widths.push_back(width);
  Id next=emit(15,width,{1,q,decoded},{1});m.metadata["registers"].push_back({q,next,2,zero});m.metadata["ports"].push_back({1,q,"q"+name});
 }
 m.validate();auto path=dir+"/decode.rsim";m.write_binary(path);auto lifted=m;specialize_decoders(lifted);lifted.validate();lifted.write_binary(dir+"/lifted.rsim");std::ofstream(dir+"/report.json")<<lifted.metadata["decoder_specialization_summary"].dump(2);std::vector<rds_sim*>sims;
 check(lifted.metadata["decoder_specialization_summary"]["decoders"].size()==55,"constant-field eligibility");
 for(unsigned mode=0;mode<8;++mode){char error[512];rds_options opt{mode>=6?4u:1u,mode>=2?4290056208u:RDS_REFERENCE};if(mode==4)opt.flags|=RDS_EAGER_COMBINATIONAL;if(mode==5)opt.flags|=RDS_SHARED_CODE;
  auto*s=rds_load_with_options((mode?dir+"/lifted.rsim":path).c_str(),&opt,error,sizeof error);check(s,error);if(mode>=6)check(rds_get_stats(s).workers>1,"parallel workers");
  if(mode>=2){auto c=dir+"/decode-"+std::to_string(mode)+".c";check(!rds_emit_c(s,c.c_str(),0),rds_error(s));auto p=fork();check(p>=0,"fork");if(!p){execlp("clang","clang","-O3","-march=native",mode%2?"-UNDEBUG":"-DNDEBUG","-shared","-fPIC",c.c_str(),"-o",(c+".so").c_str(),nullptr);_exit(127);}int status;check(waitpid(p,&status,0)==p&&WIFEXITED(status)&&!WEXITSTATUS(status),"compile");check(!rds_use_compiled(s,(c+".so").c_str()),rds_error(s));}
  sims.push_back(s);
 }
 std::mt19937_64 random(7361);
 for(unsigned cycle=0;cycle<8192;++cycle){uint64_t x=cycle|(random()&~UINT64_C(8191)),enable=cycle%3!=0,reset=cycle%173==0;
  for(auto*s:sims){check(!rds_set_u64(s,rds_find_port(s,"x"),x),rds_error(s));check(!rds_set_u64(s,rds_find_port(s,"enable"),enable),rds_error(s));check(!rds_set_u64(s,rds_find_port(s,"reset"),reset),rds_error(s));check(!rds_eval(s),rds_error(s));}
  for(const auto&c:cases){unsigned words=(c.width+63)/64;uint64_t key=selector(c.bits,c.kind,x);const uint64_t*expected=c.rows.data()+1;
   for(unsigned row=0;row<c.rows[0];++row){auto*r=c.rows.data()+1+words+row*(2+words);if((key&r[1])==r[0]){expected=r+2;break;}}
   uint64_t ref[2]{};check(!rds_get(sims[0],rds_find_port(sims[0],("q"+c.name).c_str()),ref,words),"state read");
   for(auto*s:sims){uint64_t actual[2]{};check(!rds_get(s,rds_find_port(s,c.name.c_str()),actual,words),"decode read");check(std::equal(expected,expected+words,actual),"decode mismatch "+c.name+" cycle "+std::to_string(cycle));check(!rds_get(s,rds_find_port(s,("q"+c.name).c_str()),actual,words),"state read");check(std::equal(ref,ref+words,actual),"state mismatch");}
  }
  for(auto*s:sims)check(!rds_advance(s),rds_error(s));
  if(cycle==4000)for(unsigned mode=2;mode<sims.size();++mode)check(!rds_use_compiled(sims[mode],(dir+"/decode-"+std::to_string(mode)+".c.so").c_str()),"reattach");
 }
 for(auto*s:sims)rds_free(s);std::cout<<"PASS 66 decoder shapes, 8192 inputs, 8 execution modes, first-match/default, state and reattachment\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
