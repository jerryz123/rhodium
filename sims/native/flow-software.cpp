// Builds matched IR/software flow examples and separates kernel, dispatch and API costs.
#include "flow-software.h"
#include "flow-lift.hpp"
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
namespace fs=std::filesystem;
// One fixed policy per process allows production lowering to use the same
// differential replay and handwritten baseline without changing host flags.
static const unsigned flags=[](){const char *value=std::getenv("RDS_FLOW_FLAGS");
    return value?unsigned(std::stoul(value)):4088729616u;}();
static void require(bool ok,const std::string &s){if(!ok)throw std::runtime_error(s);}
static void write(const std::string &p,const std::string &s){std::ofstream f(p);f<<s;require(bool(f),"write "+p);}
static Json read_json(const std::string &p){std::ifstream f(p);Json j;f>>j;return j;}
static void command(std::vector<std::string> args){
    pid_t pid=fork();require(pid>=0,"fork");
    if(!pid){std::vector<char*> a;for(auto &s:args)a.push_back(s.data());a.push_back(nullptr);execvp(a[0],a.data());_exit(127);}
    int status;require(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&!WEXITSTATUS(status),"command failed: "+args[0]);
}
struct Builder {
    Model m;
    Builder(unsigned kind){
        m.widths={1,1,1,kind==0?6u:2u,kind==0?6u:64u,64,64,64};
        m.metadata={{"format","rhodium-simulation-ir-v1"}};
        m.metadata["opcodes"]={"constant","copy","not","and","or","xor","add","sub","mul","shl","shru","shrs","eq","ult","slt","mux_lookup","onehot_mux","extract","zext","sext","pack","vector_index","vector_inject","vector_write_set","memory_read_async","decode","set_clear","balance","counter_step","object_query","alu","byte_merge"};
        for(auto key:{"registers","memories","writes","reads","assertions","objects","origins","inventory","ports"})m.metadata[key]=Json::array();
        m.metadata["occurrences"]={"software-flow-example"};
        for(unsigned i=0;i<8;++i)m.metadata["ports"].push_back({0,i,"i"+std::to_string(i)});
    }
    Id op(unsigned code,unsigned width,std::vector<Id> args={},std::vector<uint64_t> imm={}){
        Id id=m.widths.size();m.widths.push_back(width);m.ops.push_back({code,id,std::move(args),std::move(imm)});return id;
    }
    Id constant(uint64_t x,unsigned width=64){return op(0,width,{}, {x});}
    Id bin(unsigned code,Id a,Id b){return op(code,64,{a,b});}
    Id map(){
        Id a=4,b=5,x1=a,x2=a,x3=a;
        for(unsigned j=0;j<8;++j){
            x1=bin(6,bin(8,bin(5,x1,bin(10,x1,constant(13))),constant(UINT64_C(0x9e3779b185ebca87)+2*j)),b);
            x2=bin(6,bin(6,bin(4,bin(9,x2,constant(7)),bin(10,x2,constant(57))),b),constant(j));
            x3=bin(5,bin(6,x3,b),bin(10,x3,constant(j+1)));
        }
        return op(15,64,{3,bin(6,a,b),x1,x2,x3},{1,2,3});
    }
    Model build(unsigned kind){
        std::vector<Id> outputs;
        if(kind==0){
            m.metadata["objects"].push_back({4,64,64,0,{0,1,3,2,4},"scoreboard"});
            Id busy=op(29,64,{}, {0,0}),one=constant(1),zero=constant(0);
            outputs={busy,bin(3,bin(10,busy,3),one),bin(3,bin(10,busy,4),one),op(12,1,{busy,zero})};
        }else if(kind==1){Id result=map(),zero=constant(0);outputs={result,zero,zero,zero};}
        else{
            Id payload=map(),consume=kind==3?op(29,1,{}, {1,1}):2;
            m.metadata["objects"].push_back({1,64,1,0,{0,1,payload,consume},"mapped_queue"});
            Id data=op(29,64,{}, {0,3}),valid=op(29,1,{}, {0,2}),ready=op(29,1,{}, {0,1});
            outputs={data,valid,ready,op(3,1,{valid,2})};
            if(kind==3){
                Id next=bin(6,data,5);
                m.metadata["objects"].push_back({1,64,1,0,{0,valid,next,2},"downstream_queue"});
                outputs={op(29,64,{}, {1,3}),op(29,1,{}, {1,2}),ready,op(3,1,{valid,consume})};
            }
        }
        for(unsigned i=0;i<4;++i)m.metadata["ports"].push_back({1,outputs[i],"o"+std::to_string(i)});
        m.validate();optimize_body(m);m.validate();return m;
    }
};

static std::string port_expression(const Json &plan,Id id,bool assign){
    for(const auto &v:plan.at("values"))if(v[3]==id){
        require(v[2].is_null(),"fixture unexpectedly contains FF port");
        std::string offset=std::to_string(v[1].get<unsigned>());
        if(assign)return "put(c,"+offset+",";
        return "get(c,"+offset+")";
    }
    throw std::runtime_error("missing port binding");
}

static std::string batch_wrapper(const std::string &step){
    return "\n/* Exposes whole-loop optimization with identical inputs and output checksum. */\n"
        "uint64_t flow_run(void*restrict p,const flow_input*restrict trace,size_t count,unsigned repeats){uint64_t h=0;"
        "for(unsigned r=0;r<repeats;++r)for(size_t n=0;n<count;++n){flow_output o="+step+"(p,&trace[n]);"
        "h=(h^o.x[0])*UINT64_C(0x9e3779b185ebca87)+o.x[1]+3*o.x[2]+7*o.x[3];}return h;}\n";
}

static void build(const std::string &dir){
    fs::create_directories(dir);
    const char *cc=getenv("CC");if(!cc)cc="clang";
    for(unsigned kind=0;kind<4;++kind){
        auto m=Builder(kind).build(kind);std::string stem=dir+"/"+std::to_string(kind);
        m.write_binary(stem+".rsim");write(stem+".json",m.json().dump(2));
        char error[512];rds_options options{1,flags};auto *s=rds_load_with_options((stem+".rsim").c_str(),&options,error,sizeof error);require(s,error);
        require(!rds_emit_c(s,(stem+".c").c_str(),0),rds_error(s));require(!rds_emit_plan(s,(stem+".plan.json").c_str()),rds_error(s));rds_free(s);
        auto plan=read_json(stem+".plan.json");
        std::string code="/* Adapts the exact generated phases for direct and dispatched kernel measurements. */\n#include \""+stem+".c\"\n#include <stdlib.h>\n";
        code+=R"C(
typedef struct {uint64_t x[8];} flow_input;
typedef struct {uint64_t x[4];} flow_output;
typedef struct {rds_compiled_context c;phase_fn phase[7];char error[512];} adapter;
static void put(rds_compiled_context*c,unsigned old,uint64_t x){unsigned p=rds_generated_arena_offset(old);if(rds_generated_arena_small(old))*((unsigned char*)c->v+p)=(unsigned char)x;else c->v[p]=x;}
static uint64_t get(rds_compiled_context*c,unsigned old){unsigned p=rds_generated_arena_offset(old);return rds_generated_arena_small(old)?*((unsigned char*)c->v+p):c->v[p];}
void *flow_create(void){adapter*a=calloc(1,sizeof(*a));if(!a)return NULL;a->c.v=calloc(rds_generated_arena_words()+1,8);a->c.q=calloc(1,8);a->c.next=calloc(1,8);a->c.hot=aligned_alloc(64,(rds_generated_object_bytes()+63)&~(size_t)63);memset(a->c.hot,0,rds_generated_object_bytes());a->c.error=a->error;for(unsigned p=0;p<7;++p)a->phase[p]=rds_generated_bind(0,p);return a;}
void flow_destroy(void*p){adapter*a=p;free(a->c.v);free(a->c.q);free(a->c.next);free(a->c.hot);free(a);}
)C";
        for(bool direct:{false,true}){
            code+=(direct?"static inline __attribute__((always_inline)) ":"")+std::string("flow_output flow_")+std::string(direct?"direct_core":"phases")+"(void*p,const flow_input*i){adapter*a=p;rds_compiled_context*c=&a->c;flow_output o;\n";
            for(unsigned i=0;i<8;++i)code+=port_expression(plan,m.metadata["ports"][i][1],true)+"i->x["+std::to_string(i)+"]);\n";
            auto phase=[&](unsigned p){return direct?"phase_"+std::to_string(p)+"_0(c)":"a->phase["+std::to_string(p)+"](c)";};
            for(unsigned p:{0,1,6})code+="if("+phase(p)+")abort();\n";
            for(unsigned i=0;i<4;++i)code+="o.x["+std::to_string(i)+"]="+port_expression(plan,m.metadata["ports"][8+i][1],false)+";\n";
            for(unsigned p:{3,5,4,2})code+="if("+phase(p)+")abort();\n";
            code+="uint64_t*old=c->q;c->q=c->next;c->next=old;++c->cycle;return o;}\n";
            if(direct)code+="flow_output flow_direct(void*p,const flow_input*i){return flow_direct_core(p,i);}\n";
        }
        write(stem+"-adapter.c",code+batch_wrapper("flow_direct_core"));
        command({cc,"-O3","-march=native","-DNDEBUG","-std=c17","-fPIC","-shared",stem+"-adapter.c","-o",stem+".so"});
        write(stem+"-lifted.c",lift_flow(Model::read(stem+".json"))+batch_wrapper("flow_core"));
        command({cc,"-O3","-march=native","-DNDEBUG","-std=c17","-fPIC","-shared",stem+"-lifted.c","-o",stem+"-lifted.so"});
        Json summary={{"kind",kind},{"ir_ops",m.ops.size()},{"scheduled_work",plan["estimated_work"]}};
        std::cerr<<summary.dump()<<'\n';
    }
}

using Step=flow_output(*)(void*,const flow_input*);
using Run=uint64_t(*)(void*,const flow_input*,size_t,unsigned);
template<unsigned kind> __attribute__((noinline)) static flow_output software(void *p,const flow_input *i){
    if constexpr(kind==0)return flow_scoreboard(static_cast<flow_state*>(p),i);
    if constexpr(kind==1)return flow_mux(static_cast<flow_state*>(p),i);
    if constexpr(kind==3)return flow_pipeline(static_cast<flow_state*>(p),i);
    return flow_queue(static_cast<flow_state*>(p),i);
}
static Step software_steps[]={software<0>,software<1>,software<2>,software<3>};
struct Engine {
    void *library=nullptr,*state=nullptr;Step step=nullptr;Run run=nullptr;void(*destroy)(void*)=nullptr;
    rds_sim *sim=nullptr;
    ~Engine(){if(sim)rds_free(sim);if(destroy)destroy(state);if(library)dlclose(library);}
};
static flow_output api_step(void *p,const flow_input *i){
    auto*s=static_cast<rds_sim*>(p);
    for(unsigned j=0;j<8;++j)if(rds_set_u64(s,j,i->x[j]))throw std::runtime_error(rds_error(s));
    if(rds_eval(s))throw std::runtime_error(rds_error(s));
    flow_output o;for(unsigned j=0;j<4;++j)if(rds_get_u64(s,8+j,&o.x[j]))throw std::runtime_error(rds_error(s));
    if(rds_advance(s))throw std::runtime_error(rds_error(s));return o;
}
static void initialize(Engine &e,const std::string &dir,unsigned kind,const std::string &variant){
    std::string stem=dir+"/"+std::to_string(kind);
    if(variant=="software"){
        e.state=new flow_state{};e.destroy=[](void*p){delete static_cast<flow_state*>(p);};e.step=software_steps[kind];return;
    }
    if(variant=="api"||variant=="reference"){
        char error[512];rds_options options{1,variant=="reference"?RDS_REFERENCE:flags};
        e.sim=rds_load_with_options((stem+".rsim").c_str(),&options,error,sizeof error);require(e.sim,error);
        if(variant=="api")require(!rds_use_compiled(e.sim,(stem+".so").c_str()),rds_error(e.sim));
        e.state=e.sim;e.step=api_step;return;
    }
    e.library=dlopen((stem+(variant=="lifted"?"-lifted":"")+".so").c_str(),RTLD_NOW|RTLD_LOCAL);require(e.library,"dlopen "+stem);
    auto create=reinterpret_cast<void*(*)()>(dlsym(e.library,"flow_create"));
    e.destroy=reinterpret_cast<void(*)(void*)>(dlsym(e.library,"flow_destroy"));
    e.step=reinterpret_cast<Step>(dlsym(e.library,("flow_"+variant).c_str()));require(create&&e.destroy&&e.step,"missing adapter ABI");
    e.run=reinterpret_cast<Run>(dlsym(e.library,"flow_run"));
    e.state=create();require(e.state,"allocation");
}
static std::vector<flow_input> inputs(unsigned kind,unsigned traffic){
    unsigned count=getenv("RDS_FLOW_TRACE_SAMPLES")?std::stoul(getenv("RDS_FLOW_TRACE_SAMPLES")):8192;
    require(count>=2&&count<=1048576,"trace sample count must be in 2..1048576");
    std::vector<flow_input> trace(count);std::mt19937_64 rng(1234+kind*37+traffic);flow_state s{};
    for(unsigned n=0;n<trace.size();++n){
        auto &i=trace[n];for(auto &x:i.x)x=rng();
        i.x[0]=n==0||n+1==trace.size()||n%1021==0;
        i.x[1]=(rng()&15)<(traffic?14:1);i.x[2]=(rng()&15)<(traffic?12:2);
        i.x[3]=traffic?rng()&3:((rng()&15)?0:rng()&3);
        if(kind==0){
            i.x[3]=rng()&63;i.x[4]=rng()&63;
            i.x[1]&=!((s.busy>>i.x[3])&1);i.x[2]&=(s.busy>>i.x[4])&1;
            if(n%19==0){i.x[4]=i.x[3];i.x[1]=i.x[2]=1;}
        }
        software_steps[kind](&s,&i);
    }
    return trace;
}
static uint64_t digest(uint64_t h,flow_output o){return (h^o.x[0])*UINT64_C(0x9e3779b185ebca87)+o.x[1]+3*o.x[2]+7*o.x[3];}
template<unsigned kind> static uint64_t software_run(const std::vector<flow_input>&trace,unsigned repeats){
    flow_state state{};uint64_t h=0;
    for(unsigned r=0;r<repeats;++r)for(const auto &i:trace){
        flow_output o;
        if constexpr(kind==0)o=flow_scoreboard(&state,&i);
        if constexpr(kind==1)o=flow_mux(&state,&i);
        if constexpr(kind==2)o=flow_queue(&state,&i);
        if constexpr(kind==3)o=flow_pipeline(&state,&i);
        h=digest(h,o);
    }
    return h;
}
static void verify(const std::string &dir){
    for(unsigned kind=0;kind<4;++kind)for(unsigned traffic=0;traffic<2;++traffic){
        auto trace=inputs(kind,traffic);
        for(auto variant:{"reference","api","phases","direct","lifted"}){
            Engine candidate,golden;initialize(candidate,dir,kind,variant);initialize(golden,dir,kind,"software");
            for(unsigned repeat=0;repeat<2;++repeat)for(unsigned n=0;n<trace.size();++n){
                auto a=candidate.step(candidate.state,&trace[n]),b=golden.step(golden.state,&trace[n]);
                for(unsigned j=0;j<4;++j)require(a.x[j]==b.x[j],"mismatch kind="+std::to_string(kind)+" traffic="+std::to_string(traffic)+" variant="+variant+" cycle="+std::to_string(n)+" output="+std::to_string(j)+" got="+std::to_string(a.x[j])+" want="+std::to_string(b.x[j]));
            }
        }
    }
    std::cerr<<"PASS: all cycle outputs match reference IR, native API, generated phases, direct phases, lifted IR and software\n";
}
static void report(const std::string &path){
    std::ifstream f(path);std::string line;Json result=Json::object();
    std::map<std::pair<unsigned,unsigned>,std::pair<uint64_t,uint64_t>> signatures;
    while(std::getline(f,line)){
        Json j=Json::parse(line);unsigned k=j.at("kind"),t=j.at("traffic");std::string v=j.at("variant");
        auto signature=std::make_pair(j.at("cycles").get<uint64_t>(),j.at("checksum").get<uint64_t>());
        auto key=std::make_pair(k,t);if(signatures.count(key))require(signatures[key]==signature,"benchmark signature mismatch");else signatures[key]=signature;
        result[std::to_string(k)+"-"+std::to_string(t)][v]["samples"].push_back(j.at("ns_per_cycle"));
    }
    require(!result.empty(),"empty benchmark report");
    for(auto &row:result)for(auto &v:row){auto a=v["samples"].get<std::vector<double>>();std::sort(a.begin(),a.end());v["median_ns"]=(a[(a.size()-1)/2]+a[a.size()/2])/2;}
    std::cout<<result.dump(2)<<'\n';
}
static void measure(const std::string &dir,unsigned kind,unsigned traffic,const std::string &variant,unsigned repeats){
    require(kind<4&&traffic<2&&repeats>0,"invalid measurement arguments");auto trace=inputs(kind,traffic);
    bool batched=variant=="direct-inline"||variant=="lifted-inline";
    bool soft_inline=variant=="software-inline";
    Engine e;initialize(e,dir,kind,soft_inline?"software":batched?variant.substr(0,variant.find('-')):variant);uint64_t checksum=0;
    if(batched)require(e.run,"missing batch ABI");
    auto start=std::chrono::steady_clock::now();
    if(soft_inline){switch(kind){case 0:checksum=software_run<0>(trace,repeats);break;case 1:checksum=software_run<1>(trace,repeats);break;case 2:checksum=software_run<2>(trace,repeats);break;case 3:checksum=software_run<3>(trace,repeats);break;}}
    else if(batched)checksum=e.run(e.state,trace.data(),trace.size(),repeats);
    else for(unsigned r=0;r<repeats;++r)for(const auto &i:trace)checksum=digest(checksum,e.step(e.state,&i));
    double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    uint64_t cycles=uint64_t(repeats)*trace.size();
    std::cout<<Json({{"kind",kind},{"traffic",traffic},{"variant",variant},{"trace_samples",trace.size()},{"cycles",cycles},{"seconds",seconds},{"ns_per_cycle",seconds*1e9/cycles},{"checksum",checksum}}).dump()<<'\n';
}
int main(int argc,char **argv){try{
    require(argc>=3,"usage: flow-software build|verify DIR | measure DIR KIND TRAFFIC VARIANT REPEATS");
    std::string mode=argv[1],dir=fs::absolute(argv[2]);
    if(mode=="build"&&argc==3)build(dir);
    else if(mode=="verify"&&argc==3)verify(dir);
    else if(mode=="report"&&argc==3)report(argv[2]);
    else if(mode=="measure"&&argc==7)measure(dir,std::stoul(argv[3]),std::stoul(argv[4]),argv[5],std::stoul(argv[6]));
    else throw std::runtime_error("invalid command");
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
