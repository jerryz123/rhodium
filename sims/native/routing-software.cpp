// Compares elaborated routing circuits, generated C, and exact word-mask software.
#include "routing-software.h"
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
static constexpr unsigned flags=4290056208u;
struct Shape{unsigned rows,cols,width;};
static const Shape shapes[]={{1,1,64},{3,2,75},{7,6,244},{16,8,244}};
static void require(bool ok,const std::string&why){if(!ok)throw std::runtime_error(why);}
static Json read(const std::string&p){Json j;std::ifstream(p)>>j;return j;}
static std::string stem(const std::string&dir,Shape s){return dir+"/"+std::to_string(s.rows)+"x"+std::to_string(s.cols);}
static void compile(const std::string&path){pid_t pid=fork();require(pid>=0,"fork");
    if(!pid){auto output=path+".so";execlp("clang","clang","-O3","-march=native","-DNDEBUG","-shared","-fPIC",path.c_str(),"-o",output.c_str(),nullptr);_exit(127);}
    int status;require(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&!WEXITSTATUS(status),"compile failed");}
static void build(const std::string&dir){for(auto shape:shapes){auto base=stem(dir,shape);auto m=Model::read(base+"-optimized.json");
    require(m.metadata["registers"].empty()&&m.metadata["memories"].empty(),"expected native matcher state only");
    char error[512];rds_options options{1,flags};auto*s=rds_load_with_options((base+"-native.rsim").c_str(),&options,error,sizeof error);require(s,error);
    require(!rds_emit_c(s,(base+".c").c_str(),0),rds_error(s));require(!rds_emit_plan(s,(base+"-plan.json").c_str()),rds_error(s));rds_free(s);
    auto plan=read(base+"-plan.json");std::string path=base+"-adapter.c";std::ofstream f(path);
    f<<"/* Adapts identical packed traces to generated and software routing kernels. */\n#include \""<<base<<".c\"\n#include <stdlib.h>\n"
     <<"#define RDS_ROUTING_ROWS "<<shape.rows<<"\n#define RDS_ROUTING_COLS "<<shape.cols<<"\n#define RDS_ROUTING_WIDTH "<<shape.width<<"\n"
     <<"#include \""<<std::filesystem::absolute("sims/native/routing-software.h").string()<<"\"\n";
    f<<R"C(
typedef struct {rds_compiled_context c;char error[512];routing_software_state software;} adapter;
void *routing_create(void){adapter*a=calloc(1,sizeof(*a));if(!a)return NULL;a->c.v=calloc(rds_generated_arena_words()+1,8);a->c.q=calloc(1,8);a->c.next=calloc(1,8);a->c.hot=aligned_alloc(64,(rds_generated_object_bytes()+63)&~(size_t)63);memset(a->c.hot,0,rds_generated_object_bytes());a->c.error=a->error;return a;}
void routing_destroy(void*p){adapter*a=p;free(a->c.v);free(a->c.q);free(a->c.next);free(a->c.hot);free(a);}
static inline void put(rds_compiled_context*c,unsigned old,unsigned word,uint64_t x){unsigned p=rds_generated_arena_offset(old);if(rds_generated_arena_small(old))*((unsigned char*)c->v+p)=(unsigned char)x;else c->v[p+word]=x;}
static inline uint64_t get(rds_compiled_context*c,unsigned old,unsigned word){unsigned p=rds_generated_arena_offset(old);return rds_generated_arena_small(old)?*((unsigned char*)c->v+p):c->v[p+word];}
static inline routing_output native_core(void*p,const routing_input*in){adapter*a=p;rds_compiled_context*c=&a->c;routing_output out={0};
)C";
    auto offset=[&](Id id){for(const auto&v:plan["values"])if(v[3]==id){require(v[2].is_null(),"state port");return v[1].get<unsigned>();}throw std::runtime_error("missing port");};
    for(const auto&p:m.metadata["ports"])if(p[0]==0){std::string name=p[2];unsigned off=offset(p[1]);unsigned count=(m.widths[p[1].get<Id>()]+63)/64;
        require(name=="clock"||name=="reset"||name=="requests"||name=="valids"||name=="payloads"||name=="ready","unknown input "+name);
        bool array=name=="requests"||name=="payloads";
        for(unsigned w=0;w<count;++w)f<<"put(c,"<<off<<","<<w<<","<<(name=="clock"?"0":"in->"+name+(array?"["+std::to_string(w)+"]":""))<<");\n";}
    for(unsigned p:{0,1,6})f<<"if(phase_"<<p<<"_0(c))abort();\n";
    for(const auto&p:m.metadata["ports"])if(p[0]==1){std::string name=p[2];unsigned off=offset(p[1]);unsigned count=(m.widths[p[1].get<Id>()]+63)/64;
        require(name=="selected"||name=="accepted"||name=="offered","unknown output "+name);
        for(unsigned w=0;w<count;++w)f<<"out."<<name<<(name=="selected"?"["+std::to_string(w)+"]":"")<<"=get(c,"<<off<<","<<w<<");\n";}
    for(unsigned p:{3,5,4,2})f<<"if(phase_"<<p<<"_0(c))abort();\n";
    f<<"++c->cycle;return out;}\nrouting_output routing_native(void*p,const routing_input*in){return native_core(p,in);}\n"
     <<"routing_output routing_software(void*p,const routing_input*in){return routing_software_step(&((adapter*)p)->software,in);}\n";
    for(auto mode:{"native","software"})f<<"uint64_t routing_run_"<<mode<<"(void*p,const routing_input*trace,size_t n,unsigned repeats){uint64_t hash=0;for(unsigned r=0;r<repeats;++r)for(size_t i=0;i<n;++i){routing_output out="
      <<(std::string(mode)=="native"?"native_core(p,&trace[i])":"routing_software_step(&((adapter*)p)->software,&trace[i])")
      <<";for(unsigned w=0;w<"<<(shape.cols*shape.width+63)/64<<";++w)hash=(hash^out.selected[w])*UINT64_C(0x9e3779b185ebca87);hash+=out.offered+3*out.accepted;}return hash;}\n";
    f.close();compile(path);
}}
using Step=routing_output(*)(void*,const routing_input*);
using Run=uint64_t(*)(void*,const routing_input*,size_t,unsigned);
struct Engine{void*lib=nullptr,*state=nullptr;Step step=nullptr;Run run=nullptr;void(*destroy)(void*)=nullptr;
    Engine(const std::string&base,const std::string&mode){lib=dlopen((base+"-adapter.c.so").c_str(),RTLD_NOW|RTLD_LOCAL);require(lib,"dlopen "+base);
        auto create=(void*(*)())dlsym(lib,"routing_create");destroy=(void(*)(void*))dlsym(lib,"routing_destroy");step=(Step)dlsym(lib,("routing_"+mode).c_str());run=(Run)dlsym(lib,("routing_run_"+mode).c_str());require(create&&destroy&&step&&run,"adapter ABI");state=create();require(state,"allocation");}
    ~Engine(){if(state)destroy(state);if(lib)dlclose(lib);}};
static std::vector<routing_input> trace(Shape s,unsigned traffic){std::mt19937_64 rng(512+traffic);std::vector<routing_input> result(256);
    for(unsigned n=0;n<result.size();++n){auto&i=result[n];for(auto&w:i.requests)w=rng();for(auto&w:i.payloads)w=rng();
        if(s.rows*s.cols<128){if(s.rows*s.cols<=64)i.requests[1]=0;unsigned bits=s.rows*s.cols%64;if(bits)i.requests[(s.rows*s.cols-1)/64]&=(UINT64_C(1)<<bits)-1;}
        unsigned bits=s.rows*s.width%64;if(bits)i.payloads[(s.rows*s.width-1)/64]&=(UINT64_C(1)<<bits)-1;
        i.valids=traffic==0?0:traffic==1?((n%8)==0?UINT64_C(1)<<(rng()%s.rows):0):(UINT64_C(1)<<s.rows)-1;
        i.ready=traffic==3?rng()&((UINT64_C(1)<<s.cols)-1):(UINT64_C(1)<<s.cols)-1;i.reset=n==0||n==127||n+1==result.size();}
    return result;}
static void set(rds_sim*s,const routing_input&i,Shape shape){for(const char*name:{"reset","requests","valids","payloads","ready"}){int p=rds_find_port(s,name);require(p>=0,"input port");int rc;
    if(std::string(name)=="requests")rc=rds_set(s,p,i.requests,(shape.rows*shape.cols+63)/64);
    else if(std::string(name)=="payloads")rc=rds_set(s,p,i.payloads,(shape.rows*shape.width+63)/64);
    else rc=rds_set_u64(s,p,std::string(name)=="clock"?0:std::string(name)=="reset"?i.reset:std::string(name)=="valids"?i.valids:i.ready);
    require(!rc,rds_error(s));}}
static routing_output output(rds_sim*s,Shape shape){routing_output o{};require(!rds_get(s,rds_find_port(s,"selected"),o.selected,(shape.cols*shape.width+63)/64),rds_error(s));
    require(!rds_get_u64(s,rds_find_port(s,"accepted"),&o.accepted),rds_error(s));require(!rds_get_u64(s,rds_find_port(s,"offered"),&o.offered),rds_error(s));return o;}
static bool equal(const routing_output&a,const routing_output&b,Shape s){return a.offered==b.offered&&a.accepted==b.accepted&&!memcmp(a.selected,b.selected,8*((s.cols*s.width+63)/64));}
static void verify(const std::string&dir){for(auto shape:shapes)for(unsigned traffic=0;traffic<4;++traffic){auto base=stem(dir,shape);Engine generated(base,"native"),software(base,"software");char error[512];
    rds_options options{1,RDS_REFERENCE};auto*original=rds_load_with_options((base+"-original.rsim").c_str(),&options,error,sizeof error);require(original,error);
    auto*native=rds_load_with_options((base+"-native.rsim").c_str(),&options,error,sizeof error);require(native,error);
    for(const auto&i:trace(shape,traffic)){set(original,i,shape);set(native,i,shape);require(!rds_eval(original),rds_error(original));require(!rds_eval(native),rds_error(native));
        auto a=output(original,shape),b=output(native,shape),c=generated.step(generated.state,&i),d=software.step(software.state,&i);
        require(equal(a,b,shape)&&equal(a,c,shape)&&equal(a,d,shape),"routing mismatch "+base+" traffic "+std::to_string(traffic));
        require(!rds_advance(original),rds_error(original));require(!rds_advance(native),rds_error(native));}
    rds_free(original);rds_free(native);std::cerr<<"PASS "<<shape.rows<<"x"<<shape.cols<<" traffic="<<traffic<<'\n';}}
static void measure(const std::string&dir,unsigned index,unsigned traffic,const std::string&mode,unsigned repeats){require(index<4&&traffic<4&&repeats>0,"invalid measurement");auto s=shapes[index];auto inputs=trace(s,traffic);Engine engine(stem(dir,s),mode);
    auto start=std::chrono::steady_clock::now();auto hash=engine.run(engine.state,inputs.data(),inputs.size(),repeats);
    double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();uint64_t cycles=uint64_t(inputs.size())*repeats;
    std::cout<<Json({{"rows",s.rows},{"cols",s.cols},{"width",s.width},{"traffic",traffic},{"mode",mode},{"cycles",cycles},{"checksum",hash},{"seconds",seconds},{"ns_per_cycle",seconds*1e9/cycles}})<<'\n';}
static void report(const std::string&path){std::ifstream f(path);std::string line;Json groups=Json::object();
    while(std::getline(f,line)){auto row=Json::parse(line);auto key=std::to_string(row["rows"].get<unsigned>())+"x"+std::to_string(row["cols"].get<unsigned>())+"-"+std::to_string(row["traffic"].get<unsigned>());
        auto &g=groups[key];Json signature={row["cycles"],row["checksum"],row["width"]};
        if(g.contains("signature"))require(g["signature"]==signature,"measurement signature mismatch");else g["signature"]=signature;
        g[row["mode"].get<std::string>()].push_back(row["ns_per_cycle"]);}
    require(!groups.empty(),"empty measurements");
    for(auto &g:groups){for(auto mode:{"native","software"}){require(g.contains(mode)&&g[mode].size()>=3,"need at least three measurements per implementation");
        auto v=g[mode].get<std::vector<double>>();std::sort(v.begin(),v.end());g[std::string(mode)+"_median_ns"]=(v[(v.size()-1)/2]+v[v.size()/2])/2;}
        g["native_over_software"]=g["native_median_ns"].get<double>()/g["software_median_ns"].get<double>();}
    std::cout<<groups.dump(2)<<'\n';}
int main(int argc,char**argv){try{require(argc>=3,"routing-software build|verify DIR | measure DIR SHAPE TRAFFIC native|software REPEATS | report JSONL");auto dir=std::filesystem::absolute(argv[2]).string();std::string mode=argv[1];
    if(mode=="build"&&argc==3)build(dir);else if(mode=="verify"&&argc==3)verify(dir);else if(mode=="report"&&argc==3)report(argv[2]);else if(mode=="measure"&&argc==7)measure(dir,std::stoul(argv[3]),std::stoul(argv[4]),argv[5],std::stoul(argv[6]));else throw std::runtime_error("invalid command");
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
