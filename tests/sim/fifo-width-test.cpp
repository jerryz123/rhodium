// Checks FIFO and selection narrowing against wide-state execution, invalid selectors, and cyclic bit growth.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace rds;
static void check(bool x,const char*s){if(!x)throw std::runtime_error(s);}
static Model fixture(bool growth){Model m;m.widths={1,1,1,64};m.metadata={{"format","rhodium-simulation-ir-v1"},{"objects",Json::array()},{"registers",Json::array()},{"memories",Json::array()},{"writes",Json::array()},{"reads",Json::array()},{"assertions",Json::array()},{"origins",Json::array()},{"inventory",Json::array()},{"ports",{{0,0,"reset"},{0,1,"push"},{0,2,"pop"},{0,3,"input"}}}};
 auto emit=[&](unsigned code,unsigned width,std::vector<Id> args,std::vector<uint64_t> imm=std::vector<uint64_t>{}){Id id=m.widths.size();m.widths.push_back(width);m.ops.push_back({code,id,args,imm});return id;};
 Id q0=emit(29,240,{}, {0,3}),q1=emit(29,240,{}, {1,3});
 Id maskv=emit(0,64,{}, {0x7ffff}),bits=emit(3,64,{3,maskv}),wide=emit(18,240,{bits});
 Id incoming=emit(15,240,{2,q1,wide},{1});
 Id selector=emit(20,2,{1,2}),selected=emit(16,240,{selector,q0,incoming});
 m.metadata["ports"].push_back({1,selected,"selected"});
 for(unsigned extension:{18u,19u})for(unsigned low:{0u,37u,60u}){Id ext=emit(extension,240,{3}),view=emit(17,low==0?64:low==37?16:4,{ext},{low}),out=emit(18,240,{view});m.metadata["ports"].push_back({1,out,"view"+std::to_string(out)});}
 if(growth){Id amount=emit(0,240,{}, {1,0,0,0});incoming=emit(9,240,{incoming,amount});}
 m.metadata["objects"]={{1,240,2,0,{0,1,incoming,2},"ring/a"},{1,240,2,0,{0,1,q0,2},"ring/b"}};
 for(Id q:{q0,q1})m.metadata["ports"].push_back({1,q,"payload"+std::to_string(q)});m.validate();return m;}
static rds_sim*load(const std::string&p){char error[512];rds_options options{1,RDS_REFERENCE};auto*s=rds_load_with_options(p.c_str(),&options,error,sizeof error);if(!s)throw std::runtime_error(error);return s;}
int main(int argc,char**argv){try{check(argc==2,"test directory");std::string dir=argv[1];std::filesystem::create_directories(dir);for(bool growth:{false,true}){auto original=fixture(growth),candidate=original;narrow_fifo_payloads(candidate);candidate.validate();std::ofstream(dir+"/roundtrip.json")<<candidate.json();candidate=Model::read(dir+"/roundtrip.json");auto invalid=candidate.json();invalid["opcodes"][0]="invalid";std::ofstream(dir+"/invalid.json")<<invalid;bool rejected=false;try{Model::read(dir+"/invalid.json");}catch(const std::exception&){rejected=true;}check(rejected,"accepted invalid opcode inventory");for(const auto&o:candidate.metadata["objects"])check(o[1]==(growth?240:19),"wrong inductive width");original.write_binary(dir+"/original.rsim");candidate.write_binary(dir+"/candidate.rsim");auto*a=load(dir+"/original.rsim"),*b=load(dir+"/candidate.rsim");std::mt19937_64 random(8128);
 for(unsigned cycle=0;cycle<4000;++cycle){uint64_t values[]={uint64_t(cycle<2||cycle%137==0),random()&1,random()&1,random()};bool strict=cycle%3==0;rds_set_strict(a,strict);rds_set_strict(b,strict);for(unsigned p=0;p<4;++p){check(!rds_set_u64(a,p,values[p]),"set original");check(!rds_set_u64(b,p,values[p]),"set candidate");}int ea=rds_eval(a),eb=rds_eval(b);check(bool(ea)==bool(eb),"selector error mismatch");if(ea){check(strict&&values[1]==values[2],"unexpected eval failure");continue;}for(unsigned p=4;p<rds_get_stats(a).ports;++p){uint64_t x[4],y[4];check(!rds_get(a,p,x,4)&&!rds_get(b,p,y,4),"get");check(std::equal(x,x+4,y),"payload mismatch");}check(!rds_advance(a)&&!rds_advance(b),"advance");}rds_free(a);rds_free(b);std::cout<<"PASS growth="<<growth<<'\n';}
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
