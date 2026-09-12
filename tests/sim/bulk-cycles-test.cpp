// Checks batched publication, failed phases, callbacks, and bank parity against ordinary edges.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <filesystem>
#include <iostream>
#include <memory>
#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace rds;
static void check(bool x,const std::string&s){if(!x)throw std::runtime_error(s);}
static bool offers(const std::string&path){void*h=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);check(h,"open generated library");using Phase=int(*)(void*);using Bind=Phase(*)(uint32_t,uint32_t);auto bind=reinterpret_cast<Bind>(dlsym(h,"rds_generated_offer_bind"));bool active=bind&&bind(0,0)&&bind(0,1)&&bind(0,2);dlclose(h);return active;}
struct Build {
 Model m;
 Build(){for(auto k:{"ports","objects","registers","memories","reads","writes","assertions","origins","inventory"})m.metadata[k]=Json::array();m.widths={1,1};m.metadata["ports"]={{0,0,"reset"},{0,1,"fault"}};}
 Id val(unsigned w){Id v=m.widths.size();m.widths.push_back(w);return v;}
 Id op(unsigned c,unsigned w,std::vector<Id>a={},std::vector<uint64_t>im={}){Id v=val(w);m.ops.push_back({c,v,std::move(a),std::move(im)});return v;}
 Id lit(uint64_t x,unsigned w=64){return op(0,w,{}, {x});}
 Model make(unsigned kind){Id no=lit(0,1),yes=lit(1,1),zero=lit(0),one=lit(1);std::vector<Id>q;for(unsigned i=0;i<4;++i)q.push_back(val(64));
  for(unsigned i=0;i<4;++i){Id d=op(6,64,{q[i],one});m.metadata["registers"].push_back({q[i],d,0,zero});m.metadata["ports"].push_back({1,q[i],"q"+std::to_string(i)});}
  if(kind==0){for(unsigned i=0;i<4;++i){m.metadata["memories"].push_back({64,2});Id address=op(17,1,{q[i]},{0}),old=op(24,64,{address},{i});m.metadata["writes"].push_back({i,address,q[(i+1)%4],yes,none,64});m.metadata["ports"].push_back({1,old,"memory"+std::to_string(i)});}}
  if(kind==1){for(unsigned i=0;i<4;++i){Id index=op(15,4,{1,lit(i,4),lit(12,4)},{1});m.metadata["objects"].push_back({4,8,8,0,{0,yes,index,yes,index},"scoreboard"+std::to_string(i)});m.metadata["ports"].push_back({1,op(29,8,{}, {i,0}),"busy"+std::to_string(i)});}}
  if(kind==2){Id okay=op(4,1,{op(2,1,{1}),op(13,1,{q[0],lit(3)})});m.metadata["assertions"].push_back({okay,0,yes,"bulk-limit"});}
  if(kind==3){m.metadata["objects"].push_back({8,64,1,0,{q[0],0,1,no,no},"host"});m.metadata["ports"].push_back({1,op(29,64,{}, {0,0}),"host-output"});}
  if(kind==4){Id selector=op(15,2,{1,lit(1,2),lit(0,2)},{1});m.metadata["ports"].push_back({1,op(16,64,{selector,q[0],q[1]}),"selection"});}
  if(kind==5||kind==7){for(unsigned i=0;i<4;++i){unsigned depth=kind==7?i+1:2,bits=depth==1?1:depth<4?2:3;Id ready=op(17,1,{q[(i+1)%4]},{0});m.metadata["objects"].push_back({1,64,depth,0,{0,yes,q[(i+2)%4],ready},"queue"+std::to_string(i)});m.metadata["ports"].push_back({1,op(29,64,{}, {i,3}),"payload"+std::to_string(i)});m.metadata["ports"].push_back({1,op(29,bits,{}, {i,0}),"occupancy"+std::to_string(i)});}}
  if(kind==6){for(unsigned i=0;i<4;++i){Id requests=op(17,4,{q[(i+1)%4]},{0}),grant=op(29,4,{requests},{i,0}),accept=op(17,1,{q[(i+2)%4]},{0});m.metadata["objects"].push_back({10,4,1,96,{0,grant,accept},"matcher"+std::to_string(i)});m.metadata["ports"].push_back({1,grant,"grant"+std::to_string(i)});}}
  if(kind==8){for(unsigned i=0;i<4;++i){std::vector<Id>requests,inputs{0};for(unsigned col=0;col<3;++col){requests.push_back(op(17,4,{q[(i+1)%4]},{4*col}));Id grant=op(29,4,requests,{i,col});inputs.push_back(grant);m.metadata["ports"].push_back({1,grant,"grant"+std::to_string(i)+"_"+std::to_string(col)});}inputs.push_back(op(17,3,{q[(i+2)%4]},{0}));m.metadata["objects"].push_back({10,4,3,96,inputs,"matcher"+std::to_string(i)});}}
  if(kind==9||kind==10){std::vector<Id>data,valid,ready;for(unsigned i=0;i<4;++i){data.push_back(op(29,64,{}, {i,3}));valid.push_back(op(29,1,{}, {i,2}));ready.push_back(op(29,1,{}, {i,1}));m.metadata["ports"].push_back({1,data.back(),"data"+std::to_string(i)});m.metadata["ports"].push_back({1,valid.back(),"valid"+std::to_string(i)});}for(unsigned i=0;i<4;++i){Id payload=i?data[i-1]:q[0];if(kind==10){Id bit=op(17,1,{q[0]},{0}),good=op(15,2,{bit,lit(1,2),lit(2,2)},{1}),bad=op(15,2,{bit,lit(0,2),lit(3,2)},{1});payload=op(16,64,{op(15,2,{1,good,bad},{1}),payload,q[1]});}m.metadata["objects"].push_back({1,64,2,0,{0,i?valid[i-1]:yes,payload,i==3?op(17,1,{q[1]},{0}):ready[i+1]},"pipe"+std::to_string(i)});}}
  m.validate();return m;
 }
};
struct Host{unsigned calls=0;};
static int host(void*p,const uint64_t*in,size_t,uint64_t*out,size_t){++static_cast<Host*>(p)->calls;if(in[2]&&in[0]==3)return -1;out[0]=in[0]*7;return 0;}
struct Engine{rds_sim*s;Host h;Engine(const std::string&file,unsigned n,unsigned flags,const std::string&library){char error[512];rds_options o{n,flags};s=rds_load_with_options(file.c_str(),&o,error,sizeof error);check(s,error);rds_set_strict(s,1);if(library.size())check(!rds_use_compiled(s,library.c_str()),rds_error(s));if(rds_find_port(s,"host-output")>=0)check(!rds_bind_host(s,"host",host,&h),rds_error(s));}~Engine(){rds_free(s);}void inputs(bool reset,bool fault){check(!rds_set_u64(s,0,reset)&&!rds_set_u64(s,1,fault),rds_error(s));}};
static std::vector<uint64_t> outputs(Engine&e){check(!rds_eval(e.s),rds_error(e.s));std::vector<uint64_t>r;for(unsigned p=2;p<rds_get_stats(e.s).ports;++p){uint64_t x;check(!rds_get_u64(e.s,p,&x),rds_error(e.s));r.push_back(x);}return r;}
int main(int argc,char**argv){try{check(argc==2,"artifact directory");std::filesystem::create_directories(argv[1]);
 for(unsigned kind=0;kind<11;++kind)for(unsigned mode=0;mode<5;++mode){if(mode==4&&kind<5)continue;std::string base=std::string(argv[1])+"/"+std::to_string(kind)+"-"+std::to_string(mode);auto m=Build().make(kind);m.write_binary(base+".rsim");unsigned flags=RDS_PARALLEL_STATE|RDS_PARALLEL_PUBLISH|RDS_SPIN|(mode==1?unsigned(RDS_NO_PARTITION):0u)|(mode==2?unsigned(RDS_COPY_STATE):0u);if(mode==3)flags&=~RDS_SPIN;if((kind==6||kind==8)&&mode!=0)flags|=RDS_LIFT_PRIMITIVES;
  {Engine emitter(base+".rsim",4,flags,"");check(rds_get_stats(emitter.s).workers==4,"need four actual workers");check(!rds_emit_c(emitter.s,(base+".c").c_str(),0),rds_error(emitter.s));}
  pid_t p=fork();check(p>=0,"fork");if(!p){const char*cc=std::getenv("CC");if(!cc)cc="cc";execlp(cc,cc,"-O3","-march=native",kind>=5&&mode!=4?"-DNDEBUG":"-UNDEBUG","-shared","-fPIC",(base+".c").c_str(),"-o",(base+".so").c_str(),nullptr);_exit(127);}int status;check(waitpid(p,&status,0)==p&&WIFEXITED(status)&&!WEXITSTATUS(status),"C compile");
  Engine bulk(base+".rsim",4,flags,base+".so"),ordinary(base+".rsim",4,flags,base+".so"),reference(base+".rsim",1,RDS_REFERENCE,"");
  if(kind>=9)check(offers(base+".so")== (mode!=4),"immutable offer entry activation");
  if(kind>=5&&mode!=4){rds_set_strict(bulk.s,false);rds_set_strict(ordinary.s,false);if(kind==10)rds_set_strict(reference.s,false);}
  for(unsigned round=0;round<50;++round){bool reset=round%9==0,fault=round%7==3;unsigned count=round%8;for(auto*e:{&bulk,&ordinary,&reference})e->inputs(reset,fault);if(round%2==0&&!fault)outputs(bulk);
   int b=rds_advance_cycles(bulk.s,count),a=0,r=0;for(unsigned n=0;n<count&&!a;++n)a=rds_advance(ordinary.s);for(unsigned n=0;n<count&&!r;++n)r=rds_advance(reference.s);
   check(bool(b)==bool(a)&&bool(a)==bool(r),"failure mismatch");check(rds_get_stats(bulk.s).cycles==rds_get_stats(ordinary.s).cycles&&rds_get_stats(bulk.s).cycles==rds_get_stats(reference.s).cycles,"partial progress mismatch");check(bulk.h.calls==ordinary.h.calls&&bulk.h.calls==reference.h.calls,"callback count mismatch");
   if(round==20)check(!rds_use_compiled(bulk.s,(base+".so").c_str()),rds_error(bulk.s));
   for(auto*e:{&bulk,&ordinary,&reference})e->inputs(false,false);
   check(outputs(bulk)==outputs(ordinary)&&outputs(bulk)==outputs(reference),"state mismatch after batch/failure");
   if(kind>=5&&round==12){
    // Consecutive batches change public reset without an intervening eval.
    // Reusable offers must describe state alone, not the previous inputs.
    for(unsigned reset_again:{0u,1u}){
      unsigned edges=reset_again?3:2;
      for(auto*e:{&bulk,&ordinary,&reference})e->inputs(reset_again,false);
      check(!rds_advance_cycles(bulk.s,edges),"consecutive bulk");
      for(unsigned edge=0;edge<edges;++edge){check(!rds_advance(ordinary.s),"consecutive ordinary");check(!rds_advance(reference.s),"consecutive reference");}
    }
    check(outputs(bulk)==outputs(ordinary)&&outputs(bulk)==outputs(reference),"state-only offer reuse mismatch");
   }
   if(round==21)check(!rds_use_compiled(bulk.s,(base+".so").c_str()),rds_error(bulk.s));
  }
  std::cout<<"PASS kind="<<kind<<" mode="<<mode<<" publication, failure, parity, reattach\n";
 }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
