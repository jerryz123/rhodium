// Checks prepared payload snapshots when a host callback mutates a public input during advance.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sys/wait.h>
#include <unistd.h>
using namespace rds;
static void check(bool ok,const std::string&why){if(!ok)throw std::runtime_error(why);}
static void compile(const std::string&source,bool release){pid_t p=fork();check(p>=0,"fork");if(!p){execlp("clang","clang","-O3","-march=native",release?"-DNDEBUG":"-UNDEBUG","-shared","-fPIC",source.c_str(),"-o",(source+".so").c_str(),nullptr);_exit(127);}int status;check(waitpid(p,&status,0)==p&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"compile");}
struct Host{bool fail=false;unsigned calls=0;rds_sim*sim=nullptr;};
static int host(void*context,const uint64_t*,size_t,uint64_t*out,size_t){auto*h=static_cast<Host*>(context);++h->calls;out[0]=0;if(h->sim)check(!rds_set_u64(h->sim,rds_find_port(h->sim,"x"),UINT64_C(0xaa55aa55aa55aa55)),"host input mutation");return h->fail?-1:0;}
static Model fixture(unsigned queues,bool drop){Model m;for(auto key:{"ports","objects","registers","memories","reads","writes","assertions","origins","inventory"})m.metadata[key]=Json::array();m.widths={1,64,64};const char*names[]={"reset","x","y"};for(Id i=0;i<3;++i)m.metadata["ports"].push_back({0,i,names[i]});auto emit=[&](unsigned c,unsigned w,std::vector<Id>a,std::vector<uint64_t>im=std::vector<uint64_t>{}){Id v=m.widths.size();m.widths.push_back(w);m.ops.push_back({c,v,std::move(a),std::move(im)});return v;};
 std::vector<Id>payloads;Id late=1;
 for(unsigned i=0;i<queues;++i){Id left=emit(20,128,{1,emit(0,64,{}, {i+1})}),right=emit(20,128,{2,emit(0,64,{}, {i+40})});Id which=emit(17,1,{1},{i+24}),selector=emit(15,2,{which,emit(0,2,{}, {1}),emit(0,2,{}, {2})},{1});Id data=i%2?emit(16,128,{selector,left,right}):emit(5,128,{left,right});payloads.push_back(data);if(i%2)late=emit(5,64,{late,emit(17,64,{data},{0})});}
 for(unsigned i=0;i<queues;++i){Id valid=emit(17,1,{1},{i}),ready=emit(5,1,{emit(17,1,{late},{i}),emit(17,1,{2},{i})});m.metadata["objects"].push_back({1,128,1,drop?1:0,{0,valid,payloads[i],ready},"fifo"+std::to_string(i)});for(unsigned q:{0u,2u,3u})m.metadata["ports"].push_back({1,emit(29,q==3?128:1,{}, {i,q}),"out"+std::to_string(i)+"_"+std::to_string(q)});}
 Id no=emit(0,1,{}, {0});Id raw=m.metadata["objects"].size();m.metadata["objects"].push_back({1,64,1,drop?1:0,{0,emit(17,1,{1},{0}),1,emit(17,1,{1},{1})},"raw-input"});m.metadata["ports"].push_back({1,emit(29,64,{}, {raw,3}),"raw-output"});m.metadata["objects"].push_back({8,1,1,0,{no,no,no,no,no},"host"});m.validate();return m;
}
int main(int argc,char**argv){try{check(argc==2,"supply artifact directory");std::filesystem::create_directories(argv[1]);for(unsigned queues:{1u,3u,4u,8u,16u})for(bool drop:{false,true}){auto m=fixture(queues,drop);std::string base=std::string(argv[1])+"/batch"+std::to_string(queues)+(drop?"-drop":"-data");m.write_binary(base+".rsim");Host callback;std::vector<rds_sim*>engines;std::vector<std::string>libraries;
 for(unsigned mode=0;mode<5;++mode){char error[512];unsigned flags=mode?4290056208u:RDS_REFERENCE;if(mode==1)flags&=~RDS_FLOW_PREPARE;if(mode==4)flags|=RDS_COALESCE_SCRATCH;rds_options options{1,flags};auto*s=rds_load_with_options((base+".rsim").c_str(),&options,error,sizeof error);check(s,error);rds_set_strict(s,mode!=3);check(!rds_bind_host(s,"host",host,&callback),rds_error(s));std::string library;
 if(mode){std::string source=base+"-"+std::to_string(mode)+".c";check(!rds_emit_c(s,source.c_str(),mode==4?2:4096),rds_error(s));if(mode>=2&&queues>=4){std::ifstream f(source);std::string text((std::istreambuf_iterator<char>(f)),{});check(text.find("FIFO control batch")!=std::string::npos,"batch not exercised");}compile(source,mode==3);library=source+".so";check(!rds_use_compiled(s,library.c_str()),rds_error(s));}engines.push_back(s);libraries.push_back(library);}
 std::mt19937_64 random(943+queues);for(unsigned cycle=0;cycle<400;++cycle){uint64_t reset=cycle==0||cycle%53==0,x=random(),y=random();if(cycle%8<3)x=0;auto eval=[&](){unsigned before=callback.calls;std::vector<uint64_t>expected;for(unsigned e=0;e<engines.size();++e){auto*s=engines[e];for(auto [name,value]:std::vector<std::pair<const char*,uint64_t>>{{"reset",reset},{"x",x},{"y",y}})check(!rds_set_u64(s,rds_find_port(s,name),value),rds_error(s));check(!rds_eval(s),rds_error(s));std::vector<uint64_t>actual;for(const auto&p:m.metadata["ports"])if(p[0]==1){std::vector<uint64_t>v((m.widths[p[1].get<Id>()]+63)/64);check(!rds_get(s,rds_find_port(s,p[2].get<std::string>().c_str()),v.data(),v.size()),rds_error(s));actual.insert(actual.end(),v.begin(),v.end());}if(e==0)expected=actual;else check(actual==expected,"snapshot/payload mismatch");}check(callback.calls==before,"eval published host");};
 eval();eval();if(cycle%19==0){callback.fail=true;for(auto*s:engines){callback.sim=s;check(rds_advance(s)!=0,"failed host published");}callback.fail=false;if(cycle%38==0)for(unsigned e=1;e<engines.size();++e)check(!rds_use_compiled(engines[e],libraries[e].c_str()),rds_error(engines[e]));reset=!reset;x^=UINT64_C(0x7fffffff);eval();}for(auto*s:engines){callback.sim=s;check(!rds_advance(s),rds_error(s));}}
 for(auto*s:engines)rds_free(s);
 std::cout<<"PASS queues="<<queues<<" drop="<<drop<<'\n';}}
 catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
