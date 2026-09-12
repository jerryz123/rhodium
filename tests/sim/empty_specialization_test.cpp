// Checks semantic specialization across C regions against transactional reference execution.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
using namespace rds;
static void require(bool ok,const std::string&why){if(!ok)throw std::runtime_error(why);}
static int run(std::vector<std::string>args){pid_t p=fork();require(p>=0,"fork");if(!p){std::vector<char*>av;for(auto&a:args)av.push_back(a.data());av.push_back(nullptr);execvp(av[0],av.data());_exit(127);}int status;require(waitpid(p,&status,0)==p,"waitpid");return WIFEXITED(status)?WEXITSTATUS(status):128;}
static std::string read(const std::string&p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
static void write(const std::string&p,const std::string&s){std::ofstream(p)<<s;}
static std::string checksum(std::string code){auto p=code.rfind("/* rds-file-fnv64 ");require(p!=std::string::npos,"checksum footer");code.erase(p);uint64_t h=UINT64_C(14695981039346656037);for(unsigned char c:code)h=(h^c)*UINT64_C(1099511628211);std::ostringstream out;out<<code<<"/* rds-file-fnv64 "<<std::hex<<std::setw(16)<<std::setfill('0')<<h<<" */\n";return out.str();}
static Model fixture(bool pipe,bool state,unsigned held_width,unsigned repetitions){Model m;m.widths={1,1,1,64,1,1,64,2,64,2};m.metadata={{"format","rhodium-simulation-ir-v1"},{"registers",Json::array()},{"memories",Json::array()},{"writes",Json::array()},{"reads",Json::array()},{"assertions",Json::array()},{"objects",Json::array()},{"origins",Json::array()},{"inventory",Json::array()},{"occurrences",{"empty"}}, {"ports",{{0,0,"reset"},{0,1,"enqueue0"},{0,2,"dequeue0"},{0,3,"data0"},{0,4,"enqueue1"},{0,5,"dequeue1"},{0,6,"data1"},{0,7,"accepts"},{0,8,"ambient"},{0,9,"selector"}}}};
 m.metadata["opcodes"]={"constant","copy","not","and","or","xor","add","sub","mul","shl","shru","shrs","eq","ult","slt","mux_lookup","onehot_mux","extract","zext","sext","pack","vector_index","vector_inject","vector_write_set","memory_read_async","decode","set_clear","balance","counter_step","object_query","alu","byte_merge"};
 auto emit=[&](unsigned code,unsigned width,std::vector<Id>a,std::vector<uint64_t>im=std::vector<uint64_t>{}){Id id=m.widths.size();m.widths.push_back(width);m.ops.push_back({code,id,std::move(a),std::move(im)});return id;};
 m.metadata["objects"].push_back({pipe?2:1,64,pipe?3:1,0,{0,1,3,2},"input0"});m.metadata["objects"].push_back({1,64,1,0,{0,4,6,5},"input1"});
 Id valid0=emit(29,1,{}, {0,2}),valid1=emit(29,1,{}, {1,2});Id d0=emit(29,64,{}, {0,3}),d1=emit(29,64,{}, {1,3});
 auto bit=[&](Id data,unsigned i){return emit(17,1,{data},{i});};
 Id req0=emit(20,2,{emit(3,1,{valid0,bit(d0,0)}),emit(3,1,{valid1,bit(d1,0)})});
 Id req1=emit(20,2,{emit(3,1,{valid0,bit(d0,1)}),emit(3,1,{valid1,bit(d1,1)})});
 Id zero2=emit(0,2,{}, {0}),grant0=emit(29,2,{zero2,req0},{2,2}),grant1=emit(29,2,{grant0,req1},{2,3});
 m.metadata["objects"].push_back({10,2,2,32,{0,req0,req1,7},"matcher"});
 Id x=emit(19,64,{pipe?valid0:grant1});if(pipe){Id vector=emit(20,192,{d0,d1,8});Id index=emit(17,1,{9},{0});x=emit(3,64,{x,emit(21,64,{vector,index},{3,64})});}for(unsigned i=0;i<repetitions;++i){x=emit(3,64,{x,d0});x=emit(5,64,{x,emit(3,64,{emit(18,64,{grant0}),d1})});if(pipe)x=emit(3,64,{x,emit(19,64,{valid0})});}
 if(state){
  Id phase=m.widths.size();m.widths.push_back(2);Id held=m.widths.size();m.widths.push_back(held_width);
  m.metadata["registers"].push_back({phase,7,0,zero2});
  auto resized=[&](Id input){return held_width<64?emit(17,held_width,{input},{0}):held_width>64?emit(20,held_width,{input,emit(17,held_width-64,{d1},{0})}):input;};
  m.metadata["registers"].push_back({held,resized(8),0,emit(0,held_width,{},words(0,held_width))});
  Id alternative=resized(d0);
  x=held;for(unsigned i=0;i<48;++i)x=emit(15,held_width,{phase,alternative,x},{0});
  if(held_width<64)x=emit(18,64,{x});
  if(held_width>64)x=emit(5,64,{emit(17,64,{x},{0}),emit(18,64,{emit(17,held_width-64,{x},{64})})});
 }
 Id reg=m.widths.size();m.widths.push_back(64);Id zero=emit(0,64,{}, {0});Id next=emit(3,64,{x,emit(19,64,{valid0})});m.metadata["registers"].push_back({reg,next,0,zero});
 Id payload=emit(3,64,{x,emit(19,64,{valid1})});m.metadata["objects"].push_back({1,64,1,0,{0,1,payload,2},"receiver"});
 Id observed=emit(29,64,{}, {3,3});Id bitzero=emit(0,1,{}, {0});m.metadata["objects"].push_back({8,1,1,0,{bitzero,bitzero,bitzero,bitzero,bitzero},"host"});
 Id onehot=emit(16,64,{9,d0,d1});
 for(auto entry:std::vector<std::pair<Id,std::string>>{{x,"result"},{reg,"state"},{observed,"receiver"},{d0,"preview0"},{d1,"preview1"},{grant0,"grant"},{onehot,"partial"}}){m.metadata["ports"].push_back({1,entry.first,entry.second});}
 m.validate();return m;}
struct Host{bool fail=false;unsigned calls=0;};static int callback(void*p,const uint64_t*,size_t,uint64_t*,size_t){auto*h=static_cast<Host*>(p);++h->calls;return h->fail?-1:0;}
int main(int argc,char**argv){try{require(argc==3||argc==4,"supply output directory and specializer binary [--pipe]");
 bool local=argc==4&&std::string(argv[3])=="--local";
 bool grants=argc==4&&std::string(argv[3])=="--grants";
 bool ancestors=local||grants||(argc==4&&std::string(argv[3])=="--ancestors");
 bool pipe=argc==4&&std::string(argv[3])=="--pipe",state=argc==4&&(std::string(argv[3])=="--state"||std::string(argv[3])=="--state-narrow"||std::string(argv[3])=="--state-wide");require(argc==3||pipe||state||ancestors,"unknown test mode");std::filesystem::create_directories(argv[1]);std::string stem=std::string(argv[1])+"/fixture",tool=argv[2];unsigned held_width=argc==4&&std::string(argv[3])=="--state-narrow"?13:argc==4&&std::string(argv[3])=="--state-wide"?73:64;auto model=fixture(pipe,state,held_width,ancestors?800:48);
 if(grants){auto &matcher=model.metadata["objects"][2];matcher[3]=96;unsigned converted=0;
   for(const auto &o:model.ops)if(o.code==29&&o.imm[0]==2&&o.imm[1]>=2&&o.imm[1]<4){matcher[4][o.imm[1]-1]=o.out;++converted;}
   require(converted==2,"grant fixture requires both step queries");
   model.validate();
 }
 if(local){
  auto emit=[&](unsigned code,unsigned width,std::vector<Id>a,std::vector<uint64_t>im=std::vector<uint64_t>{}){Id id=model.widths.size();model.widths.push_back(width);model.ops.push_back({code,id,std::move(a),std::move(im)});return id;};
  Id parts[2];Id no=emit(0,1,{}, {0});
  for(unsigned source=0;source<2;++source){Id valid=emit(29,1,{}, {source,2}),data=emit(29,64,{}, {source,3});
   Id request=emit(3,1,{valid,emit(17,1,{data},{0})});Id owner=model.metadata["objects"].size();
   model.metadata["objects"].push_back({10,1,1,32,{0,request,request},"local-matcher-"+std::to_string(source)});
   Id x=emit(19,64,{emit(29,1,{no,request},{owner,1})});
   for(unsigned i=0;i<96;++i){x=emit(3,64,{x,data});x=emit(5,64,{x,emit(3,64,{x,8})});}
   parts[source]=x;
  }
  Id result=emit(5,64,{parts[0],parts[1]});model.metadata["ports"].push_back({1,result,"local_result"});model.validate();
 }
 model.write_binary(stem+".rsim");write(stem+".json",model.json().dump());
 std::vector<rds_sim*>sims;std::vector<std::string>libs;Host host;std::string source,plan;
 for(unsigned mode=0;mode<4;++mode){rds_options opt{1,mode?unsigned(1387597840) | (std::getenv("RDS_TEST_LIFT") ? RDS_LIFT_TRANSITIONS | RDS_LIFT_PRIMITIVES : 0u):1u};char error[512];auto*s=rds_load_with_options((stem+".rsim").c_str(),&opt,error,sizeof error);require(s,error);rds_set_strict(s,mode!=3);require(!rds_bind_host(s,"host",callback,&host),rds_error(s));std::string library;
  if(mode){source=stem+"-"+std::to_string(mode)+".c";plan=source+".plan.json";require(!rds_emit_c(s,source.c_str(),4096),rds_error(s));require(!rds_emit_plan(s,plan.c_str()),rds_error(s));if(mode>=2){std::string transformed=source+".optimized.c",report=source+".report.json";std::vector<std::string>args={tool,stem+".json",plan,source,transformed,report};if(pipe){args.push_back("--invalid-pipe");args.push_back("0");}else if(state){args.push_back("--state");args.push_back(std::to_string(model.metadata["registers"][0][0].get<Id>()));args.push_back("0");}else {args.push_back("--partial");if(ancestors)args.push_back("--matcher-ancestors");if(local){args.push_back("--local-guards");args.push_back("64");}}require(!run(args),"specialization failed");Json r=Json::parse(read(report));require(!r["regions"].empty(),"fixture did not select a proof region");if(ancestors){bool selected=false;for(const auto&region:r["regions"])selected|=region.value("ancestor_region",false);require(selected,"fixture must specialize a matcher-dependent region across a C boundary");}if(local){bool narrowed=false;for(const auto&region:r["regions"])if(region.contains("local_guards"))for(const auto&count:region["local_guards"]["owner_counts"])narrowed|=count.get<unsigned>()>0&&count.get<unsigned>()<region["owners"].size();require(narrowed,"local fixture must narrow an owner guard");}source=transformed;}library=source+".so";require(!run({"clang","-std=c17","-O3","-march=native",mode==3?"-DNDEBUG":"-UNDEBUG","-shared","-fPIC",source,"-o",library}),"compile failed");require(!rds_use_compiled(s,library.c_str()),rds_error(s));}sims.push_back(s);libs.push_back(library);}
 std::mt19937_64 rng(0x656d707479);auto set=[&](rds_sim*s,const std::vector<uint64_t>&v){const char*names[]={"reset","enqueue0","dequeue0","data0","enqueue1","dequeue1","data1","accepts","ambient","selector"};for(unsigned i=0;i<v.size();++i)require(!rds_set_u64(s,rds_find_port(s,names[i]),v[i]),rds_error(s));};
 auto eval=[&](const std::vector<uint64_t>&v){std::vector<uint64_t>expected;unsigned before=host.calls;for(unsigned i=0;i<sims.size();++i){auto*s=sims[i];set(s,v);require(!rds_eval(s),rds_error(s));std::vector<uint64_t>actual;for(const char*name:{"result","state","receiver","preview0","preview1","grant","partial"}){uint64_t x;require(!rds_get_u64(s,rds_find_port(s,name),&x),rds_error(s));actual.push_back(x);}if(local){uint64_t extra;require(!rds_get_u64(s,rds_find_port(s,"local_result"),&extra),rds_error(s));actual.push_back(extra);}if(!i)expected=actual;else require(expected==actual,"specialized state/preview diverged");}require(host.calls==before,"eval published host side effects");};
 for(unsigned cycle=0;cycle<512;++cycle){std::vector<uint64_t>v={cycle%31==0,rng()&1,rng()&1,rng(),rng()&1,rng()&1,rng(),rng()&3,rng(),1u<<(rng()&1)};if(cycle%8<3){v[1]=v[4]=0;v[2]=v[5]=1;}eval(v);v[8]^=UINT64_MAX;eval(v);eval(v);if(cycle%11==0){host.fail=true;for(unsigned i=0;i<sims.size();++i){require(rds_advance(sims[i])!=0,"failed host published");if(cycle%22==0&&!libs[i].empty())require(!rds_use_compiled(sims[i],libs[i].c_str()),rds_error(sims[i]));}host.fail=false;v[0]=!v[0];v[3]^=UINT64_MAX;eval(v);}for(auto*s:sims)require(!rds_advance(s),rds_error(s));}
 // A diagnostic-only partial operation remains observable when all inputs empty.
 std::vector<uint64_t>empty={1,0,1,UINT64_MAX,0,1,UINT64_MAX,0,0,1};eval(empty);for(auto*s:sims)require(!rds_advance(s),rds_error(s));empty[0]=0;empty[9]=3;for(unsigned i=0;i<sims.size();++i){set(sims[i],empty);require((rds_eval(sims[i])!=0)==(i!=3),"strict partial diagnostic changed");}
 for(auto*s:sims)rds_free(s);
 std::string good=stem+"-1.c",goodplan=good+".plan.json";
 auto rejected=[&](const std::string&m,const std::string&p,const std::string&c){require(run({tool,m,p,c,stem+"-rejected.c",stem+"-rejected.json","--partial"})!=0,"malformed provenance accepted");};
 write(stem+"-bad.c",read(good)+"\n");rejected(stem+".json",goodplan,stem+"-bad.c");
 Json wrong=model.json();wrong["ports"][0][2]="changed-model";write(stem+"-wrong.json",wrong.dump());rejected(stem+"-wrong.json",goodplan,good);
 Json wrongplan=Json::parse(read(goodplan));wrongplan["compiled_key"]="0000000000000000";write(stem+"-wrong-plan.json",wrongplan.dump());rejected(stem+".json",stem+"-wrong-plan.json",good);
 std::string malformed=read(good);auto store=malformed.find("/* rds-store ");require(store!=std::string::npos,"missing store fixture");auto target=malformed.find('\n',store)+1;malformed.replace(target,1,"q");write(stem+"-storage.c",checksum(malformed));rejected(stem+".json",goodplan,stem+"-storage.c");
 malformed=read(good);auto complete=malformed.find("/* rds-complete 0 ");require(complete!=std::string::npos,"missing completion fixture");malformed.replace(complete,18,"/* rds-complete 1 ");write(stem+"-mapping.c",checksum(malformed));rejected(stem+".json",goodplan,stem+"-mapping.c");
 malformed=read(good);auto region=malformed.find("/* rds-region "),region_end=malformed.find(" */",region);require(region!=std::string::npos&&region_end!=std::string::npos,"missing region fixture");
 Json region_info=Json::parse(malformed.substr(region+14,region_end-region-14));region_info["operations"][0][2]=region_info["operations"][0][2].get<unsigned>()+1;malformed.replace(region+14,region_end-region-14,region_info.dump());write(stem+"-width.c",checksum(malformed));rejected(stem+".json",goodplan,stem+"-width.c");
 malformed=read(good);region=malformed.find("/* rds-region ");region_end=malformed.find(" */",region);region_info=Json::parse(malformed.substr(region+14,region_end-region-14));region_info["operations"]=Json::array();malformed.replace(region+14,region_end-region-14,region_info.dump());write(stem+"-empty.c",checksum(malformed));rejected(stem+".json",goodplan,stem+"-empty.c");
 require(run({tool,stem+".json",goodplan,good,stem+"/missing/output.c",stem+"-unwritable.json","--partial"})!=0,"unwritable source accepted");
 require(run({tool,stem+".json",goodplan,good,stem+"-unwritable.c",stem+"/missing/report.json","--partial"})!=0,"unwritable report accepted");
 if(local)for(const std::string span:{"7","257"})require(run({tool,stem+".json",goodplan,good,stem+"-bad-local.c",stem+"-bad-local.json","--partial","--local-guards",span})!=0,"invalid local guard span accepted");
 if(pipe){
  require(run({tool,stem+".json",goodplan,good,stem+"-bad-option.c",stem+"-bad-option.json","--invalid-pipe","1"})!=0,"FIFO accepted as PIPE");
  malformed=read(good);auto p=malformed.find("/* rds-provenance "),e=malformed.find(" */",p);Json manifest=Json::parse(malformed.substr(p+18,e-p-18));
  require(!manifest["query_views"].empty(),"missing PIPE view");manifest["query_views"][0][2]="h->valid_0[0]";
  malformed.replace(p+18,e-p-18,manifest.dump());write(stem+"-bad-view.c",checksum(malformed));
  require(run({tool,stem+".json",goodplan,stem+"-bad-view.c",stem+"-bad-option.c",stem+"-bad-option.json","--invalid-pipe","0"})!=0,"wrong PIPE stage accepted");
 }
 if(state){
  require(run({tool,stem+".json",goodplan,good,stem+"-bad-state.c",stem+"-bad-state.json","--state","0","0"})!=0,"input port accepted as immutable FF");
  require(run({tool,stem+".json",goodplan,good,stem+"-bad-state.c",stem+"-bad-state.json","--state",std::to_string(model.metadata["registers"][0][0].get<Id>()),"4"})!=0,"out-of-width state condition accepted");
  Json bad=Json::parse(read(goodplan));Id root=model.metadata["registers"][1][0];bool changed=false;
  for(auto &value:bad["values"])if(value[3]==root&&!value[2].is_null()){value[2]=value[2].get<unsigned>()+1;changed=true;break;}
  require(changed,"missing immutable state binding");write(stem+"-bad-state-plan.json",bad.dump());
  require(run({tool,stem+".json",stem+"-bad-state-plan.json",good,stem+"-bad-state.c",stem+"-bad-state.json","--state",std::to_string(model.metadata["registers"][0][0].get<Id>()),"0"})!=0,"altered state offset accepted");
 }
 std::cout<<"PASS: four engines, snapshots/invalid previews, all store banks, reset/retry/reattach, strict diagnostics and artifact/output rejections\n";
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
