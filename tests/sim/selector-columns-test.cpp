// Checks guarded selector-matrix regrouping with sparse columns, inactive rows and independent software.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace rds;
static void check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
static Model fixture(unsigned rows,unsigned bits,bool inactive){
 Model m;for(auto k:{"objects","registers","memories","writes","reads","assertions","origins","inventory","ports"})m.metadata[k]=Json::array();m.metadata["format"]="rhodium-simulation-ir-v1";
 std::vector<Id> selectors,enables;
 auto input=[&](unsigned width,const std::string&name){Id id=m.widths.size();m.widths.push_back(width);m.metadata["ports"].push_back({0,id,name});return id;};
 auto emit=[&](unsigned code,unsigned width,std::vector<Id> args,std::vector<uint64_t> imm=std::vector<uint64_t>{}){Id id=m.widths.size();m.widths.push_back(width);m.ops.push_back({code,id,args,imm});return id;};
 for(unsigned r=0;r<rows;++r){selectors.push_back(input(bits,"selector"+std::to_string(r)));enables.push_back(input(1,"enable"+std::to_string(r)));}
 for(unsigned key:{(1u<<bits)-1,0u,1u}){std::vector<Id> lanes;
  for(unsigned r=0;r<rows;++r){Id eq=emit(12,1,{emit(0,bits,{}, {key}),selectors[r]});lanes.push_back(inactive&&r==1?emit(0,1,{}, {0}):emit(3,1,{eq,enables[r]}));}
  Id column=emit(20,rows,lanes);m.metadata["ports"].push_back({1,column,"column"+std::to_string(key)});
 }
 m.validate();return m;
}
static rds_sim*load(const std::string&path){char error[512];rds_options options{1,RDS_REFERENCE};auto*s=rds_load_with_options(path.c_str(),&options,error,sizeof error);if(!s)throw std::runtime_error(error);return s;}
int main(int argc,char**argv){try{check(argc==2,"artifact directory");std::string dir=argv[1];std::filesystem::create_directories(dir);
 for(auto [rows,bits]:std::vector<std::pair<unsigned,unsigned>>{{5,3},{8,3},{2,5}})for(bool inactive:{false,true}){
  auto original=fixture(rows,bits,inactive),candidate=original;regroup_selector_columns(candidate);candidate.validate();
  check(candidate.metadata["selector_column_summary"]["outputs"]==3,"missing column group");std::ofstream(dir+"/candidate.json")<<candidate.json();candidate=Model::read(dir+"/candidate.json");
  original.write_binary(dir+"/original.rsim");candidate.write_binary(dir+"/candidate.rsim");auto*a=load(dir+"/original.rsim"),*b=load(dir+"/candidate.rsim");std::mt19937_64 random(71983);std::vector<unsigned>selectors(rows),enables(rows);
  for(unsigned cycle=0;cycle<4000;++cycle){for(unsigned r=0;r<rows;++r){selectors[r]=random()&((1u<<bits)-1);enables[r]=random()&1;for(auto*s:{a,b})check(!rds_set_u64(s,2*r,selectors[r])&&!rds_set_u64(s,2*r+1,enables[r]),"set");}
   check(!rds_eval(a)&&!rds_eval(b),"eval");unsigned p=2*rows;
   for(unsigned key:{(1u<<bits)-1,0u,1u}){uint64_t expected=0,x=0,y=0;for(unsigned r=0;r<rows;++r)expected|=uint64_t(enables[r]&&selectors[r]==key&&!(inactive&&r==1))<<r;check(!rds_get_u64(a,p,&x)&&!rds_get_u64(b,p,&y),"get");check(x==expected&&y==expected,"selector matrix mismatch");++p;}
   check(!rds_advance(a)&&!rds_advance(b),"advance");
  }
  rds_free(a);rds_free(b);std::cout<<"PASS selector rows="<<rows<<" bits="<<bits<<" inactive="<<inactive<<'\n';
 }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
