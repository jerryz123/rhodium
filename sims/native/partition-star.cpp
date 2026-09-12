// Measures eight parallel clients sharing an arbitrated elastic service pipeline.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sys/wait.h>
#include <unistd.h>
using namespace rds;
static void require(bool b,const std::string &s){if(!b)throw std::runtime_error(s);}
static constexpr unsigned default_policy=4088747088u|RDS_SPIN;
static unsigned policy(const std::string&dir){unsigned value=default_policy;std::ifstream(dir+"/policy")>>value;return value;}
static void command(std::vector<std::string> args){pid_t p=fork();require(p>=0,"fork");if(!p){std::vector<char*> a;for(auto&s:args)a.push_back(s.data());a.push_back(nullptr);execvp(a[0],a.data());_exit(127);}int status=0;require(waitpid(p,&status,0)==p&&WIFEXITED(status)&&!WEXITSTATUS(status),"compiler failed");}
struct Builder {
 Model m;
 Builder(){m.metadata={{"format","rhodium-simulation-ir-v1"}};for(auto k:{"ports","registers","objects","memories","writes","reads","assertions","origins","inventory"})m.metadata[k]=Json::array();}
 Id value(unsigned w){Id v=m.widths.size();m.widths.push_back(w);return v;}
 Id op(unsigned c,unsigned w,std::vector<Id>a={},std::vector<uint64_t>im={}){Id v=value(w);m.ops.push_back({c,v,std::move(a),std::move(im)});return v;}
 Id lit(uint64_t x,unsigned w=64){return op(0,w,{}, {x});}
 Id query(unsigned o,unsigned q,unsigned w=1,std::vector<Id>a={}){return op(29,w,std::move(a),{o,q});}
 Id bin(unsigned c,Id a,Id b){return op(c,m.widths[a],{a,b});}
 Id land(Id a,Id b){return op(3,1,{a,b});}
 Id mux(Id s,Id yes,Id no){return op(15,m.widths[yes],{s,no,yes},{1});}
 void out(Id v,const std::string&name){m.metadata["ports"].push_back({1,v,name});}
 Model build(unsigned work){
  Id reset=value(1),traffic=value(2);m.metadata["ports"]={{0,reset,"reset"},{0,traffic,"traffic"}};
  Id zero=lit(0),one=lit(1),no=lit(0,1),tick=value(64);
  m.metadata["registers"].push_back({tick,bin(6,tick,one),reset,zero});
  std::array<Id,8> q{},sum{},sent{},recv{},rv{},rd{},rr{},sv{},sd{},sr{},enable{},consume{},packet{};
  for(unsigned i=0;i<8;++i){q[i]=value(64);sum[i]=value(64);sent[i]=value(64);recv[i]=value(64);
   rv[i]=query(i,2);rd[i]=query(i,3,64);rr[i]=query(i,1);
   sv[i]=query(8+i,2);sd[i]=query(8+i,3,64);sr[i]=query(8+i,1);
   Id phase=bin(6,tick,lit(i));
   Id sparse=op(12,1,{bin(3,phase,lit(31)),zero});
   enable[i]=mux(op(12,1,{traffic,lit(0,2)}),no,mux(op(12,1,{traffic,lit(1,2)}),sparse,lit(1,1)));
   consume[i]=op(2,1,{op(12,1,{bin(3,phase,lit(7)),zero})});
   Id x=q[i];for(unsigned j=0;j<work;++j)x=bin(8,bin(5,x,bin(10,x,lit(13))),lit(UINT64_C(0x9e3779b185ebca87)+2*j));
   packet[i]=bin(4,bin(3,x,lit(~UINT64_C(7))),lit(i));
  }
  Id pipevalid=query(16,2),pipedata=query(16,3,64),destination=bin(3,pipedata,lit(7));
  Id downstream=no;std::array<Id,8> offered{};
  for(unsigned i=0;i<8;++i){Id dest=op(12,1,{destination,lit(i)});offered[i]=land(pipevalid,dest);downstream=op(4,1,{downstream,land(dest,sr[i])});}
  Id pipeready=query(16,1,1,{downstream});
  Id requests=op(20,8,std::vector<Id>(rv.begin(),rv.end()));
  Id grant=query(17,0,8,{requests}),any=op(2,1,{op(12,1,{grant,lit(0,8)})});
  Id chosen=op(16,64,{mux(any,grant,lit(1,8)),rd[0],rd[1],rd[2],rd[3],rd[4],rd[5],rd[6],rd[7]});
  for(unsigned i=0;i<8;++i){Id push=land(enable[i],rr[i]),pop=land(sv[i],consume[i]);
   m.metadata["registers"].push_back({q[i],mux(push,bin(6,q[i],lit(8)),q[i]),reset,lit(8*(i+1))});
   m.metadata["registers"].push_back({sum[i],mux(pop,bin(5,bin(8,sum[i],lit(UINT64_C(0x9e3779b185ebca87))),sd[i]),sum[i]),reset,zero});
   m.metadata["registers"].push_back({sent[i],bin(6,sent[i],op(18,64,{push})),reset,zero});
   m.metadata["registers"].push_back({recv[i],bin(6,recv[i],op(18,64,{pop})),reset,zero});
   Id taken=land(pipeready,op(17,1,{grant},{i}));
   m.metadata["objects"].push_back({1,64,2,0,{reset,enable[i],packet[i],taken},"star/client"+std::to_string(i)+"/request"});
   out(q[i],"q"+std::to_string(i));out(sum[i],"sum"+std::to_string(i));out(sent[i],"sent"+std::to_string(i));out(recv[i],"recv"+std::to_string(i));
   out(rv[i],"request_valid"+std::to_string(i));out(mux(rv[i],rd[i],zero),"request_data"+std::to_string(i));
   out(sv[i],"response_valid"+std::to_string(i));out(mux(sv[i],sd[i],zero),"response_data"+std::to_string(i));
  }
  for(unsigned i=0;i<8;++i)m.metadata["objects"].push_back({1,64,2,0,{reset,offered[i],pipedata,consume[i]},"star/client"+std::to_string(i)+"/response"});
  m.metadata["objects"].push_back({2,64,4,0,{reset,any,chosen,downstream},"star/shared_service/pipe"});
  m.metadata["objects"].push_back({10,8,1,32,{reset,requests,land(any,pipeready)},"star/shared_service/arbiter"});
  out(pipevalid,"service_valid");out(mux(pipevalid,pipedata,zero),"service_data");out(grant,"grant");out(tick,"tick");
  m.validate();optimize_body(m);m.validate();return m;
 }
};
#include "partition-mesh.hpp"
// Close a retained, actually elaborated SimpleRouter around stateful sources and
// periodically stalled sinks. The router graph and state owners are unchanged.
static Model noc_fixture(const std::string&source,Model&debug){
 Builder b;b.m=Model::read(source);auto original=b.m.ops;auto ports=b.m.metadata["ports"];b.m.ops.clear();
 Id reset=none;std::map<std::string,Id> names;for(const auto&p:ports){names[p[2].get<std::string>()]=p[1];if(p[2]=="reset")reset=p[1];}
 require(reset!=none,"router reset missing");Id traffic=b.value(2),tick=b.value(64),zero=b.lit(0),one=b.lit(1);
 b.m.metadata["ports"]={{0,reset,"reset"},{0,traffic,"traffic"}};
 b.m.metadata["registers"].push_back({tick,b.bin(6,tick,one),reset,zero});
 struct Source{Id q,valid,ready,pattern;unsigned index;};std::vector<Source> sources;
 auto drive=[&](Id id,Id v){require(b.m.widths[id]==b.m.widths[v],"router input width");b.m.ops.push_back({1,id,{v},{}});};
 for(const auto&p:ports){std::string name=p[2];Id id=p[1];if(p[0]==1){b.m.metadata["ports"].push_back(p);continue;}if(name=="reset")continue;
  if(name=="clock"){drive(id,b.lit(0,1));continue;}
  if(name.rfind("ingress_",0)==0&&name.find("_in")!=std::string::npos){unsigned i=std::stoul(name.substr(8));require(i<11&&b.m.widths[id]==245,"requires retained 11-port 244-bit dat router");
   Id q=b.value(64),valid=b.value(1),phase=b.bin(6,tick,b.lit(i));
   Id pattern=b.mux(b.op(12,1,{traffic,b.lit(2,2)}),b.lit(1,1),b.land(b.op(12,1,{traffic,b.lit(1,2)}),b.op(12,1,{b.bin(3,phase,b.lit(31)),zero})));
   // These are the legal global route keys for this retained site's origins.
   constexpr unsigned keys[11]={0,1,5,6,7,8,9,12,13,14,15};
   Id payload=b.op(18,240,{b.op(20,80,{q,b.lit(i,16)})}),route=b.lit(keys[i],4);
   if(i==1)route=b.bin(6,b.op(17,4,{b.bin(3,q,b.lit(3))},{0}),b.lit(1,4));
   if(i==6){Id sel=b.op(17,2,{q},{0});route=b.op(15,4,{sel,b.lit(9,4),b.lit(10,4),b.lit(11,4)},{1,2});}
   drive(id,b.op(20,245,{payload,route,valid}));
   auto it=names.find("ingress_"+std::to_string(i)+"_out");require(it!=names.end()&&b.m.widths[it->second]==1,"router ready port");sources.push_back({q,valid,it->second,pattern,i});continue;
  }
  if(name.rfind("target_",0)==0&&name.find("_in")!=std::string::npos){unsigned i=std::stoul(name.substr(7));require(b.m.widths[id]==2,"router target width");Id ready=b.op(2,1,{b.op(12,1,{b.bin(3,b.bin(6,tick,b.lit(i)),b.lit(7)),zero})});drive(id,b.op(20,2,{ready,ready}));continue;}
  throw std::runtime_error("unhandled router input "+name);
 }
 require(sources.size()==11,"requires eleven live router sources");
 b.m.ops.insert(b.m.ops.end(),original.begin(),original.end());
 for(const auto&s:sources){Id fire=b.land(s.valid,s.ready);b.m.metadata["registers"].push_back({s.q,b.mux(fire,b.bin(6,s.q,one),s.q),reset,zero});
  Id hold=b.land(s.valid,b.op(2,1,{s.ready}));b.m.metadata["registers"].push_back({s.valid,b.op(4,1,{hold,s.pattern}),reset,b.lit(0,1)});b.out(s.q,"accepted"+std::to_string(s.index));}
 b.out(tick,"tick");b.m.validate();debug=b.m;share_decoders(b.m);share_matchers(b.m);optimize_body(b.m);recover_words(b.m);regroup_bits(b.m);optimize_body(b.m);release_body(b.m);pack_matchers(b.m);b.m.validate();return b.m;
}
struct Queue {uint64_t data[2]{};unsigned head=0,tail=0,count=0;
 uint64_t front()const{return data[head];}
 void step(bool reset,bool valid,uint64_t bits,bool ready){bool push=valid&&count<2,pop=ready&&count; if(push){data[tail]=bits;tail^=1;}if(pop)head^=1;count+=unsigned(push)-unsigned(pop);if(reset)head=tail=count=0;}
};
struct Software {
 std::array<Queue,8> req{},rsp{};std::array<uint64_t,8> q{},sum{},sent{},recv{};std::array<uint64_t,4> data{};std::array<bool,4> valid{};uint64_t tick=0;unsigned priority=0,work;
 explicit Software(unsigned w):work(w){}
 unsigned grant()const{for(unsigned n=0;n<8;++n){unsigned i=(priority+n)&7;if(req[i].count)return 1u<<i;}return 0;}
 std::vector<uint64_t> outputs()const{std::vector<uint64_t> o;for(unsigned i=0;i<8;++i){for(uint64_t x:{q[i],sum[i],sent[i],recv[i],uint64_t(req[i].count!=0),req[i].count?req[i].front():0,uint64_t(rsp[i].count!=0),rsp[i].count?rsp[i].front():0})o.push_back(x);}o.insert(o.end(),{uint64_t(valid[3]),valid[3]?data[3]:0,grant(),tick});return o;}
 void step(bool reset,unsigned traffic){unsigned g=grant(),dest=data[3]&7;bool downstream=rsp[dest].count<2;std::array<bool,4> ready;bool r=downstream;for(unsigned s=4;s--;){ready[s]=r=!valid[s]||r;}
  uint64_t chosen=g?req[__builtin_ctz(g)].front():req[0].front();
  for(unsigned i=0;i<8;++i){bool enable=traffic==2||(traffic==1&&((tick+i)&31)==0),consume=((tick+i)&7)!=0;
   bool push=enable&&req[i].count<2,pop=consume&&rsp[i].count;uint64_t x=q[i];for(unsigned j=0;j<work;++j)x=(x^(x>>13))*(UINT64_C(0x9e3779b185ebca87)+2*j);
   if(pop){sum[i]=sum[i]*UINT64_C(0x9e3779b185ebca87)^rsp[i].front();++recv[i];}
   if(push){q[i]+=8;++sent[i];}
   req[i].step(reset,enable,(x&~UINT64_C(7))|i,(g>>i&1)&&ready[0]);rsp[i].step(reset,valid[3]&&dest==i,data[3],consume);
   if(reset){q[i]=8*(i+1);sum[i]=sent[i]=recv[i]=0;}
  }
  for(unsigned s=4;s--;){bool v=s?valid[s-1]:g!=0;if(ready[s]){if(v)data[s]=s?data[s-1]:chosen;valid[s]=v;}if(reset)valid[s]=false;}
  if(g&&ready[0])priority=(__builtin_ctz(g)+1)&7;if(reset)priority=0;tick=reset?0:tick+1;
 }
};
struct Engine {
 rds_sim*s=nullptr;
 Engine(const std::string&dir,unsigned workers,bool reference=false){char error[512];rds_options o{workers,reference?unsigned(RDS_REFERENCE):policy(dir)};std::string file=reference&&std::filesystem::exists(dir+"/original.rsim")?"/original.rsim":"/model.rsim";s=rds_load_with_options((dir+file).c_str(),&o,error,sizeof error);require(s,error);rds_set_strict(s,reference);if(!reference)require(!rds_use_compiled(s,(dir+"/w"+std::to_string(workers)+".so").c_str()),rds_error(s));}
 ~Engine(){rds_free(s);}
 void inputs(bool reset,unsigned traffic){require(!rds_set_u64(s,0,reset)&&!rds_set_u64(s,1,traffic),rds_error(s));}
 std::vector<uint64_t> outputs(){require(!rds_eval(s),rds_error(s));std::vector<uint64_t> out;for(unsigned p=2;p<rds_get_stats(s).ports;++p){size_t first=out.size(),words=(rds_port_width(s,p)+63)/64;out.resize(first+words);require(!rds_get(s,p,out.data()+first,words),rds_error(s));}return out;}
 void step(){require(!rds_advance(s),rds_error(s));}
};
static void build(const std::string&dir,unsigned work,const std::string&source="",unsigned side=0){std::filesystem::create_directories(dir);Model debug;auto m=side?mesh_fixture(side):source.empty()?Builder().build(work):noc_fixture(source,debug);(source.empty()?m:debug).write_binary(dir+"/original.rsim");
 if(const char*reuse=getenv("RDS_PARTITION_REUSE_GRANTS")){require(std::string(reuse)=="0"||std::string(reuse)=="1","reuse grants must be 0 or 1");if(*reuse=='1')reuse_matcher_grants(m);}
 if(const char*split=getenv("RDS_PARTITION_SPLIT_MATCHERS")){require(std::string(split)=="0"||std::string(split)=="1","split matchers must be 0 or 1");if(*split=='1')split_matcher_columns(m);}
 m.write_binary(dir+"/model.rsim");std::ofstream(dir+"/model.json")<<m.json();std::ofstream(dir+"/work")<<work;std::ofstream(dir+"/kind")<<(side?"mesh":source.empty()?"star":"noc");if(side)std::ofstream(dir+"/side")<<side;
 uint64_t selected=default_policy;if(const char*v=getenv("RDS_PARTITION_FLAGS"))selected=std::stoull(v);require(selected<=UINT32_MAX,"partition flags exceed 32 bits");std::ofstream(dir+"/policy")<<selected;
 for(unsigned n:{1,2,4,8}){char error[512];rds_options o{n,policy(dir)};auto*s=rds_load_with_options((dir+"/model.rsim").c_str(),&o,error,sizeof error);require(s,error);std::string base=dir+"/w"+std::to_string(n);require(!rds_emit_c(s,(base+".c").c_str(),0),rds_error(s));require(!rds_emit_plan(s,(base+".json").c_str()),rds_error(s));auto stats=rds_get_stats(s);std::cerr<<"workers "<<n<<" actual "<<stats.workers<<" operations "<<stats.operations<<'\n';rds_free(s);command({"clang","-O3","-march=native","-DNDEBUG","-fPIC","-shared",base+".c","-o",base+".so"});}
}
static void verify_mesh(const std::string&dir){unsigned side=0;require(bool(std::ifstream(dir+"/side")>>side),"missing mesh geometry");
 for(unsigned n:{1,2,4,8}){Engine native(dir,n);MeshSoftware sw(side);std::unique_ptr<Engine> reference;if(side<=4)reference=std::make_unique<Engine>(dir,1,true);std::set<std::pair<unsigned,uint64_t>> delivered;
  for(unsigned t=0;t<1000+side*side*32;++t){bool reset=t<2||t==507;unsigned traffic=t<750?2:t<1000?1:0;native.inputs(reset,traffic);if(reference)reference->inputs(reset,traffic);auto expected=sw.outputs(),actual=native.outputs();if(actual!=expected){for(unsigned i=0;i<actual.size();++i)if(actual[i]!=expected[i]){std::cerr<<"mesh cycle="<<t<<" word="<<i<<" actual="<<actual[i]<<" expected="<<expected[i]<<'\n';break;}throw std::runtime_error("mesh software mismatch");}if(reference)require(actual==reference->outputs(),"mesh reference mismatch");
   if(reset)delivered.clear();else for(unsigned dest=0;dest<side*side;++dest)if(actual[dest*8+3]){uint64_t seq=actual[dest*8+4],meta=actual[dest*8+5];unsigned src=meta&65535;require((meta>>16)==dest&&src<side*side,"mesh misroute");require(delivered.insert({src,seq}).second,"duplicate mesh delivery");}
   native.step();sw.step(reset,traffic);if(reference)reference->step();
  }
  auto final=native.outputs();uint64_t sent=0,received=0;for(unsigned i=0;i<side*side;++i){require(final[i*8]>10&&final[i*8+1]>0,"idle mesh endpoint");sent+=final[i*8];received+=final[i*8+1];}require(sent==received&&received==delivered.size(),"mesh failed to drain");
  for(unsigned block=0;block<40;++block){unsigned count=std::array<unsigned,5>{0,1,2,7,31}[block%5],traffic=block%3;bool reset=block==19;native.inputs(reset,traffic);if(reference)reference->inputs(reset,traffic);require(!rds_advance_cycles(native.s,count),rds_error(native.s));for(unsigned c=0;c<count;++c){sw.step(reset,traffic);if(reference)reference->step();}require(native.outputs()==sw.outputs(),"mesh bulk software mismatch");if(reference)require(native.outputs()==reference->outputs(),"mesh bulk reference mismatch");}
  std::cerr<<"PASS mesh "<<side<<'x'<<side<<" workers="<<n<<" cycle outputs, reset, stalls, route/unique delivery, drain, bulk\n";
 }
}
static void verify_noc(const std::string&dir){for(unsigned n:{1,2,4,8}){Engine native(dir,n),reference(dir,1,true);std::array<uint64_t,11> received{},targets{};
 for(unsigned t=0;t<1800;++t){bool reset=t<2||t==707;unsigned traffic=t<1600?(t/200)%3:0;native.inputs(reset,traffic);reference.inputs(reset,traffic);auto outputs=native.outputs();require(outputs==reference.outputs(),"NoC complete output mismatch cycle "+std::to_string(t));
  if(reset){received.fill(0);targets.fill(0);}else for(unsigned p=0;p<11;++p){uint64_t data[4]{};require(!rds_get(native.s,rds_find_port(native.s,("target_"+std::to_string(p)+"_out").c_str()),data,4),rds_error(native.s));if((data[3]>>52&1)&&((outputs.back()+p)&7)){unsigned src=data[1]&65535;require(src<11&&data[0]==received[src],"NoC lost, duplicate or reordered packet");++received[src];++targets[p];}}
  native.step();reference.step();}
 auto final=native.outputs();for(unsigned i=0;i<11;++i){require(received[i]>5&&targets[i]>0,"idle NoC source or target");require(received[i]==final[final.size()-12+i],"NoC failed to drain");}
 for(unsigned block=0;block<30;++block){unsigned count=std::array<unsigned,5>{0,1,2,7,31}[block%5],traffic=block%3;bool reset=block==19;native.inputs(reset,traffic);reference.inputs(reset,traffic);require(!rds_advance_cycles(native.s,count),rds_error(native.s));for(unsigned c=0;c<count;++c)reference.step();require(native.outputs()==reference.outputs(),"NoC bulk mismatch");}
 std::cerr<<"PASS real dat NoC workers="<<n<<" debug route assertions, complete outputs, all sources/targets, ordered delivery, drain, bulk\n";}}
static void verify(const std::string&dir,unsigned work){for(unsigned n:{1,2,4,8}){Engine native(dir,n),reference(dir,1,true);Software sw(work);for(unsigned t=0;t<3000;++t){bool reset=t<2||t==707||t==2019;unsigned traffic=(t/250)%3;native.inputs(reset,traffic);reference.inputs(reset,traffic);auto expected=sw.outputs();auto actual=native.outputs();auto ref=reference.outputs();require(actual==ref,"native/reference mismatch cycle "+std::to_string(t));if(actual!=expected){for(unsigned i=0;i<actual.size();++i)if(actual[i]!=expected[i])std::cerr<<"port "<<i<<" native="<<actual[i]<<" sw="<<expected[i]<<'\n';throw std::runtime_error("software mismatch cycle "+std::to_string(t));}native.step();reference.step();sw.step(reset,traffic);}std::cerr<<"PASS workers="<<n<<" reset, contention, empty, sparse, saturated, backpressure\n";
 for(unsigned block=0;block<120;++block){unsigned count=std::array<unsigned,7>{0,1,2,3,7,16,129}[block%7],traffic=block%3;bool reset=block%19==0;native.inputs(reset,traffic);reference.inputs(reset,traffic);if(block&1)native.outputs();require(!rds_advance_cycles(native.s,count),rds_error(native.s));for(unsigned c=0;c<count;++c){reference.step();sw.step(reset,traffic);}require(native.outputs()==reference.outputs()&&native.outputs()==sw.outputs(),"bulk state or bank orientation mismatch");}
 std::cerr<<"PASS workers="<<n<<" bulk odd/even/empty blocks, input changes and cached evaluation\n";
}}
static void measure(const std::string&dir,unsigned work,const std::string&kind,const std::string&mode,unsigned workers,uint64_t cycles,unsigned traffic){require(cycles&&traffic<=2,"invalid cycles or traffic");std::vector<uint64_t> signature;double seconds;unsigned actual=1;uint64_t ops=0,replicas=0;std::unique_ptr<Engine> engine;Software sw(work);
 if(mode=="native"||mode=="bulk"){engine=std::make_unique<Engine>(dir,workers);engine->inputs(true,traffic);engine->step();engine->inputs(false,traffic);actual=rds_get_stats(engine->s).workers;auto e=rds_get_execution_stats(engine->s);ops=e.scheduled_operations;replicas=e.replicated_operations;}else require(mode=="software"&&workers==1,"software uses one worker");
 if(!engine){require(kind=="star","software oracle covers star only");sw.step(true,traffic);}
 auto start=std::chrono::steady_clock::now();if(mode=="bulk")require(!rds_advance_cycles(engine->s,cycles),rds_error(engine->s));else if(engine){for(uint64_t c=0;c<cycles;++c)engine->step();}else for(uint64_t c=0;c<cycles;++c)sw.step(false,traffic);
 seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();signature=engine?engine->outputs():sw.outputs();if(kind=="star"){for(unsigned i=0;i<8;++i)require(traffic==0||signature[i*8+3]>0,"idle client");}else if(kind=="mesh"){for(unsigned i=0;i<signature.size()/8;++i)require(traffic==0||(signature[i*8]>cycles/1024&&signature[i*8+1]>cycles/1024),"stalled mesh endpoint");}else{for(unsigned i=0;i<11;++i)require(traffic==0||signature[signature.size()-12+i]>cycles/128,"stalled NoC input");}
 std::cout<<Json({{"kind",kind},{"nodes",kind=="mesh"?signature.size()/8:0},{"mode",mode},{"requested_workers",workers},{"actual_workers",actual},{"cycles",cycles},{"traffic",traffic},{"work",work},{"seconds",seconds},{"ns_per_cycle",seconds*1e9/cycles},{"scheduled_operations",ops},{"replicas",replicas},{"signature",signature}})<<'\n';
}
int main(int argc,char**argv){try{require(argc>=3,"partition-star build DIR WORK | build-noc DIR SOURCE | build-mesh DIR SIDE | verify DIR | measure DIR native|bulk|software WORKERS CYCLES TRAFFIC");std::string mode=argv[1],dir=std::filesystem::absolute(argv[2]);unsigned work=0;if(mode=="build"){require(argc==4,"build arguments");work=std::stoul(argv[3]);require(work<=256,"work exceeds fixture limit");build(dir,work);}else if(mode=="build-noc"&&argc==4)build(dir,0,argv[3]);else if(mode=="build-mesh"&&argc==4)build(dir,0,"",std::stoul(argv[3]));else{require(bool(std::ifstream(dir+"/work")>>work),"missing work file");std::string kind;require(bool(std::ifstream(dir+"/kind")>>kind),"missing fixture kind");if(mode=="verify"&&argc==3){if(kind=="noc")verify_noc(dir);else if(kind=="mesh")verify_mesh(dir);else verify(dir,work);}else if(mode=="measure"&&argc==7)measure(dir,work,kind,argv[3],std::stoul(argv[4]),std::stoull(argv[5]),std::stoul(argv[6]));else throw std::runtime_error("invalid arguments");}}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
