// Verifies reuse of complete request-prefix grants with reset, stalls and mismatched update operands.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace rds;
static void check(bool x,const char*s){if(!x)throw std::runtime_error(s);}
static Model fixture(unsigned columns){Model m;m.metadata={{"format","rhodium-simulation-ir-v1"},{"objects",Json::array()},{"registers",Json::array()},{"memories",Json::array()},{"writes",Json::array()},{"reads",Json::array()},{"assertions",Json::array()},{"origins",Json::array()},{"inventory",Json::array()},{"ports",Json::array()}};m.widths={1,columns};m.metadata["ports"]={{0,0,"reset"},{0,1,"accept"}};std::vector<Id> input{0},queries;
 for(unsigned c=0;c<columns;++c){Id v=m.widths.size();m.widths.push_back(5);input.push_back(v);m.metadata["ports"].push_back({0,v,"requests"+std::to_string(c)});}
 for(unsigned c=0;c<columns;++c){Id v=m.widths.size();m.widths.push_back(5);m.ops.push_back({29,v,std::vector<Id>(input.begin()+1,input.begin()+2+c),{0,c}});queries.push_back(v);m.metadata["ports"].push_back({1,v,"grant"+std::to_string(c)});}
 input.push_back(1);m.metadata["objects"]={{10,5,columns,32,input,"arbiter"}};m.validate();return m;}
int main(int argc,char**argv){try{check(argc==2,"test directory");std::string dir=argv[1];std::filesystem::create_directories(dir);std::mt19937_64 rng(1234);
 for(unsigned columns:{1,3}){auto original=fixture(columns),candidate=original;reuse_matcher_grants(candidate);candidate.validate();check(candidate.metadata["objects"][0][3]==96,"request prefix not reused");auto mismatch=original;mismatch.metadata["objects"][0][4][1]=original.ops[0].out;mismatch.validate();reuse_matcher_grants(mismatch);check(mismatch.metadata["objects"][0][3]==32,"mismatched request prefix reused");
 original.write_binary(dir+"/original.rsim");candidate.write_binary(dir+"/candidate.rsim");std::vector<rds_sim*> sims;for(const char*name:{"original","candidate"}){char error[512];rds_options options{1,RDS_REFERENCE};auto*s=rds_load_with_options((dir+"/"+name+".rsim").c_str(),&options,error,sizeof error);if(!s)throw std::runtime_error(error);sims.push_back(s);}
 for(unsigned cycle=0;cycle<4000;++cycle){std::vector<uint64_t>inputs{uint64_t(cycle<2||cycle%101==0),rng()&((1u<<columns)-1)};for(unsigned c=0;c<columns;++c)inputs.push_back(rng()&31);for(auto*s:sims){for(unsigned p=0;p<inputs.size();++p)check(!rds_set_u64(s,p,inputs[p]),"input");check(!rds_eval(s),"eval");}for(unsigned c=0;c<columns;++c){uint64_t a=0,b=0;check(!rds_get_u64(sims[0],columns+2+c,&a)&&!rds_get_u64(sims[1],columns+2+c,&b),"read");check(a==b,"grant mismatch");}for(auto*s:sims)check(!rds_advance(s),"advance");}for(auto*s:sims)rds_free(s);std::cout<<"PASS prefix columns="<<columns<<'\n';}
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
