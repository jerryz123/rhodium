// Checks packed Boolean identities and decoder-column equality against complete tables and independent software.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace rds;
static void require(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
static Model fixture(unsigned field,unsigned variant){
 Model m;for(auto k:{"objects","registers","memories","writes","reads","assertions","origins","inventory","ports"})m.metadata[k]=Json::array();m.metadata["format"]="rhodium-simulation-ir-v1";
 auto input=[&](unsigned w,const char*name){Id v=m.widths.size();m.widths.push_back(w);m.metadata["ports"].push_back({0,v,name});return v;};
 auto emit=[&](unsigned c,unsigned w,std::vector<Id>a,std::vector<uint64_t>im=std::vector<uint64_t>{}){Id v=m.widths.size();m.widths.push_back(w);m.ops.push_back({c,v,a,im});return v;};
 Id key=input(7,"key"),enable=input(1,"enable"),ready=input(1,"ready");
 unsigned width=field*2+1;
 auto decision=[&](Big target,Big fallback){return Big(1)|(fallback<<1)|(target<<(field+1));};
 Big selected=Big(1)<<(field-1);
 std::vector<uint64_t> table{3};auto append=[&](Big n){auto ws=words(n,width);table.insert(table.end(),ws.begin(),ws.end());};
 append(decision(selected,variant==1?Big(0):selected));
 table.insert(table.end(),{1,3});append(decision(selected,variant==2?Big(0):selected));
 // The overlapping second row must not change first-match selection.
 table.insert(table.end(),{variant==1?2u:0u,variant==1?3u:0u});append(decision(0,0));
 table.insert(table.end(),{1,127});append(decision(selected,variant==3?Big(0):selected));
 Id decoded=emit(25,width,{key},table);
 Id valid=emit(17,1,{decoded},{0}),target=emit(17,1,{decoded},{2*field});
 Id active=emit(3,1,{enable,valid}),request=emit(3,1,{active,target});
 Id fallback=emit(17,field,{decoded},{1}),mask=emit(19,field,{active});
 Id filtered=emit(3,field,{mask,fallback}),fbit=emit(17,1,{filtered},{field-1});
 Id adaptive=emit(3,1,{emit(3,1,{request,emit(2,1,{fbit})}),ready});
 Id inverted=emit(5,1,{emit(2,1,{request}),request});
 Id choose=emit(15,1,{ready,fbit,fbit},{0});
 m.metadata["ports"].push_back({1,adaptive,"adaptive"});m.metadata["ports"].push_back({1,inverted,"inverse"});m.metadata["ports"].push_back({1,choose,"choose"});m.metadata["ports"].push_back({1,decoded,"decoded"});
 Id target_mask=emit(3,field,{mask,emit(17,field,{decoded},{field+1})});
 Id adaptive_mask=emit(3,field,{emit(3,field,{target_mask,emit(2,field,{filtered})}),emit(19,field,{ready})});
 Id empty=emit(12,1,{adaptive_mask,emit(0,field,{},words(0,field))});
 m.metadata["ports"].push_back({1,empty,"empty"});m.validate();return m;
}
static rds_sim*load(const std::string&p){char error[512];rds_options options{1,RDS_REFERENCE};auto*s=rds_load_with_options(p.c_str(),&options,error,sizeof error);if(!s)throw std::runtime_error(error);return s;}
int main(int argc,char**argv){try{require(argc==2,"artifact directory");std::string dir=argv[1];std::filesystem::create_directories(dir);
 for(unsigned field:{6,68})for(unsigned variant:{0,1,2,3}){
  auto a=fixture(field,variant),b=a;canonicalize_bit_relations(b);b.validate();
  require(b.metadata["bit_relation_summary"]["runtime_nodes_added"]==0,"runtime graph grew");
  require(b.metadata["bit_relation_summary"]["constants"].get<unsigned>()>0,"no identities proved");
  std::ofstream(dir+"/candidate.json")<<b.json();b=Model::read(dir+"/candidate.json");a.write_binary(dir+"/original.rsim");b.write_binary(dir+"/candidate.rsim");
  auto*x=load(dir+"/original.rsim"),*y=load(dir+"/candidate.rsim");
  for(unsigned key=0;key<128;++key)for(unsigned en=0;en<2;++en)for(unsigned ready=0;ready<2;++ready){
   bool first=(key&3)==1,default_row=variant==1&&!first&&(key&3)!=2;
   bool missing=(variant==2&&first)||default_row;
   // Row one shadows row three. Variant one also exercises the default.
   Big selected=(first||default_row)?Big(1)<<(field-1):Big(0),fallback=missing?Big(0):selected;
   auto expected=words(Big(1)|(fallback<<1)|(selected<<(field+1)),2*field+1);
   for(auto*s:{x,y}){require(!rds_set_u64(s,0,key)&&!rds_set_u64(s,1,en)&&!rds_set_u64(s,2,ready)&&!rds_eval(s),"evaluate");uint64_t v=0;
    require(!rds_get_u64(s,3,&v)&&v==unsigned(en&&ready&&missing),"adaptive mismatch");require(!rds_get_u64(s,4,&v)&&v==1,"complement mismatch");require(!rds_get_u64(s,5,&v)&&v==unsigned(en&&selected!=0&&!missing),"choice mismatch");
    std::vector<uint64_t>actual(expected.size());require(!rds_get(s,6,actual.data(),actual.size())&&actual==expected,"decoder result mismatch");
    require(!rds_get_u64(s,7,&v)&&v==unsigned(!(en&&ready&&missing)),"word equality mismatch");}
  }
  rds_free(x);rds_free(y);std::cout<<"PASS bit relations field="<<field<<" variant="<<variant<<'\n';
 }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
