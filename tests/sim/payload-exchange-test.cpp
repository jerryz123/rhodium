// Replays routed tokens and checks retained control fields never depend on pooled body reads.
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
static int host(void*p,const uint64_t*,size_t,uint64_t*out,size_t){out[0]=0;return *static_cast<bool*>(p)?-1:0;}
static Model circuit(unsigned width,unsigned mode){
 Model m;m.widths={1,1,1,1,1,width,width};for(auto k:{"ports","objects","registers","memories","reads","writes","assertions","origins","inventory"})m.metadata[k]=Json::array();
 const char*names[]={"reset","push0","push1","pop0","pop1","data0","data1"};for(Id i=0;i<7;++i)m.metadata["ports"].push_back({0,i,names[i]});
 auto emit=[&](unsigned code,unsigned w,std::vector<Id>a,std::vector<uint64_t>imm=std::vector<uint64_t>{}){Id id=m.widths.size();m.widths.push_back(w);m.ops.push_back({code,id,std::move(a),std::move(imm)});return id;};
 auto both=[&](Id a,Id b){return emit(3,1,{a,b});};auto either=[&](Id a,Id b){return emit(4,1,{a,b});};auto inv=[&](Id a){return emit(2,1,{a});};
 std::vector<Id>valid,ready,data,header;
 for(Id i=0;i<4;++i){valid.push_back(emit(29,1,{}, {i,2}));ready.push_back(emit(29,1,{}, {i,1}));data.push_back(emit(29,width,{}, {i,3}));header.push_back(emit(17,4,{data.back()},{width-4}));}
 Id route0=emit(17,1,{header[0]},{0}),route1=emit(17,1,{header[1]},{0});
 Id g00=both(valid[0],inv(route0)),g01=both(valid[0],route0);
 Id g10=both(both(valid[1],inv(route1)),inv(g00)),g11=both(both(valid[1],route1),inv(g01));
 Json matcher_object;
 if(mode>=5&&mode<=14){bool packed=mode==6||mode==8||mode>=9,step=mode!=7&&mode!=8&&mode!=12&&mode!=14;
  unsigned rows=mode>=13?64:mode>=11?7:2;Id unused=emit(0,1,{}, {0});std::vector<Id>request_bits(rows,unused);request_bits.front()=valid[0];request_bits.back()=valid[1];
  Id requests=emit(20,rows,request_bits),query_requests=mode==10?emit(0,rows,{}, {3}):requests;
  Id empty=emit(0,rows,{}, {0});std::vector<Id>first=packed?std::vector<Id>{query_requests}:std::vector<Id>{valid[0],valid[1]};
  if(step)first.insert(first.begin(),empty);
  Id grant0=emit(29,rows,first,{4,step?2u:0u});
  std::vector<Id>second=packed?std::vector<Id>{query_requests}:std::vector<Id>{valid[0],valid[1]};
  if(step)second.insert(second.begin(),mode==9?empty:grant0);
  else second.insert(second.begin(),first.begin(),first.end());
  Id grant1=emit(29,rows,second,{4,step?3u:1u});
  g00=emit(17,1,{grant0},{0});g10=emit(17,1,{grant0},{rows-1});g01=emit(17,1,{grant1},{0});g11=emit(17,1,{grant1},{rows-1});
  std::vector<Id>inputs=packed?std::vector<Id>{0,requests,requests,emit(20,2,{ready[2],ready[3]})}:std::vector<Id>{0,valid[0],valid[0],valid[1],valid[1],ready[2],ready[3]};
  matcher_object={10,rows,2,packed?32:0,inputs,"matcher"};
 }
 if(mode==1){g01=g00;g11=g10;}
 Id select0=emit(15,width,{g10,data[0],data[1]},{1}),select1=emit(15,width,{g11,data[0],data[1]},{1});
 if(mode==4){select0=emit(16,width,{emit(20,2,{inv(g10),g10}),data[0],data[1]});select1=emit(16,width,{emit(20,2,{inv(g11),g11}),data[0],data[1]});}
 if(mode==25){Id index=emit(15,7,{g10,emit(0,7,{}, {0}),emit(0,7,{}, {65})},{1});
  std::vector<Id>args{index,data[0]};std::vector<uint64_t>keys;
  for(unsigned k=0;k<=65;++k){keys.push_back(k);args.push_back(k==65?data[1]:data[0]);}
  select0=emit(15,width,std::move(args),std::move(keys));
 }
 Id zero=emit(0,1,{}, {0});
 Id take0=either(both(g00,ready[2]),both(g01,ready[3])),take1=either(both(g10,ready[2]),both(g11,ready[3]));
 if(mode==2)take0=zero;
 m.metadata["objects"]={{1,width,1,0,{0,1,5,take0},"ingress0"},{1,width,1,0,{0,2,6,take1},"ingress1"},{1,width,1,0,{0,either(g00,g10),select0,3},"egress0"},{1,width,1,0,{0,either(g01,g11),select1,4},"egress1"}};
 if(!matcher_object.is_null())m.metadata["objects"].push_back(matcher_object);
 for(Id i=0;i<4;++i){m.metadata["ports"].push_back({1,valid[i],"valid"+std::to_string(i)});m.metadata["ports"].push_back({1,ready[i],"ready"+std::to_string(i)});m.metadata["ports"].push_back({1,header[i],"route"+std::to_string(i)});}
 Id zeros=emit(0,width,{},std::vector<uint64_t>((width+63)/64));
 for(Id i=2;i<4;++i)m.metadata["ports"].push_back({1,emit(15,width,{valid[i],zeros,data[i]},{1}),"result"+std::to_string(i-2)});
 if(mode>=15&&mode<=17){Id source=mode==17?0:2,field=emit(17,32,{data[source]},{0}),empty=emit(0,32,{}, {0});
  Id result=mode==16?field:emit(15,32,{valid[source],empty,field},{1});
  m.metadata["ports"].push_back({1,result,"body_field"});
 }
 if(mode==18)m.metadata["ports"].push_back({1,emit(17,32,{data[0]},{width/3}),"middle_control"});
 if(mode==19)for(unsigned low:{3u,17u,65u})m.metadata["ports"].push_back({1,emit(17,1,{data[0]},{low}),"control"+std::to_string(low)});
 if(mode==20||mode==21){Id bit=emit(17,1,{data[2]},{9});m.metadata["ports"].push_back({1,mode==20?both(valid[2],bit):either(inv(valid[2]),bit),"terminal_predicate"});}
 if(mode==22||mode==23){Id field=emit(17,32,{data[2]},{0});m.metadata["memories"].push_back({32,1});
  m.metadata["writes"].push_back({0,zero,field,mode==22?valid[2]:emit(0,1,{}, {1}),none,0});
  m.metadata["ports"].push_back({1,emit(24,32,{zero},{0}),"retired_word"});
 }
 if(mode==24||mode==38){Id previous_valid=1,previous_data=5;unsigned capacity=mode==38?73:17;
  for(unsigned owner=4;owner<capacity;++owner){Id next_ready=owner+1==capacity?ready[0]:emit(29,1,{}, {owner+1,1});
   m.metadata["objects"].push_back({1,width,1,0,{0,previous_valid,previous_data,next_ready},"pipeline"+std::to_string(owner)});
   previous_valid=emit(29,1,{}, {owner,2});previous_data=emit(29,width,{}, {owner,3});
  }
  m.metadata["objects"][0][4][1]=previous_valid;m.metadata["objects"][0][4][2]=previous_data;
 }
 if((mode>=26&&mode<=28)||mode==35){Id source=mode==35?0:2,field=emit(17,32,{data[source]},{0}),owner=m.metadata["objects"].size();
  m.metadata["objects"].push_back({1,32,4,mode==27?2:0,{0,mode==28?Id(1):valid[source],field,3},"terminal_fifo"});
  m.metadata["ports"].push_back({1,emit(29,32,{}, {owner,3}),"terminal_fifo_data"});
  m.metadata["ports"].push_back({1,emit(29,1,{}, {owner,2}),"terminal_fifo_valid"});
 }
 if(mode==29||mode==30){Id field=emit(17,32,{data[2]},{0}),state=m.widths.size();m.widths.push_back(64);
  Id next=emit(23,64,{state,mode==30?Id(1):valid[2],emit(0,1,{}, {1}),field},{2,32,1,1});
  m.metadata["registers"].push_back({state,next,0,emit(0,64,{}, {0})});
  m.metadata["ports"].push_back({1,state,"terminal_masked_state"});
 }
 if(mode==31||mode==32){Id field=emit(17,64,{data[2]},{0}),mask=emit(19,64,{valid[2]});
  m.metadata["ports"].push_back({1,emit(mode==31?3:4,64,{field,mode==31?mask:emit(2,64,{mask})}),"terminal_word_guard"});
 }
 if(mode==33||mode==34)m.metadata["ports"].push_back({1,data[2],"early_terminal0"});
 if(mode==34)m.metadata["ports"].push_back({1,data[3],"early_terminal1"});
 if(mode==36||mode==37){Id priming=mode==36?2:3,preview=emit(17,32,{data[priming]},{0});
  // Prime a constant under one queue's invalid-state assumption before a
  // different source's observation depends on that queue being valid.
  m.metadata["ports"].push_back({1,emit(15,32,{valid[priming],emit(0,32,{}, {0}),preview},{1}),"priming_observation"});
  Id source=mode==36?0:2,guard=mode==36?valid[2]:valid[3],field=emit(17,32,{data[source]},{0});
  m.metadata["ports"].push_back({1,emit(15,32,{guard,emit(0,32,{}, {0}),field},{1}),"cross_queue_observation"});
 }
 if(mode==3)m.metadata["ports"].push_back({1,emit(15,width,{valid[0],zeros,data[0]},{1}),"intermediate"});
 m.metadata["objects"].push_back({8,1,1,0,{zero,zero,zero,zero,zero},"host"});m.validate();return m;
}
int main(int argc,char**argv){try{check(argc>=2&&argc<=4,"supply artifact directory and optional first variant/layout");unsigned first=argc>=3?std::stoul(argv[2]):0,first_layout=argc==4?std::stoul(argv[3]):0;check(first<39&&first_layout<4,"first variant/layout out of range");std::filesystem::create_directories(argv[1]);
 for(unsigned layout=first_layout;layout<4;++layout)for(unsigned width:{75u,244u,1024u})for(unsigned variant=first;variant<39;++variant){
  bool packed=layout!=0;
  Model original=circuit(width,variant),lifted=original;exchange_payload_handles(lifted,packed,layout==2,variant>=33,layout==3);lifted.validate();
  auto summary=lifted.metadata["payload_exchange_summary"];
  bool extra_control=(variant>=16&&variant<=18)||variant==23||variant==28||variant==30||(variant>=35&&variant<=37);
  bool accepted=variant==0||variant==4||(variant>=5&&variant<=8)||(variant>=11&&variant<=15)||(extra_control&&width>=244)||(variant>=19&&variant<=22)||(variant>=24&&variant<34&&!extra_control)||variant==38;
  check(summary["regions"].size()==(accepted?1u:0u),"ownership proof decision: "+summary.dump());
  if(accepted){unsigned capacity=variant==38?73:variant==24?17:variant==33?3:4,hw=1;while((1u<<hw)<capacity)++hw;
   check(lifted.metadata["memories"].back()[1]==capacity,"pool capacity differs from hardware");check(summary["regions"][0]["inline_bits"]==(extra_control?36:variant==19?7:4),"observed fields not retained");check(lifted.metadata["registers"].size()==(layout==3?1:packed?(capacity+64/hw-1)/(64/hw):capacity)+(variant==29||variant==30),"handle state layout");
   // Equivalent values alone would miss a regression that reconstructs a wide
   // packet from the pool just to read an inline route or intermediate field.
   std::vector<bool> pooled(lifted.widths.size());
   for(const auto&o:lifted.ops){bool reads=o.code==24&&o.imm[0]>=original.metadata["memories"].size();
    for(Id arg:o.args)reads=reads||pooled[arg];
    pooled[o.out]=reads;}
   for(const auto&p:lifted.metadata["ports"])if(p[0]==1){std::string name=p[2];
    bool inline_field=name.rfind("valid",0)==0||name.rfind("ready",0)==0||name.rfind("route",0)==0||
     name.rfind("control",0)==0||name=="middle_control"||name=="cross_queue_observation"||
     (name=="body_field"&&(variant==16||variant==17));
    if(inline_field)check(!pooled[p[1].get<Id>()],"inline field reads pool: "+name);
   }
  }
  auto base=std::string(argv[1])+"/"+std::to_string(width)+"-"+std::to_string(variant)+(layout==3?"-bitmap":layout==2?"-fused":packed?"-packed":"");original.write_binary(base+"-original.rsim");lifted.write_binary(base+"-lifted.rsim");std::ofstream(base+"-report.json")<<lifted.json().dump();
  bool fail=false;std::vector<rds_sim*>sims;
  for(unsigned mode=0;mode<5;++mode){char error[512];rds_options opt{mode==4?4u:1u,mode<2?RDS_REFERENCE:4290056208u};if(mode==3)opt.flags|=RDS_INLINE_BODIES;auto*s=rds_load_with_options((base+(mode?"-lifted.rsim":"-original.rsim")).c_str(),&opt,error,sizeof error);check(s,error);
   if(mode>=2){auto c=base+"-"+std::to_string(mode)+".c";check(!rds_emit_c(s,c.c_str(),0),rds_error(s));auto p=fork();check(p>=0,"fork");if(!p){execlp("clang","clang","-O3","-march=native",mode==2?"-UNDEBUG":"-DNDEBUG","-shared","-fPIC",c.c_str(),"-o",(c+".so").c_str(),nullptr);_exit(127);}int status;check(waitpid(p,&status,0)==p&&WIFEXITED(status)&&!WEXITSTATUS(status),"compile");check(!rds_use_compiled(s,(c+".so").c_str()),rds_error(s));}
   check(!rds_bind_host(s,"host",host,&fail),rds_error(s));sims.push_back(s);
  }
  std::mt19937_64 random(15951);unsigned words=(width+63)/64;
  for(unsigned cycle=0;cycle<4000;++cycle){uint64_t controls[]={cycle%193==0,random()%4!=0,random()%4!=0,cycle%400<160?0:random()&1,cycle%400<80?0:random()&1};uint64_t payload[2][16]{};for(auto&row:payload){for(unsigned w=0;w<words;++w)row[w]=random();if(width%64)row[words-1]&=(UINT64_C(1)<<(width%64))-1;}
   for(auto*s:sims){const char*names[]={"reset","push0","push1","pop0","pop1"};for(unsigned i=0;i<5;++i)check(!rds_set_u64(s,rds_find_port(s,names[i]),controls[i]),rds_error(s));for(unsigned i=0;i<2;++i)check(!rds_set(s,rds_find_port(s,("data"+std::to_string(i)).c_str()),payload[i],words),rds_error(s));check(!rds_eval(s),rds_error(s));check(!rds_eval(s),rds_error(s));}
   for(const auto&p:original.metadata["ports"])if(p[0]==1){std::string name=p[2];unsigned n=(original.widths[p[1].get<Id>()]+63)/64;uint64_t expected[16]{},actual[16]{};check(!rds_get(sims[0],rds_find_port(sims[0],name.c_str()),expected,n),"get reference");for(unsigned mode=1;mode<sims.size();++mode){check(!rds_get(sims[mode],rds_find_port(sims[mode],name.c_str()),actual,n),"get candidate");check(std::equal(expected,expected+n,actual),"mismatch "+name+" cycle="+std::to_string(cycle)+" mode="+std::to_string(mode));}}
   if(cycle%97==0){fail=true;for(auto*s:sims)check(rds_advance(s)!=0,"failed host published");fail=false;for(auto*s:sims)check(!rds_eval(s),rds_error(s));}
   for(auto*s:sims)check(!rds_advance(s),rds_error(s));
   if(cycle==2000)for(unsigned mode=2;mode<5;++mode)check(!rds_use_compiled(sims[mode],(base+"-"+std::to_string(mode)+".c.so").c_str()),rds_error(sims[mode]));
  }
  for(auto*s:sims)rds_free(s);
  std::cout<<"PASS width="<<width<<" variant="<<variant<<" layout="<<layout<<std::endl;
 }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
