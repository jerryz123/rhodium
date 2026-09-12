// Compares real routed-queue RTL with generated copy/exchange and independent software kernels.
#include "transport-software.h"
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <algorithm>
#include <chrono>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sys/wait.h>
#include <unistd.h>
using namespace rds;
static const unsigned widths[]={75,244,1024};
static const char* modes[]={"native","exchange","software","software-pool","software-bitmap"};
static void require(bool ok,const std::string&why){if(!ok)throw std::runtime_error(why);}
static Json read(const std::string&p){Json j;std::ifstream(p)>>j;return j;}
static std::string stem(const std::string&dir,unsigned w){return dir+"/"+std::to_string(w);}
static void compile(const std::string&path){pid_t pid=fork();require(pid>=0,"fork");if(!pid){execlp("clang","clang","-O3","-march=native","-DNDEBUG","-shared","-fPIC",path.c_str(),"-o",(path+".so").c_str(),nullptr);_exit(127);}int status;require(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&!WEXITSTATUS(status),"compile failed");}
static void build(const std::string&dir){for(unsigned width:widths)for(std::string mode:modes){auto base=stem(dir,width)+"-"+mode,path=base+"-adapter.c";std::ofstream f(path);
 f<<"/* Adapts routed queues to an identical trace and full-output checksum. */\n#include <stdlib.h>\n#define TRANSPORT_WIDTH "<<width<<"\n";
 if(mode=="software-pool"||mode=="software-bitmap")f<<"#define TRANSPORT_POOL 1\n";
 if(mode=="software-bitmap")f<<"#define TRANSPORT_BITMAP 1\n";
#ifdef RDS_TRANSPORT_TYPED_INPUTS
 f<<"#define RDS_TRANSPORT_TYPED_INPUTS 1\n";
#endif
 f<<"#include \""<<std::filesystem::absolute("sims/native/transport-software.h").string()<<"\"\n";
#ifdef RDS_TRANSPORT_FORCE_INLINE
 f<<"#define TRANSPORT_INLINE inline __attribute__((always_inline))\n";
#else
 f<<"#define TRANSPORT_INLINE inline\n";
#endif
 if(mode=="software"||mode=="software-pool"||mode=="software-bitmap"){
  f<<"void*transport_create(void){transport_state*s=malloc(sizeof(*s));if(s)transport_initialize(s);return s;}\nvoid transport_destroy(void*p){free(p);}\nstatic TRANSPORT_INLINE transport_output step(void*restrict p,const transport_input*restrict i){return transport_step(p,i);}\n";
 }else{
  auto m=Model::read(base+"-optimized.json");require(m.metadata["reads"].empty(),"no synchronous reads in adapter");
  char error[512];rds_options options{1,4290056208u};
#ifdef RDS_TRANSPORT_FORCE_INLINE
  options.flags|=RDS_INLINE_BODIES;
#endif
  auto*s=rds_load_with_options((base+".rsim").c_str(),&options,error,sizeof error);require(s,error);
  require(!rds_emit_c(s,(base+"-generated.c").c_str(),0),rds_error(s));require(!rds_emit_plan(s,(base+"-plan.json").c_str()),rds_error(s));rds_free(s);
  auto plan=read(base+"-plan.json");size_t state_words=1;for(const auto&v:plan["values"])if(!v[2].is_null())state_words=std::max(state_words,v[2].get<size_t>()+(v[0].get<size_t>()+63)/64);
  bool local_state=false;
#ifdef RDS_TRANSPORT_LOCAL_STATE
  local_state=state_words==1&&m.metadata["registers"].size()==1;
#endif
  f<<"#include \""<<base<<"-generated.c\"\n";
  f<<"typedef struct{unsigned bank;_Alignas(64) object_state hot;uint64_t state[2]["<<state_words<<"];";
  for(unsigned id=0;id<m.metadata["memories"].size();++id){const auto&mem=m.metadata["memories"][id];f<<"uint64_t memory_"<<id<<"["<<((mem[0].get<unsigned>()+63)/64)*mem[1].get<unsigned>()<<"];";}
  for(unsigned id=0;id<m.metadata["objects"].size();++id){const auto&o=m.metadata["objects"][id];require(o[0]==1&&o[2]==1&&o[3]==0,"ordinary one-entry queues required");unsigned words=(o[1].get<unsigned>()+63)/64;f<<"uint64_t data_"<<id<<"["<<words<<"],pending_"<<id<<"["<<words<<"];";}
  f<<"rds_object objects["<<m.metadata["objects"].size()+1<<"];char error[512];uint64_t arena[];}adapter;\n";
  auto context=[&](bool bound=false){f<<"adapter*a=p;uint64_t*memories[]={";for(unsigned id=0;id<m.metadata["memories"].size();++id)f<<"a->memory_"<<id<<",";
   f<<"0};unsigned bank=a->bank&1;rds_compiled_context local={.v=a->arena,.q="<<(bound?"current":"a->state[bank]")<<",.next="<<(bound?"next":"a->state[bank^1]")<<",.objects=a->objects,.memories=memories,.error=a->error,.hot=&a->hot};rds_compiled_context*c=&local;\n";
#ifdef RDS_TRANSPORT_BIND_MEMORY
   for(unsigned id=0;id<m.metadata["memories"].size();++id)f<<"a->hot.memory_"<<id<<"=a->memory_"<<id<<";\n";
#endif
  };
  f<<"static inline void put(rds_compiled_context*c,unsigned old,unsigned word,uint64_t x){unsigned p=rds_generated_arena_offset(old);if(rds_generated_arena_small(old))*((unsigned char*)c->v+p)=(unsigned char)x;else c->v[p+word]=x;}\nstatic inline uint64_t get(rds_compiled_context*c,unsigned old,unsigned word){unsigned p=rds_generated_arena_offset(old);return rds_generated_arena_small(old)?*((unsigned char*)c->v+p):c->v[p+word];}\n";
  f<<"void*transport_create(void){size_t bytes=(sizeof(adapter)+rds_generated_arena_words()*8+63)&~(size_t)63;void*p=aligned_alloc(64,bytes);if(!p)return 0;memset(p,0,bytes);";context();
  for(unsigned id=0;id<m.metadata["objects"].size();++id)f<<"a->objects["<<id<<"].data=a->data_"<<id<<";a->objects["<<id<<"].pending=a->pending_"<<id<<";";
  for(const auto&r:m.metadata["registers"]){const Op*init=nullptr;for(const auto&o:m.ops)if(o.out==r[3])init=&o;require(init&&init->code==0,"literal register init required");
   bool found=false;for(const auto&v:plan["values"])if(v[3]==r[0]){require(!v[2].is_null(),"register missing state offset");for(unsigned word=0;word<init->imm.size();++word)f<<"a->state[0]["<<v[2].get<unsigned>()+word<<"]=a->state[1]["<<v[2].get<unsigned>()+word<<"]=UINT64_C("<<init->imm[word]<<");";found=true;}require(found,"missing register");}
  for(const auto&p:m.metadata["ports"])if(p[0]==1)for(const auto&o:m.ops)if(o.out==p[1]&&o.code==0)for(const auto&v:plan["values"])if(v[3]==o.out)for(unsigned w=0;w<o.imm.size();++w)f<<"put(c,"<<v[1]<<","<<w<<",UINT64_C("<<o.imm[w]<<"));";
  f<<"rds_generated_objects(c,false);return a;}\nvoid transport_destroy(void*p){free(p);}\nstatic TRANSPORT_INLINE transport_output "<<(local_state?"step_bound(void*restrict p,const transport_input*restrict i,uint64_t*restrict current,uint64_t*restrict next)":"step(void*restrict p,const transport_input*restrict i)")<<"{";context(local_state);f<<"transport_output out={0};\n";
  auto offset=[&](Id id){for(const auto&v:plan["values"])if(v[3]==id){require(v[2].is_null(),"state port");return v[1].get<unsigned>();}throw std::runtime_error("missing port");};
  for(const auto&p:m.metadata["ports"])if(p[0]==0){std::string name=p[2];require(name=="clock"||name=="reset"||name=="push0"||name=="push1"||name=="pop0"||name=="pop1"||name=="data0"||name=="data1","unknown input "+name);
   for(unsigned w=0;w<(m.widths[p[1].get<Id>()]+63)/64;++w)f<<"put(c,"<<offset(p[1])<<","<<w<<","<<(name=="clock"?"0":"i->"+name+((name=="data0"||name=="data1")?"["+std::to_string(w)+"]":""))<<");\n";}
  for(unsigned phase:{0,1,6})f<<"if(phase_"<<phase<<"_0(c))abort();\n";
  for(const auto&p:m.metadata["ports"])if(p[0]==1){std::string name=p[2];require(name=="valid"||name=="ready"||name=="route"||name=="result0"||name=="result1","unknown output "+name);
   const Op*literal=nullptr;for(const auto&o:m.ops)if(o.out==p[1]&&o.code==0)literal=&o;
   for(unsigned w=0;w<(m.widths[p[1].get<Id>()]+63)/64;++w){f<<"out."<<name<<((name=="result0"||name=="result1")?"["+std::to_string(w)+"]":"")<<"=";if(literal)f<<"UINT64_C("<<literal->imm[w]<<")";else f<<"get(c,"<<offset(p[1])<<","<<w<<")";f<<";\n";}}
  for(unsigned phase:{3,5,4,2})f<<"if(phase_"<<phase<<"_0(c))abort();\n";
  if(local_state){
   f<<"return out;}\nstatic TRANSPORT_INLINE transport_output step(void*p,const transport_input*i){adapter*a=p;unsigned bank=a->bank&1;transport_output out=step_bound(p,i,a->state[bank],a->state[bank^1]);a->bank=bank^1;return out;}\n"
    <<"static TRANSPORT_INLINE transport_output batch_step(void*p,const transport_input*i,uint64_t*current,uint64_t*previous){transport_output out=step_bound(p,i,current,previous);uint64_t old=*current;*current=*previous;*previous=old;return out;}\n"
    <<"#define TRANSPORT_BATCH_ENTER adapter*batch=p;unsigned saved_bank=batch->bank&1;uint64_t current=batch->state[saved_bank][0],previous=batch->state[saved_bank^1][0]\n"
    <<"#define TRANSPORT_BATCH_STEP(p,i) batch_step(p,i,&current,&previous)\n"
    <<"#define TRANSPORT_BATCH_LEAVE unsigned final_bank=saved_bank^((count&repeats)&1);batch->state[final_bank][0]=current;batch->state[final_bank^1][0]=previous;batch->bank=final_bank\n";
  }else f<<"a->bank=bank^1;return out;}\n";
 }
 f<<R"C(
#ifndef TRANSPORT_BATCH_ENTER
#define TRANSPORT_BATCH_ENTER ((void)0)
#define TRANSPORT_BATCH_STEP(p,i) step(p,i)
#define TRANSPORT_BATCH_LEAVE ((void)0)
#endif
transport_output transport_once(void*p,const transport_input*i){return step(p,i);}
uint64_t transport_run(void*restrict p,const transport_input*restrict trace,unsigned count,unsigned repeats){
 TRANSPORT_BATCH_ENTER;
 typedef uint64_t vector __attribute__((vector_size(32)));vector h[2][(TRANSPORT_WORDS+3)/4]={0};uint64_t control=0;
 for(unsigned r=0;r<repeats;++r)for(unsigned i=0;i<count;++i){transport_output o=TRANSPORT_BATCH_STEP(p,trace+i);control=control*UINT64_C(0x9e3779b185ebca87)+o.valid+3*o.ready+7*o.route;
  for(unsigned dst=0;dst<2;++dst)for(unsigned g=0;g<(TRANSPORT_WORDS+3)/4;++g){vector data={0};unsigned words=TRANSPORT_WORDS-4*g;if(words>4)words=4;memcpy(&data,(dst?o.result1:o.result0)+4*g,words*8);h[dst][g]=h[dst][g]*UINT64_C(0x9e3779b185ebca87)+data;}}
 TRANSPORT_BATCH_LEAVE;uint64_t result=control,words[2][((TRANSPORT_WORDS+3)/4)*4];memcpy(words,h,sizeof h);for(unsigned dst=0;dst<2;++dst)for(unsigned w=0;w<TRANSPORT_WORDS;++w)result=(result^words[dst][w])*UINT64_C(0x100000001b3);return result;}
)C";f.close();compile(path);
}}
using Step=transport_output(*)(void*,const transport_input*);using Run=uint64_t(*)(void*,const transport_input*,unsigned,unsigned);
struct Engine{void*lib=nullptr,*state=nullptr;Step step=nullptr;Run run=nullptr;void(*destroy)(void*)=nullptr;
 Engine(const std::string&base,const std::string&mode){lib=dlopen((base+"-"+mode+"-adapter.c.so").c_str(),RTLD_NOW|RTLD_LOCAL);require(lib,"dlopen "+base+mode);auto create=(void*(*)())dlsym(lib,"transport_create");destroy=(void(*)(void*))dlsym(lib,"transport_destroy");step=(Step)dlsym(lib,"transport_once");run=(Run)dlsym(lib,"transport_run");require(create&&destroy&&step&&run,"adapter ABI");state=create();require(state,"allocation");}
 ~Engine(){if(state)destroy(state);if(lib)dlclose(lib);}};
static std::vector<transport_input> trace(unsigned width,unsigned traffic,unsigned count){std::mt19937_64 rng(8192+traffic);std::vector<transport_input> result(count);
 for(unsigned n=0;n<count;++n){auto&i=result[n];for(unsigned w=0;w<(width+63)/64;++w){i.data0[w]=rng();i.data1[w]=rng();}if(width%64){i.data0[width/64]&=(UINT64_C(1)<<(width%64))-1;i.data1[width/64]&=(UINT64_C(1)<<(width%64))-1;}
  i.push0=traffic==0?0:traffic==1?rng()%8==0:1;i.push1=traffic==0?0:traffic==1?rng()%8==0:1;i.pop0=traffic==3?rng()%4==0:1;i.pop1=traffic==3?rng()%4==0:1;i.reset=n==197||n==count-1;}
 return result;}
static void set(rds_sim*s,const transport_input&i,unsigned width){for(const char*name:{"reset","push0","push1","pop0","pop1","data0","data1"}){int p=rds_find_port(s,name);require(p>=0,"input port");int rc;std::string n=name;
 if(n=="data0"||n=="data1")rc=rds_set(s,p,n=="data0"?i.data0:i.data1,(width+63)/64);else rc=rds_set_u64(s,p,n=="reset"?i.reset:n=="push0"?i.push0:n=="push1"?i.push1:n=="pop0"?i.pop0:i.pop1);require(!rc,rds_error(s));}}
static transport_output output(rds_sim*s,unsigned width){transport_output o{};for(const char*name:{"valid","ready","route","result0","result1"}){std::string n=name;int p=rds_find_port(s,name);int rc;if(n=="result0"||n=="result1")rc=rds_get(s,p,n=="result0"?o.result0:o.result1,(width+63)/64);else rc=rds_get_u64(s,p,n=="valid"?&o.valid:n=="ready"?&o.ready:&o.route);require(!rc,rds_error(s));}return o;}
static bool equal(const transport_output&a,const transport_output&b,unsigned width){return a.valid==b.valid&&a.ready==b.ready&&a.route==b.route&&!memcmp(a.result0,b.result0,8*((width+63)/64))&&!memcmp(a.result1,b.result1,8*((width+63)/64));}
static void verify(const std::string&dir){for(unsigned width:widths)for(unsigned traffic=0;traffic<4;++traffic){auto base=stem(dir,width);auto inputs=trace(width,traffic,4096);char error[512];rds_options options{1,RDS_REFERENCE};auto*original=rds_load_with_options((base+"-original.rsim").c_str(),&options,error,sizeof error);require(original,error);
 std::vector<std::unique_ptr<Engine>> engines;for(const char*mode:modes)engines.emplace_back(new Engine(base,mode));unsigned cycle=0;
 for(const auto&i:inputs){set(original,i,width);require(!rds_eval(original),rds_error(original));auto a=output(original,width);for(unsigned e=0;e<engines.size();++e){auto b=engines[e]->step(engines[e]->state,&i);require(equal(a,b,width),"transport mismatch "+std::to_string(width)+" "+modes[e]+" traffic="+std::to_string(traffic)+" cycle="+std::to_string(cycle)+" expected-control="+std::to_string(a.valid)+","+std::to_string(a.route)+" got="+std::to_string(b.valid)+","+std::to_string(b.route));}require(!rds_advance(original),rds_error(original));++cycle;}
 uint64_t hash=0;for(const char*mode:modes){Engine e(base,mode);auto value=e.run(e.state,inputs.data(),inputs.size(),3);if(hash)require(value==hash,"batch checksum mismatch");hash=value;}
 for(const char*mode:modes){Engine batched(base,mode),scalar(base,mode);
  for(unsigned length:{0u,1u,2u,17u,31u})for(unsigned repeats:{0u,1u,2u,3u}){
   (void)batched.run(batched.state,inputs.data(),length,repeats);
   for(unsigned r=0;r<repeats;++r)for(unsigned n=0;n<length;++n)(void)scalar.step(scalar.state,&inputs[n]);
   for(unsigned n=0;n<8;++n){const auto&i=inputs[101+n+length];auto a=batched.step(batched.state,&i),b=scalar.step(scalar.state,&i);require(equal(a,b,width),"batch state restoration "+std::string(mode)+" length="+std::to_string(length));}
  }
 }
 rds_free(original);std::cerr<<"PASS transport width="<<width<<" traffic="<<traffic<<'\n';}}
static void measure(const std::string&dir,unsigned width,unsigned traffic,const std::string&mode,unsigned repeats,unsigned count){require(std::find(std::begin(widths),std::end(widths),width)!=std::end(widths)&&traffic<4&&repeats&&count>197,"invalid measurement");auto inputs=trace(width,traffic,count);Engine e(stem(dir,width),mode);
 auto start=std::chrono::steady_clock::now();auto hash=e.run(e.state,inputs.data(),inputs.size(),repeats);double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();uint64_t cycles=uint64_t(count)*repeats;
 std::cout<<Json({{"width",width},{"traffic",traffic},{"mode",mode},{"cycles",cycles},{"checksum",hash},{"seconds",seconds},{"ns_per_cycle",seconds*1e9/cycles}})<<'\n';}
static void report(const std::string&path){std::ifstream f(path);std::string line;Json groups=Json::object();while(std::getline(f,line)){auto row=Json::parse(line);auto key=std::to_string(row["width"].get<unsigned>())+"-"+std::to_string(row["traffic"].get<unsigned>());auto&g=groups[key];Json signature={row["cycles"],row["checksum"]};if(g.contains("signature"))require(g["signature"]==signature,"measurement signature mismatch");else g["signature"]=signature;g[row["mode"].get<std::string>()].push_back(row["ns_per_cycle"]);}
 require(!groups.empty(),"empty measurements");for(auto&g:groups){for(const char*mode:modes){require(g.contains(mode)&&g[mode].size()>=3,"need three samples per engine");auto v=g[mode].get<std::vector<double>>();std::sort(v.begin(),v.end());g[std::string(mode)+"_median_ns"]=(v[(v.size()-1)/2]+v[v.size()/2])/2;}g["exchange_speedup"]=g["native_median_ns"].get<double>()/g["exchange_median_ns"].get<double>();g["exchange_over_best_software"]=g["exchange_median_ns"].get<double>()/std::min({g["software_median_ns"].get<double>(),g["software-pool_median_ns"].get<double>(),g["software-bitmap_median_ns"].get<double>()});}std::cout<<groups.dump(2)<<'\n';}
int main(int argc,char**argv){try{require(argc>=3,"transport-software build|verify DIR | measure DIR WIDTH TRAFFIC MODE REPEATS [TRACE] | report JSONL");auto dir=std::filesystem::absolute(argv[2]).string();std::string mode=argv[1];if(mode=="build"&&argc==3)build(dir);else if(mode=="verify"&&argc==3)verify(dir);else if(mode=="report"&&argc==3)report(argv[2]);else if(mode=="measure"&&(argc==7||argc==8))measure(dir,std::stoul(argv[3]),std::stoul(argv[4]),argv[5],std::stoul(argv[6]),argc==8?std::stoul(argv[7]):4096);else throw std::runtime_error("invalid command");}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
