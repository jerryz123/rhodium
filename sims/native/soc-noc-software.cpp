// Validates and times the actual-SoC functional NoC model against recorded endpoint traffic.
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>
using J=nlohmann::json;
static void require(bool b,const std::string&s){if(!b)throw std::runtime_error(s);}
static uint32_t read32(std::istream&f){uint32_t n=0;for(unsigned i=0;i<4;++i){int c=f.get();require(c>=0,"truncated trace");n|=uint32_t(c)<<(i*8);}return n;}
static uint64_t read64(std::istream&f){uint64_t n=0;for(unsigned i=0;i<8;++i){int c=f.get();require(c>=0,"truncated trace");n|=uint64_t(c)<<(i*8);}return n;}
struct Change{unsigned word;uint64_t value;};
struct Frame{std::vector<Change>changes;bool measured;};
struct Run{std::vector<Change>changes;uint64_t first,count;bool measured;};
struct Software{
 void*library=nullptr,*state=nullptr;void(*destroy)(void*)=nullptr;void(*eval)(void*)=nullptr;void(*step)(void*)=nullptr;uint64_t*input=nullptr;const uint64_t*output=nullptr;size_t bytes=0;
 template<class T>T symbol(const char*name){auto p=reinterpret_cast<T>(dlsym(library,name));require(p,"missing software symbol");return p;}
 explicit Software(const char*path,unsigned workers){library=dlopen(path,RTLD_NOW|RTLD_LOCAL);if(!library)throw std::runtime_error(dlerror());auto count=reinterpret_cast<unsigned(*)()>(dlsym(library,"snoc_workers"));require((count?count():1)==workers,"software worker count mismatch");state=symbol<void*(*)()>("snoc_create")();require(state,"model allocation failed");destroy=symbol<void(*)(void*)>("snoc_destroy");eval=symbol<void(*)(void*)>("snoc_eval");step=symbol<void(*)(void*)>("snoc_step");input=symbol<uint64_t*(*)(void*)>("snoc_inputs")(state);output=symbol<const uint64_t*(*)(void*)>("snoc_outputs")(state);bytes=symbol<size_t(*)()>("snoc_storage")();}
 ~Software(){if(state&&destroy)destroy(state);if(library)dlclose(library);}
};
static double now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
int main(int argc,char**argv){try{
 require(argc==4||argc==5,"soc-noc-software verify|measure SOFTWARE.so TRACE.bin [1|8]");std::string mode=argv[1];unsigned workers=argc==5?std::stoul(argv[4]):1;require(workers==1||workers==8,"software workers must be one or eight");require(mode=="verify"||mode=="measure","unknown action");Software model(argv[2],workers);
 std::ifstream trace(argv[3],std::ios::binary);char magic[8];trace.read(magic,8);require(trace&&std::string(magic,8)=="RDSNOCT1"&&read32(trace)==288&&read32(trace)==260,"wrong eight-hart trace");
 std::vector<Frame>frames;std::vector<Run>runs;std::vector<uint64_t>expected(548);uint64_t measured=0,changed=0;
 while(trace.peek()!=std::char_traits<char>::eof()){unsigned n=read32(trace);int mark=trace.get();require(n<=548&&mark>=0&&mark<=1,"invalid frame");Frame frame;frame.measured=mark;std::vector<Change>inputs;std::set<unsigned>seen;
  for(unsigned i=0;i<n;++i){unsigned w=read32(trace);uint64_t value=read64(trace);require(w<548&&seen.insert(w).second,"invalid word change");expected[w]=value;frame.changes.push_back({w,value});if(w<288)inputs.push_back({w,value});}
  if(runs.empty()||!inputs.empty()||runs.back().measured!=bool(mark))runs.push_back({std::move(inputs),frames.size(),0,bool(mark)});++runs.back().count;frames.push_back(std::move(frame));measured+=mark;changed+=n;
 }
 require(frames.size()>1&&measured,"empty traffic");uint64_t compared=0,timed=0,digest=1469598103934665603ull;double elapsed=0,start=0;bool timing=false;
 if(mode=="verify"){std::vector<uint64_t>words(548);for(size_t tick=0;tick<frames.size();++tick){for(auto c:frames[tick].changes)words[c.word]=c.value;std::copy(words.begin(),words.begin()+288,model.input);model.eval(model.state);for(unsigned w=0;w<260;++w){require(model.output[w]==words[288+w],"software mismatch cycle="+std::to_string(tick)+" output_word="+std::to_string(w)+" got="+std::to_string(model.output[w])+" expected="+std::to_string(words[288+w]));++compared;}if(tick+1<frames.size())model.step(model.state);}}
 else{for(const auto&r:runs){if(r.measured&&!timing){start=now();timing=true;}if(!r.measured&&timing){elapsed+=now()-start;timing=false;}for(auto c:r.changes)model.input[c.word]=c.value;uint64_t edges=std::min(r.count,frames.size()-1-r.first);if(r.measured)timed+=edges;for(uint64_t i=0;i<edges;++i)model.step(model.state);}if(timing)elapsed+=now()-start;model.eval(model.state);for(unsigned w=0;w<260;++w)require(model.output[w]==expected[288+w],"terminal software mismatch");}
 for(unsigned w=0;w<260;++w)digest=(digest^model.output[w])*1099511628211ull;
 J report={{"reference","eight-hart RV5Stage CHI NoC recorded endpoints"},{"engine","functional-software"},{"workers",workers},{"frames",frames.size()},{"measured_cycles",mode=="verify"?measured:timed},{"fixed_input_runs",runs.size()},{"changed_words",changed},{"terminal_digest",digest},{"compared_output_words",compared},{"storage_bytes",model.bytes},{"input_delivery_inside_timing",true},{"trace_decode_inside_timing",false},{"checks_inside_timing",false},{"timing_claim",mode=="measure"}};
 if(mode=="measure"){report["seconds"]=elapsed;report["ns_per_cycle"]=elapsed*1e9/timed;}std::cout<<report.dump(2)<<'\n';
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
