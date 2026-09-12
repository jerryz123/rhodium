// Builds, verifies and times actual interconnected IR against copied and pooled software.
#include "payload-software.h"
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <algorithm>
#include <chrono>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
using namespace rds;
#if defined(RDS_PAYLOAD_BOUND_STATE) && (!defined(RDS_PAYLOAD_STATIC_STORAGE) || defined(RDS_PAYLOAD_LOCAL_STATE))
#error "Bound state requires static storage and is independent of escaped local state"
#endif
struct Shape{unsigned stages,width;};
static const Shape shapes[]={{2,244},{8,244},{16,244},{8,1024}};
#ifdef RDS_TOKEN_BENCHMARK
static const char*engines[]={"native","lifetime","software","software-ring"};
static const char*candidate="lifetime";
#else
static const char*engines[]={"native","pool","software","software-pool"};
static const char*candidate="pool";
#endif
static void require(bool ok,const std::string&why){if(!ok)throw std::runtime_error(why);}
static unsigned argument(const char*text){size_t end=0;std::string value=text;require(!value.empty()&&value[0]!='-',"expected an unsigned integer");auto n=std::stoull(value,&end);require(end==value.size()&&n<=std::numeric_limits<unsigned>::max(),"unsigned argument out of range");return static_cast<unsigned>(n);}
static std::string stem(const std::string&dir,Shape s){return dir+"/"+std::to_string(s.stages)+"x"+std::to_string(s.width);}
#if defined(RDS_PAYLOAD_DEFER_INPUTS)||defined(RDS_PAYLOAD_BORROW_INPUTS)
static std::vector<Id> write_only_input(const Model&m,Id id){
 std::vector<Id> enables;bool safe=true;
 auto observe=[&](Id v){if(v==id)safe=false;};
 for(const auto&o:m.ops)for(Id a:o.args)observe(a);
 for(const auto&o:m.metadata["objects"])for(const auto&a:o[4])observe(a);
 for(const auto&r:m.metadata["registers"])for(const auto&a:r)observe(a);
 for(const auto&r:m.metadata["reads"])for(unsigned i=1;i<5;++i)observe(r[i]);
 for(const auto&r:m.metadata["assertions"])for(unsigned i=0;i<3;++i)observe(r[i]);
 for(const auto&p:m.metadata["ports"])if(p[0]==1)observe(p[1]);
 for(const auto&w:m.metadata["writes"]){observe(w[1]);observe(w[3]);observe(w[4]);if(w[2]==id){if(w[3]==none)safe=false;else enables.push_back(w[3]);}}
 // This adapter reads guards from materialized input/combinational slots.
 for(Id guard:enables){for(const auto&r:m.metadata["registers"])if(r[0]==guard)safe=false;
  for(const auto&o:m.ops)if(o.out==guard&&(o.code==0||o.code==1))safe=false;}
 if(!safe)enables.clear();return enables;
}
#endif
static void compile(const std::string&p){pid_t child=fork();require(child>=0,"fork");if(!child){execlp("clang","clang","-O3","-march=native","-DNDEBUG","-shared","-fPIC",p.c_str(),"-o",(p+".so").c_str(),nullptr);_exit(127);}int status;require(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status),"C compilation failed");}
static void batch(std::ostream&f){
 f<<"#ifndef RDS_BATCH_ENTER\n#define RDS_BATCH_ENTER ((void)0)\n#define RDS_BATCH_LEAVE ((void)0)\n#endif\n";
 f<<"#ifndef RDS_BATCH_STEP\n#define RDS_BATCH_STEP(p,i) step(p,i)\n#endif\n";
#ifdef RDS_PAYLOAD_UNROLL_TWO
 f<<"#define RDS_TRACE_LOOP _Pragma(\"clang loop unroll_count(2)\")\n";
#else
 f<<"#define RDS_TRACE_LOOP\n";
#endif
#ifdef RDS_PAYLOAD_LANE_HASH
 f<<R"C(
uint64_t payload_run(void*restrict p,const payload_input*restrict trace,unsigned count,unsigned repeats){RDS_BATCH_ENTER;typedef uint64_t hash_vector __attribute__((vector_size(32)));hash_vector h[(PAYLOAD_WORDS+3)/4]={0};uint64_t control=0;for(unsigned r=0;r<repeats;++r)RDS_TRACE_LOOP for(unsigned i=0;i<count;++i){payload_output o=RDS_BATCH_STEP(p,trace+i);control=control*UINT64_C(0x9e3779b185ebca87)+o.ready+3*o.valid;for(unsigned g=0;g<(PAYLOAD_WORDS+3)/4;++g){hash_vector data={0};unsigned words=PAYLOAD_WORDS-4*g;if(words>4)words=4;memcpy(&data,o.data+4*g,words*8);h[g]=h[g]*UINT64_C(0x9e3779b185ebca87)+data;}}RDS_BATCH_LEAVE;uint64_t result=control*UINT64_C(0x100000001b3),words[((PAYLOAD_WORDS+3)/4)*4];memcpy(words,h,sizeof h);for(unsigned w=0;w<PAYLOAD_WORDS;++w)result=(result^words[w])*UINT64_C(0x100000001b3);return result;}
payload_output payload_once(void*p,const payload_input*i){return step(p,i);}
)C";
#else
 f<<R"C(
uint64_t payload_run(void*restrict p,const payload_input*restrict trace,unsigned count,unsigned repeats){RDS_BATCH_ENTER;uint64_t h=0;for(unsigned r=0;r<repeats;++r)RDS_TRACE_LOOP for(unsigned i=0;i<count;++i){payload_output o=RDS_BATCH_STEP(p,trace+i);h=h*UINT64_C(0x9e3779b185ebca87)+o.ready+3*o.valid;for(unsigned w=0;w<PAYLOAD_WORDS;++w)h=(h^o.data[w])*UINT64_C(0x100000001b3);}RDS_BATCH_LEAVE;return h;}
payload_output payload_once(void*p,const payload_input*i){return step(p,i);}
)C";
#endif
}
static void build(const std::string&dir){for(auto shape:shapes)for(std::string mode:engines){
 auto base=stem(dir,shape),path=base+"-"+mode+".c";std::ofstream f(path);
  f<<"/* Adapts an exact queue-chain model to the common payload trace ABI. */\n#include <stdlib.h>\n#define PAYLOAD_STAGES "<<shape.stages<<"\n#define PAYLOAD_WIDTH "<<shape.width<<'\n';
#ifdef RDS_PAYLOAD_TYPED_INPUTS
 f<<"#define RDS_PAYLOAD_TYPED_INPUTS 1\n";
#endif
#ifdef RDS_TOKEN_BENCHMARK
 f<<"#define PAYLOAD_VALID_ONLY 1\n";
#endif
#ifdef RDS_TOKEN_PIPELINE
 f<<"#define PAYLOAD_FIXED_PIPELINE 1\n";
#endif
 f<<"#include \""<<std::filesystem::absolute("sims/native/payload-software.h").string()<<"\"\n";
 if(mode=="native"||mode==candidate){
  auto model=Model::read(base+"-"+mode+"-optimized.json");require(model.metadata["reads"].empty(),"chain adapter expects no synchronous memories");
#ifdef RDS_TOKEN_PIPELINE
  if(mode=="native"){unsigned stages=0;for(const auto&o:model.metadata["objects"]){require(o[0]==2&&o[3]==8,"fixed-pipeline benchmark requires ValidPipe extraction");stages+=o[2].get<unsigned>();}require(stages==shape.stages,"fixed-pipeline geometry mismatch");}
#endif
  char error[512];rds_options opt{1,4290056208u};auto*s=rds_load_with_options((base+"-"+mode+".rsim").c_str(),&opt,error,sizeof error);require(s,error);
  require(!rds_emit_c(s,(base+"-"+mode+"-generated.c").c_str(),0),rds_error(s));require(!rds_emit_plan(s,(base+"-"+mode+"-plan.json").c_str()),rds_error(s));rds_free(s);
  Json plan;std::ifstream(base+"-"+mode+"-plan.json")>>plan;
  size_t state_words=1;for(auto&v:plan["values"])if(!v[2].is_null())state_words=std::max(state_words,v[2].get<size_t>()+(v[0].get<size_t>()+63)/64);
  bool bound_state=false;
#ifdef RDS_PAYLOAD_BOUND_STATE
  bound_state=!model.metadata["registers"].empty();
#endif
  f<<"#include \""<<base<<"-"<<mode<<"-generated.c\"\n";
  auto initialize_ports=[&](){
    // Production loading seeds constant ports before attachment. The private
    // adapter must do the same; object import initializes object storage only.
    for(const auto&p:model.metadata["ports"])if(p[0]==1)for(const auto&o:model.ops)if(o.out==p[1]&&o.code==0){
      for(const auto&v:plan["values"])if(v[3]==o.out){require(v[2].is_null(),"constant port has FF storage");
        for(unsigned w=0;w<o.imm.size();++w)f<<"c->v[rds_generated_arena_offset("<<v[1]<<")+"<<w<<"]=UINT64_C("<<o.imm[w]<<");";}
    }
  };
#ifdef RDS_PAYLOAD_STATIC_STORAGE
  // Expose allocation ownership to Clang without changing any generated phase.
  f<<"typedef struct{unsigned bank;";
#ifdef RDS_PAYLOAD_LOCAL_STATE
  f<<"uint64_t(*active_state)["<<state_words<<"];";
#endif
  f<<"_Alignas(64) object_state hot;uint64_t state[2]["<<state_words<<"];";
  for(unsigned m=0;m<model.metadata["memories"].size();++m){const auto&mem=model.metadata["memories"][m];f<<"uint64_t memory_"<<m<<"["<<((mem[0].get<unsigned>()+63)/64)*mem[1].get<unsigned>()<<"];";}
  for(unsigned id=0;id<model.metadata["objects"].size();++id){const auto&o=model.metadata["objects"][id];require(o[0]==1||o[0]==2,"adapter expects queues or pipelines");unsigned words=(o[1].get<unsigned>()+63)/64;
    f<<"uint64_t data_"<<id<<"["<<words*o[2].get<unsigned>()<<"],pending_"<<id<<"["<<words<<"];";
    if(o[0]==2)f<<"unsigned char valid_"<<id<<"["<<o[2]<<"],changes_"<<id<<"["<<o[2]<<"];";}
  f<<"rds_object objects["<<model.metadata["objects"].size()+1<<"];char error[512];uint64_t arena[];}adapter;\n";
  auto context=[&](bool bound=false){f<<"uint64_t*memories[]={";for(unsigned m=0;m<model.metadata["memories"].size();++m)f<<"a->memory_"<<m<<",";
    f<<"0};unsigned bank="<<(bound?"*current":"a->bank")<<"&1;rds_compiled_context local={.v=a->arena,.q=";
    if(bound)f<<"banks[bank],.next=banks[bank^1]";
    else
#ifdef RDS_PAYLOAD_LOCAL_STATE
    f<<"a->active_state[bank],.next=a->active_state[bank^1]";
#else
    f<<"a->state[bank],.next=a->state[bank^1]";
#endif
    f<<",.objects=a->objects,.memories=memories,.error=a->error,.hot=&a->hot};rds_compiled_context*c=&local;\n";};
  f<<"void*payload_create(void){size_t bytes=(sizeof(adapter)+rds_generated_arena_words()*8+63)&~(size_t)63;adapter*a=aligned_alloc(64,bytes);if(!a)return 0;memset(a,0,bytes);";
#ifdef RDS_PAYLOAD_LOCAL_STATE
  f<<"a->active_state=a->state;";
  if(!model.metadata["registers"].empty())f<<"\n#define RDS_BATCH_ENTER adapter*batch_model=p;unsigned saved_bank=batch_model->bank&1;uint64_t(*saved_state)["<<state_words<<"]=batch_model->active_state;uint64_t local_state[2]["<<state_words<<"];memcpy(local_state[0],saved_state[saved_bank],"<<state_words*8<<");memcpy(local_state[1],saved_state[saved_bank^1],"<<state_words*8<<");batch_model->active_state=local_state;batch_model->bank=0\n#define RDS_BATCH_LEAVE memcpy(saved_state[saved_bank],local_state[0],"<<state_words*8<<");memcpy(saved_state[saved_bank^1],local_state[1],"<<state_words*8<<");batch_model->active_state=saved_state;batch_model->bank^=saved_bank\n";
#endif
  context();
  initialize_ports();
  for(unsigned id=0;id<model.metadata["objects"].size();++id){f<<"a->objects["<<id<<"].data=a->data_"<<id<<";a->objects["<<id<<"].pending=a->pending_"<<id<<";";
    if(model.metadata["objects"][id][0]==2)f<<"a->objects["<<id<<"].valid=a->valid_"<<id<<";a->objects["<<id<<"].changes=a->changes_"<<id<<";";}
  f<<"rds_generated_objects(c,false);return a;}\nvoid payload_destroy(void*p){free(p);}\n";
  if(bound_state)f<<"#define RDS_BATCH_ENTER adapter*batch_model=p;unsigned saved_bank=batch_model->bank&1,local_bank=0;uint64_t local_state[2]["<<state_words<<"];memcpy(local_state[0],batch_model->state[saved_bank],"<<state_words*8<<");memcpy(local_state[1],batch_model->state[saved_bank^1],"<<state_words*8<<")\n#define RDS_BATCH_LEAVE memcpy(batch_model->state[saved_bank],local_state[0],"<<state_words*8<<");memcpy(batch_model->state[saved_bank^1],local_state[1],"<<state_words*8<<");batch_model->bank=local_bank^saved_bank\n#define RDS_BATCH_STEP(p,i) step_bound(p,i,local_state,&local_bank)\n";
#else
  f<<"typedef struct{rds_compiled_context c;char error[512];} adapter;\nvoid*payload_create(void){adapter*a=calloc(1,sizeof(*a));a->c.v=calloc(rds_generated_arena_words()+1,8);a->c.q=calloc("<<state_words<<",8);a->c.next=calloc("<<state_words<<",8);a->c.hot=aligned_alloc(64,(rds_generated_object_bytes()+63)&~(size_t)63);memset(a->c.hot,0,rds_generated_object_bytes());a->c.error=a->error;a->c.memories=calloc("<<model.metadata["memories"].size()+1<<",sizeof(uint64_t*));";
  for(unsigned m=0;m<model.metadata["memories"].size();++m){auto&mem=model.metadata["memories"][m];f<<"a->c.memories["<<m<<"]=calloc("<<((mem[0].get<unsigned>()+63)/64)*mem[1].get<unsigned>()<<",8);";}
  f<<"a->c.objects=calloc("<<model.metadata["objects"].size()<<",sizeof(rds_object));";
  for(unsigned id=0;id<model.metadata["objects"].size();++id){const auto&o=model.metadata["objects"][id];require(o[0]==1||o[0]==2,"adapter expects queues or pipelines");unsigned words=(o[1].get<unsigned>()+63)/64;
    f<<"a->c.objects["<<id<<"].data=calloc("<<words*o[2].get<unsigned>()<<",8);a->c.objects["<<id<<"].pending=calloc("<<words<<",8);";
    if(o[0]==2)f<<"a->c.objects["<<id<<"].valid=calloc("<<o[2]<<",1);a->c.objects["<<id<<"].changes=calloc("<<o[2]<<",1);";}
  f<<"rds_compiled_context*c=&a->c;";initialize_ports();
  f<<"rds_generated_objects(c,false);return a;}\nvoid payload_destroy(void*p){adapter*a=p;";
  for(unsigned id=0;id<model.metadata["objects"].size();++id){f<<"free(a->c.objects["<<id<<"].data);free(a->c.objects["<<id<<"].pending);";
    if(model.metadata["objects"][id][0]==2)f<<"free(a->c.objects["<<id<<"].valid);free(a->c.objects["<<id<<"].changes);";}
  f<<"free(a->c.objects);";
  for(unsigned m=0;m<model.metadata["memories"].size();++m)f<<"free(a->c.memories["<<m<<"]);";
  f<<"free(a->c.memories);free(a->c.v);free(a->c.q);free(a->c.next);free(a->c.hot);free(a);}\n";
#endif
  f<<R"C(
static inline void put(rds_compiled_context*c,unsigned old,unsigned word,uint64_t value){unsigned p=rds_generated_arena_offset(old);if(rds_generated_arena_small(old))*((unsigned char*)c->v+p)=(unsigned char)value;else c->v[p+word]=value;}
static inline uint64_t get(rds_compiled_context*c,unsigned old,unsigned word){unsigned p=rds_generated_arena_offset(old);return rds_generated_arena_small(old)?*((unsigned char*)c->v+p):c->v[p+word];}
)C";
  if(bound_state)f<<"static inline payload_output step_bound(void*restrict p,const payload_input*restrict i,uint64_t(*restrict banks)["<<state_words<<"],unsigned*restrict current){\n";
  else f<<"static inline payload_output step(void*p,const payload_input*i){\n";
#ifdef RDS_PAYLOAD_STATIC_STORAGE
  f<<"adapter*a=p;";context(bound_state);
  // With no semantic objects, import only refreshes caches and SRAM bases.
  if(model.metadata["objects"].empty())f<<"rds_generated_objects(c,false);\n";
#else
  f<<"rds_compiled_context*c=&((adapter*)p)->c;\n";
#endif
  f<<"payload_output out={0};\n";
  auto offset=[&](Id id){for(auto&v:plan["values"])if(v[3]==id){require(v[2].is_null(),"unexpected state port");return v[1].get<unsigned>();}throw std::runtime_error("missing port");};
  bool borrowed=false;std::string borrowed_args;
#ifdef RDS_PAYLOAD_BORROW_INPUTS
  borrowed=!model.metadata["writes"].empty();
  for(const auto&w:model.metadata["writes"]){bool found=false;
   for(const auto&p:model.metadata["ports"])if(p[0]==0&&p[1]==w[2]&&!write_only_input(model,p[1]).empty()){
    std::string name=p[2];borrowed_args+=",i->"+name;if(name!="data")borrowed=false;found=true;break;}
   if(!found)borrowed=false;
  }
#endif
  std::ostringstream deferred;
  for(auto &p:model.metadata["ports"])if(p[0]==0){std::string name=p[2];unsigned n=(model.widths[p[1].get<Id>()]+63)/64;
   std::vector<Id> guards;
#if defined(RDS_PAYLOAD_DEFER_INPUTS)||defined(RDS_PAYLOAD_BORROW_INPUTS)
   guards=write_only_input(model,p[1]);
#endif
   if(borrowed&&!guards.empty())continue;
   std::ostream&out=guards.empty()?static_cast<std::ostream&>(f):deferred;
   if(!guards.empty()){out<<"/* Accepted writes are this input's only observers. */\nif(";for(size_t g=0;g<guards.size();++g)out<<(g?"||":"")<<"get(c,"<<offset(guards[g])<<",0)";out<<"){\n";}
   for(unsigned w=0;w<n;++w)out<<"put(c,"<<offset(p[1])<<","<<w<<","<<(name=="clock"?"0":"i->"+name+(name=="data"?"["+std::to_string(w)+"]":""))<<");\n";
   if(!guards.empty())out<<"}\n";
  }
  for(unsigned phase:{0,1,6})f<<"if(phase_"<<phase<<"_0(c))abort();\n";
  for(auto &p:model.metadata["ports"])if(p[0]==1){std::string name=p[2];unsigned n=(model.widths[p[1].get<Id>()]+63)/64;const Op*constant=nullptr;
    for(const auto&o:model.ops)if(o.out==p[1]&&o.code==0)constant=&o;
    for(unsigned w=0;w<n;++w){f<<"out."<<(name=="result"?"data["+std::to_string(w)+"]":name)<<"=";
      if(constant)f<<"UINT64_C("<<constant->imm.at(w)<<")";
      else f<<"get(c,"<<offset(p[1])<<","<<w<<")";
      f<<";\n";}
  }
  for(unsigned phase:{3,5})f<<"if(phase_"<<phase<<"_0(c))abort();\n";
  f<<deferred.str();
  if(borrowed)f<<"if(rds_generated_publish_bound(c"<<borrowed_args<<"))abort();\n";
  else f<<"if(phase_4_0(c))abort();\n";
  f<<"if(phase_2_0(c))abort();\n";
  if(!model.metadata["registers"].empty())
#ifdef RDS_PAYLOAD_STATIC_STORAGE
    f<<(bound_state?"*current^=1;\n":"a->bank^=1;\n");
#else
    f<<"uint64_t*old=c->q;c->q=c->next;c->next=old;\n";
#endif
  f<<"return out;}\n";
  if(bound_state)f<<"static inline payload_output step(void*p,const payload_input*i){adapter*a=p;return step_bound(p,i,a->state,&a->bank);}\n";
 }else if(mode=="software-ring")f<<"void*payload_create(void){return calloc(1,sizeof(payload_ring));}\nvoid payload_destroy(void*p){free(p);}\nstatic inline payload_output step(void*p,const payload_input*i){return payload_ring_step(p,i);}\n";
 else f<<"void*payload_create(void){return calloc(1,sizeof(payload_software));}\nvoid payload_destroy(void*p){free(p);}\nstatic inline payload_output step(void*p,const payload_input*i){return payload_step(p,i,"<<(mode=="software-pool")<<");}\n";
 batch(f);f.close();compile(path);
}}
struct Engine{void*lib=nullptr,*state=nullptr;payload_output(*once)(void*,const payload_input*);uint64_t(*run)(void*,const payload_input*,unsigned,unsigned);void(*destroy)(void*);
 explicit Engine(const std::string&p){lib=dlopen(p.c_str(),RTLD_NOW|RTLD_LOCAL);require(lib,dlerror()?"dlopen failed: "+p:p);auto create=(void*(*)())dlsym(lib,"payload_create");once=(decltype(once))dlsym(lib,"payload_once");run=(decltype(run))dlsym(lib,"payload_run");destroy=(decltype(destroy))dlsym(lib,"payload_destroy");require(create&&once&&run&&destroy,"missing ABI");state=create();require(state,"create");}
 ~Engine(){destroy(state);dlclose(lib);}};
static uint64_t replay(Engine&e,const std::vector<payload_input>&input,unsigned count,unsigned repeats,Shape shape){
 uint64_t hash=0;
#ifdef RDS_PAYLOAD_LANE_HASH
 uint64_t lanes[16]={};
#endif
 for(unsigned r=0;r<repeats;++r)for(unsigned i=0;i<count;++i){auto o=e.once(e.state,&input[i]);hash=hash*UINT64_C(0x9e3779b185ebca87)+o.ready+3*o.valid;
  for(unsigned w=0;w<(shape.width+63)/64;++w)
#ifdef RDS_PAYLOAD_LANE_HASH
   lanes[w]=lanes[w]*UINT64_C(0x9e3779b185ebca87)+o.data[w];
#else
   hash=(hash^o.data[w])*UINT64_C(0x100000001b3);
#endif
 }
#ifdef RDS_PAYLOAD_LANE_HASH
 hash*=UINT64_C(0x100000001b3);for(unsigned w=0;w<(shape.width+63)/64;++w)hash=(hash^lanes[w])*UINT64_C(0x100000001b3);
#endif
 return hash;
}
static std::vector<payload_input> trace(Shape s,unsigned traffic){std::mt19937_64 random(448233);std::vector<payload_input> t(256);for(unsigned n=0;n<t.size();++n){auto&i=t[n];i.reset=n==0||n==127;i.push=traffic==0?0:traffic==1?random()%8==0:1;i.pop=traffic==3?random()%4==0:1;for(auto&w:i.data)w=random();unsigned words=(s.width+63)/64;for(unsigned w=words;w<16;++w)i.data[w]=0;if(s.width%64)i.data[words-1]&=(UINT64_C(1)<<(s.width%64))-1;}return t;}
static void verify(const std::string&dir){for(auto shape:shapes)for(unsigned traffic=0;traffic<4;++traffic){auto base=stem(dir,shape);auto inputs=trace(shape,traffic);std::vector<rds_sim*> references;
 for(auto mode:{"original","native",candidate}){char error[512];rds_options opt{1,RDS_REFERENCE};auto*s=rds_load_with_options((base+"-"+mode+".rsim").c_str(),&opt,error,sizeof error);require(s,error);references.push_back(s);}
 std::vector<std::unique_ptr<Engine>> models;for(auto mode:engines)models.push_back(std::make_unique<Engine>(base+"-"+mode+".c.so"));
 for(unsigned cycle=0;cycle<2048;++cycle){auto&i=inputs[cycle%inputs.size()];payload_output expected{};
  for(unsigned k=0;k<references.size();++k){auto*s=references[k];require(!rds_set_u64(s,rds_find_port(s,"reset"),i.reset),rds_error(s));require(!rds_set_u64(s,rds_find_port(s,"push"),i.push),rds_error(s));require(!rds_set_u64(s,rds_find_port(s,"pop"),i.pop),rds_error(s));require(!rds_set(s,rds_find_port(s,"data"),i.data,(shape.width+63)/64),rds_error(s));require(!rds_eval(s),rds_error(s));require(!rds_eval(s),rds_error(s));payload_output out{};require(!rds_get_u64(s,rds_find_port(s,"ready"),&out.ready),rds_error(s));require(!rds_get_u64(s,rds_find_port(s,"valid"),&out.valid),rds_error(s));require(!rds_get(s,rds_find_port(s,"result"),out.data,(shape.width+63)/64),rds_error(s));if(!k)expected=out;else require(!memcmp(&expected,&out,sizeof out),"reference mismatch cycle "+std::to_string(cycle));}
  for(unsigned k=0;k<models.size();++k){auto out=models[k]->once(models[k]->state,&i);if(memcmp(&expected,&out,sizeof out)){std::cerr<<"expected "<<expected.ready<<","<<expected.valid<<" actual "<<out.ready<<","<<out.valid<<"\n";for(unsigned w=0;w<16;++w)if(expected.data[w]!=out.data[w])std::cerr<<w<<":"<<expected.data[w]<<" vs "<<out.data[w]<<"\n";}require(!memcmp(&expected,&out,sizeof out),"software/compiled mismatch mode "+std::to_string(k)+" cycle "+std::to_string(cycle));}
  for(auto*s:references)require(!rds_advance(s),rds_error(s));
 }
 for(auto*s:references)rds_free(s);
 for(unsigned count:{0u,1u,127u,255u,256u})for(const char*mode:engines){Engine batched(base+"-"+mode+".c.so"),scalar(base+"-"+mode+".c.so");
  // Begin with the other physical FF bank current, including zero-cycle runs.
  (void)batched.once(batched.state,&inputs.back());(void)scalar.once(scalar.state,&inputs.back());
  require(batched.run(batched.state,inputs.data(),count,3)==replay(scalar,inputs,count,3,shape),"batch checksum mismatch "+std::string(mode)+" count "+std::to_string(count));
  auto a=batched.once(batched.state,&inputs.back()),b=scalar.once(scalar.state,&inputs.back());require(!memcmp(&a,&b,sizeof a),"batch state mismatch");
 }
 std::cout<<"PASS "<<shape.stages<<"x"<<shape.width<<" traffic="<<traffic<<'\n';
}}
int main(int argc,char**argv){try{require(argc>=3,"expected build/verify/measure directory");std::string mode=argv[1],dir=std::filesystem::absolute(argv[2]);if(mode=="build")build(dir);else if(mode=="verify")verify(dir);else{require(mode=="measure"&&argc==7,"measure directory shape traffic engine repeats");unsigned index=argument(argv[3]),traffic=argument(argv[4]),repeats=argument(argv[6]);require(index<4&&traffic<4&&repeats>0,"shape and traffic must be 0..3; repeats must be positive");std::string engine=argv[5];require(std::any_of(std::begin(engines),std::end(engines),[&](const char*name){return engine==name;}),"unknown payload engine");auto shape=shapes[index];auto inputs=trace(shape,traffic);Engine e(stem(dir,shape)+"-"+argv[5]+".c.so");auto start=std::chrono::steady_clock::now();auto checksum=e.run(e.state,inputs.data(),inputs.size(),repeats);double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();std::cout<<Json{{"stages",shape.stages},{"width",shape.width},{"traffic",traffic},{"engine",argv[5]},{"cycles",uint64_t(inputs.size())*repeats},{"checksum",checksum},{"seconds",seconds},{"ns_per_cycle",seconds*1e9/(uint64_t(inputs.size())*repeats)}}.dump()<<'\n';}}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
