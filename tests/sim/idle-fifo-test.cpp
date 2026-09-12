// Checks inductive empty-FIFO folding against original cycle semantics and strict diagnostics.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sys/wait.h>
#include <unistd.h>
using namespace rds;
static void check(bool yes,const std::string&why){if(!yes)throw std::runtime_error(why);}
static int run(std::vector<std::string>args){pid_t pid=fork();check(pid>=0,"fork");if(!pid){std::vector<char*>v;for(auto&a:args)v.push_back(a.data());v.push_back(nullptr);execvp(v[0],v.data());_exit(127);}int status;check(waitpid(pid,&status,0)==pid,"waitpid");return WIFEXITED(status)?WEXITSTATUS(status):128;}
struct Host{bool fail=false;unsigned calls=0;};
static int host(void*context,const uint64_t*,size_t,uint64_t*out,size_t){auto*h=static_cast<Host*>(context);++h->calls;out[0]=0;return h->fail?-1:0;}
static Model fixture(unsigned width,unsigned depth,unsigned flags){
 Model m;m.widths={1,1,1,width,2,1};for(const char*k:{"ports","objects","registers","memories","reads","writes","assertions","origins","inventory"})m.metadata[k]=Json::array();
 const char*names[]={"reset","pop","query_valid","data","selector","active_push"};for(Id i=0;i<6;++i)m.metadata["ports"].push_back({0,i,names[i]});
 auto emit=[&](unsigned code,unsigned w,std::vector<Id>a,std::vector<uint64_t>im=std::vector<uint64_t>{}){Id v=m.widths.size();m.widths.push_back(w);m.ops.push_back({code,v,std::move(a),std::move(im)});return v;};
 Id no=emit(0,1,{}, {0}),zero=emit(0,width,{},words(0,width));Id partial=emit(16,width,{4,3,zero});
 unsigned cw=1;while((UINT64_C(1)<<cw)<=depth)++cw;
 Id count=emit(29,cw,{}, {0,0});Id ready=emit(29,1,flags&2?std::vector<Id>{1}:std::vector<Id>{},{0,1});
 Id valid=emit(29,1,flags&4?std::vector<Id>{2}:std::vector<Id>{},{0,2});Id preview=emit(29,width,(flags&4)&&!(flags&1)?std::vector<Id>{3}:std::vector<Id>{},{0,3});
 Id nonempty=emit(2,1,{emit(12,1,{count,emit(0,cw,{}, {0})})});
 m.metadata["objects"].push_back({1,width,depth,flags,{0,no,partial,1},"idle"});
 m.metadata["objects"].push_back({1,width,depth,0,{0,nonempty,3,1},"downstream"});
 m.metadata["objects"].push_back({1,width,depth,0,{0,5,3,1},"active"});
 m.metadata["objects"].push_back({8,1,1,0,{no,no,no,no,no},"host"});
 Id downstream=emit(29,width,{}, {1,3}),downvalid=emit(29,1,{}, {1,2}),active=emit(29,width,{}, {2,3}),active_count=emit(29,cw,{}, {2,0});
 Id state=m.widths.size();m.widths.push_back(width);m.metadata["registers"].push_back({state,emit(5,width,{preview,active}),0,zero});
 for(auto [v,name]:std::vector<std::pair<Id,const char*>>{{count,"count"},{ready,"ready"},{valid,"valid"},{preview,"preview"},{downstream,"downstream"},{downvalid,"downvalid"},{active,"active"},{active_count,"active_count"},{state,"state"}})m.metadata["ports"].push_back({1,v,name});
 m.validate();return m;
}
int main(int argc,char**argv){try{
 check(argc==2,"supply artifact directory");std::filesystem::create_directories(argv[1]);
 for(unsigned width:{1u,75u,257u})for(unsigned depth:{1u,3u})for(unsigned flags=0;flags<8;++flags){
  auto original=fixture(width,depth,flags),folded=original;fold_idle_fifos(folded);folded.validate();
  check(folded.metadata["idle_fifo_summary"]["removed"].size()==2,"must fold both inductively empty queues");check(folded.metadata["idle_fifo_summary"]["rounds"]==2,"must propagate empty state to downstream enqueue");
  check(folded.metadata["objects"].size()==2&&folded.metadata["objects"][0][5]=="active","must retain the independently driven queue and remap it");
  std::string base=std::string(argv[1])+"/"+std::to_string(width)+"-"+std::to_string(depth)+"-"+std::to_string(flags);original.write_binary(base+"-original.rsim");folded.write_binary(base+"-folded.rsim");
  auto release=folded;release_body(release);release.write_binary(base+"-release.rsim");
  Host callbacks;std::vector<rds_sim*>engines;std::vector<std::string>libraries;
  for(unsigned mode=0;mode<5;++mode){std::string image=base+(mode==0||mode==2?"-original.rsim":mode==4?"-release.rsim":"-folded.rsim");char error[512];
   rds_options options{mode==3?4u:1u,mode<2?RDS_REFERENCE:4290056208u};auto*s=rds_load_with_options(image.c_str(),&options,error,sizeof error);check(s,error);rds_set_strict(s,mode!=4);check(!rds_bind_host(s,"host",host,&callbacks),rds_error(s));std::string library;
   if(mode>=2){std::string source=base+"-"+std::to_string(mode)+".c";library=source+".so";check(!rds_emit_c(s,source.c_str(),0),rds_error(s));check(!run({"clang","-O3","-march=native",mode==4?"-DNDEBUG":"-UNDEBUG","-shared","-fPIC",source,"-o",library}),"generated compile");check(!rds_use_compiled(s,library.c_str()),rds_error(s));}
   engines.push_back(s);libraries.push_back(library);
  }
  std::mt19937_64 random(817+width+depth+flags);unsigned words=(width+63)/64;std::vector<uint64_t>data(words);
  auto evaluate=[&](bool reset,bool pop,bool valid,unsigned selector,bool push){std::vector<uint64_t>expected;unsigned before=callbacks.calls;
   for(unsigned e=0;e<engines.size();++e){auto*s=engines[e];for(auto [name,value]:std::vector<std::pair<const char*,uint64_t>>{{"reset",reset},{"pop",pop},{"query_valid",valid},{"selector",selector},{"active_push",push}})check(!rds_set_u64(s,rds_find_port(s,name),value),rds_error(s));check(!rds_set(s,rds_find_port(s,"data"),data.data(),words),rds_error(s));check(!rds_eval(s),rds_error(s));
    std::vector<uint64_t>actual;for(const char*name:{"count","ready","valid","preview","downstream","downvalid","active","active_count","state"}){int p=rds_find_port(s,name);std::vector<uint64_t>v((rds_port_width(s,p)+63)/64);check(!rds_get(s,p,v.data(),v.size()),rds_error(s));actual.insert(actual.end(),v.begin(),v.end());}if(e==0)expected=actual;else check(actual==expected,"folded FIFO output mismatch");
   }check(callbacks.calls==before,"eval published a host effect");};
  for(unsigned cycle=0;cycle<500;++cycle){for(auto&w:data)w=random();if(width%64)data.back()&=(UINT64_C(1)<<(width%64))-1;
   bool reset=cycle==0||cycle%97==0,pop=random()&1,valid=random()&1,push=cycle>=20&&(random()&1);unsigned selector=1u<<(random()&1);
   evaluate(reset,pop,valid,selector,push);evaluate(reset,pop,valid,selector,push);
   if(cycle%117==0){callbacks.fail=true;for(auto*s:engines)check(rds_advance(s)!=0,"failed host published state");callbacks.fail=false;evaluate(reset,pop,valid,selector,push);}
   for(auto*s:engines)check(!rds_advance(s),rds_error(s));
   if(cycle==133)for(unsigned e=2;e<engines.size();++e)check(!rds_use_compiled(engines[e],libraries[e].c_str()),rds_error(engines[e]));
  }
  for(unsigned e=0;e<engines.size();++e){auto*s=engines[e];check(!rds_set_u64(s,rds_find_port(s,"selector"),3),rds_error(s));check((rds_eval(s)!=0)==(e!=4),"dead-input strict selector diagnostic changed");rds_free(s);}
  std::cout<<"PASS width="<<width<<" depth="<<depth<<" flags="<<flags<<'\n';
 }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
