// Emits a direct FIFO/router software model with compiled adapters from the actual eight-hart NoC.
#include <nlohmann/json.hpp>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
using J=nlohmann::json;using Id=uint32_t;
static void require(bool b,const std::string&s){if(!b)throw std::runtime_error(s);}
static unsigned words(unsigned width){return(width+63)/64;}
static uint64_t mask(unsigned width){return width==64?UINT64_MAX:(UINT64_C(1)<<width)-1;}
struct Generator{
 J topology,graph;std::ostream&f=std::cout;std::map<std::string,unsigned>input_offsets,output_offsets;unsigned ni=0,no=0,workers=1;std::map<Id,std::pair<unsigned,unsigned>>fifo_router;std::map<Id,unsigned>fifo_owner;std::vector<unsigned>router_owner;
 unsigned width(Id v)const{return graph["values"][v];}
 std::string cv(Id v,unsigned i=0)const{return "v"+std::to_string(v)+"["+std::to_string(i)+"]";}
 std::string extract(Id v,unsigned low,unsigned bits)const{unsigned word=low/64,shift=low%64;std::string s="("+cv(v,word);if(shift)s+=">>"+std::to_string(shift);s+=")";if(shift&&bits>64-shift&&word+1<words(width(v)))s="("+s+"|("+cv(v,word+1)+"<<"+std::to_string(64-shift)+"))";return "("+s+"&UINT64_C("+std::to_string(mask(bits))+"))";}
 void operation(const J&o){unsigned code=o[0];Id v=o[1];auto a=o[2].get<std::vector<Id>>();auto im=o[3].get<std::vector<uint64_t>>();unsigned w=width(v),n=words(w);f<<"uint64_t v"<<v<<"["<<n<<"];\n";
  if(code==25){require(width(a[0])<=64&&w<=64&&im.size()==2+3*im[0],"wide adapter decoder");f<<cv(v)<<"=UINT64_C("<<im[1]<<");";for(unsigned row=0;row<im[0];++row)f<<(row?"else ":"")<<"if(("<<cv(a[0])<<"&UINT64_C("<<im[3+3*row]<<"))==UINT64_C("<<im[2+3*row]<<"))"<<cv(v)<<"=UINT64_C("<<im[4+3*row]<<");";f<<'\n';return;}
  for(unsigned i=0;i<n;++i){unsigned used=std::min(64u,w-i*64);std::string expr;
   if(code==0)expr="UINT64_C("+std::to_string(im.at(i))+")";
   else if(code==3)expr=cv(a[0],i)+"&"+cv(a[1],i);
   else if(code==17)expr=extract(a[0],im[0]+i*64,used);
   else if(code==20){unsigned offset=0;expr="UINT64_C(0)";for(Id source:a){unsigned begin=std::max(i*64,offset),end=std::min(i*64+used,offset+width(source));if(begin<end)expr+="|("+extract(source,begin-offset,end-begin)+"<<"+std::to_string(begin-i*64)+")";offset+=width(source);}}
   else throw std::runtime_error("unsupported adapter opcode "+std::to_string(code));
   f<<cv(v,i)<<"=("<<expr<<")&UINT64_C("<<mask(used)<<");\n";
  }
 }
 void binding(const J&b){Id v=b["value"];std::string source=b["source"],expr;unsigned n=words(width(v));f<<"uint64_t v"<<v<<"["<<n<<"];";
   if(source=="endpoint")expr="m->inputs+"+std::to_string(input_offsets.at(b["name"].get<std::string>()));
   else if(source=="fifo"){unsigned q=b["object"],query=b["query"];if(query==3)expr="fifo["+std::to_string(q)+"].data";else{require(query<=2,"unknown FIFO query");f<<cv(v)<<"="<<(query==1?"!":"")<<"fifo["<<q<<"].full;\n";return;}}
   else if(source=="router"){unsigned r=b["router"],t=b["target"];if(b["field"]=="valid"){f<<cv(v)<<"=(router["<<r<<"].valid>>"<<t<<")&1;\n";return;}expr="fifo[router_fifo["+std::to_string(r)+"][router["+std::to_string(r)+"].winner["+std::to_string(t)+"]]].data";}
   else throw std::runtime_error("unknown glue source");
   f<<"memcpy(v"<<v<<","<<expr<<","<<n*8<<");\n";
 }
 std::set<Id> closure(const std::set<Id>&roots,const std::set<Id>&defined={})const{
  std::map<Id,const J*>defs;for(const auto&o:graph["operations"])defs[o[1].get<Id>()]=&o;
  std::set<Id> needed;std::function<void(Id)> visit=[&](Id v){if(defined.count(v)||!needed.insert(v).second)return;auto found=defs.find(v);if(found!=defs.end())for(Id a:(*found->second)[2])visit(a);};for(Id v:roots)visit(v);return needed;
 }
 void values(const std::set<Id>&needed){for(const auto&b:topology["glue"]["inputs"])if(needed.count(b["value"].get<Id>()))binding(b);for(const auto&o:graph["operations"])if(needed.count(o[1].get<Id>()))operation(o);}
 void evaluate(bool advance,unsigned lane=0){bool parallel=advance&&workers>1;f<<(parallel?"static ":"")<<"void snoc_"<<(parallel?"lane_"+std::to_string(lane):advance?"step":"eval")<<"(void*context){Model*m=context;";
  if(workers>1)f<<"State*s=&m->state[m->bank];Fifo*fifo=s->fifo;Router*router=s->router;";else f<<"Fifo*fifo=m->fifo;Router*router=m->router;";
  if(parallel)f<<"State*next=&m->state[m->bank^1];Fifo*nfifo=next->fifo;Router*nrouter=next->router;";else f<<"publish("<<(workers>1?"s":"m")<<");";f<<'\n';
  std::map<std::pair<unsigned,unsigned>,Id>qin,ready;
  std::set<Id> roots;
  for(const auto&b:topology["glue"]["outputs"]){Id v=b["value"];std::string source=b["source"];
   if(source=="endpoint"){if(!advance)roots.insert(v);}
   else if(source=="fifo_input")qin[{b["object"],b["input"]}]=v;
   else if(source=="target_ready")ready[{b["router"],b["target"]}]=v;
   if(advance&&((source=="target_ready"&&(!parallel||router_owner[b["router"].get<unsigned>()]==lane))||(source=="fifo_input"&&b["input"]!=2&&(!parallel||fifo_owner.at(b["object"].get<Id>())==lane))))roots.insert(v);
  }
  auto controls=closure(roots);values(controls);
  if(!advance)for(const auto&b:topology["glue"]["outputs"])if(b["source"]=="endpoint"){Id v=b["value"];f<<"memcpy(m->outputs+"<<output_offsets.at(b["name"].get<std::string>())<<",v"<<v<<","<<words(width(v))*8<<");\n";}
  if(advance){
   // Stage only accepted payloads, completing every old-state read before any
   // FIFO publishes. Each payload cone has a bounded local lifetime.
   if(!parallel){f<<"uint64_t pending["<<topology["objects"].size()<<"][4];bool enqueue["<<topology["objects"].size()<<"];\n";
    for(unsigned q=0;q<topology["objects"].size();++q){const auto&o=topology["objects"][q];if(o[0]!=1)continue;Id data=qin.at({q,2});f<<"enqueue["<<q<<"]="<<cv(qin.at({q,1}))<<"&&!fifo["<<q<<"].full;if(enqueue["<<q<<"]){\n";values(closure({data},controls));f<<"memcpy(pending["<<q<<"],v"<<data<<","<<words(o[1].get<unsigned>())*8<<");}\n";}}
   f<<"uint8_t pop[96]={0};\n";
   for(unsigned r=0;r<topology["routers"].size();++r){if(parallel&&router_owner[r]!=lane)continue;const auto&router=topology["routers"][r];unsigned inputs=router["inputs"].size(),outputs=router["outputs"].size();Id reset=topology["objects"][router["matcher"].get<Id>()][4][0];f<<"{Router*x=&"<<(parallel?"nrouter":"router")<<"["<<r<<"];";if(parallel)f<<"*x=router["<<r<<"];";
    for(unsigned t=0;t<outputs;++t){f<<"if((x->valid>>"<<t<<"&1)&&"<<cv(ready.at({r,t}))<<"){unsigned winner=x->winner["<<t<<"];pop["<<r<<"]|=1u<<winner;x->priority["<<t<<"]=(winner+1=="<<inputs<<")?0:winner+1;x->dirty=true;}";}f<<"if("<<cv(reset)<<"){memset(x->priority,0,7);x->dirty=true;}}\n";
   }
   for(unsigned q=0;q<topology["objects"].size();++q){const auto&o=topology["objects"][q];if(o[0]!=1||(parallel&&fifo_owner.at(q)!=lane))continue;std::string pop=cv(qin.count({q,3})?qin.at({q,3}):qin.at({q,0}));auto owner=fifo_router.find(q);if(owner!=fifo_router.end())pop="((pop["+std::to_string(owner->second.first)+"]>>"+std::to_string(owner->second.second)+")&1)";
    std::string enqueue=parallel?"enqueue":"enqueue["+std::to_string(q)+"]";f<<"{Fifo*x=&"<<(parallel?"nfifo":"fifo")<<"["<<q<<"];";if(parallel)f<<"*x=fifo["<<q<<"];bool enqueue="<<cv(qin.at({q,1}))<<"&&!x->full;";
    f<<"bool next="<<cv(qin.at({q,0}))<<"?0:x->full?!"<<pop<<":"<<cv(qin.at({q,1}))<<";if("<<enqueue<<"){";
    if(parallel){Id data=qin.at({q,2});values(closure({data},controls));f<<"memcpy(x->data,v"<<data<<","<<words(o[1].get<unsigned>())*8<<");";}else f<<"memcpy(x->data,pending["<<q<<"],"<<words(o[1].get<unsigned>())*8<<");";
    if(owner!=fifo_router.end()){const auto&entry=topology["routers"][owner->second.first]["inputs"][owner->second.second];unsigned offset=entry["route_offset"],size=entry["route_table"].size();require(offset%64+unsigned(__builtin_ctz(size))<=64,"route field crosses a host word");f<<"x->request=route_"<<q<<"[(x->data["<<offset/64<<"]>>"<<offset%64<<")&"<<size-1<<"];";}f<<"}";
    if(owner!=fifo_router.end())f<<(parallel?"nrouter":"router")<<"["<<owner->second.first<<"].dirty|="<<enqueue<<"||(x->full!=next);";
    f<<"x->full=next;}\n";
   }
  }
  if(parallel)f<<"publish_"<<lane<<"(&m->state[m->bank^1]);";f<<"}\n";
 }
 void run(const char*tp,const char*gp,unsigned count){workers=count;require(workers==1||workers==8,"software workers must be one or eight");std::ifstream(tp)>>topology;std::ifstream(gp)>>graph;require(topology["format"]=="rhodium-eight-hart-noc-software-v1","wrong topology");
  for(const auto&p:topology["ports"]){Id v=p[1];std::string name=p[2];if(p[0]==0){input_offsets[name]=ni;ni+=words(width(v));}else{output_offsets[name]=no;no+=words(width(v));}}
  require(ni==288&&no==260,"wrong eight-hart boundary");
  std::map<std::string,unsigned>sites;for(const auto&r:topology["routers"]){std::string path=r["path"],site=path.substr(0,path.rfind('/'));auto[it,inserted]=sites.emplace(site,sites.size());(void)inserted;router_owner.push_back(workers>1?it->second/3:0);}require(sites.size()==24,"wrong site partition");
  for(Id q=0;q<topology["objects"].size();++q)if(topology["objects"][q][0]==1){std::string path=topology["objects"][q][5];auto at=path.find("/router/");require(at!=std::string::npos,"unowned software FIFO");fifo_owner[q]=workers>1?sites.at(path.substr(0,at+7))/3:0;}
  f<<"/* Models the retained eight-hart CHI NoC with direct one-entry FIFOs and output-greedy routers. */\n#include <stdint.h>\n#include <stdbool.h>\n#include <stdlib.h>\n#include <string.h>\n";
  if(workers>1)f<<"#include \"soc-noc-software-runtime.h\"\n";
  f<<"typedef struct{uint64_t data[4];bool full;uint8_t request;}Fifo;\ntypedef struct{uint8_t priority[7],winner[7],valid;bool dirty;}Router;\n";
  if(workers>1)f<<"typedef struct{Fifo fifo["<<topology["objects"].size()<<"];Router router[96];}State;\ntypedef struct{State state[2];uint64_t inputs[288],outputs[260];snoc_pool pool;unsigned bank;}Model;\n";
  else f<<"typedef struct{Fifo fifo["<<topology["objects"].size()<<"];Router router[96];uint64_t inputs[288],outputs[260];}Model;\n";
  f<<"static const unsigned router_fifo[96][7]={\n";
  for(unsigned r=0;r<96;++r){f<<'{';unsigned index=0;for(const auto&in:topology["routers"][r]["inputs"]){Id q=in["fifo"];f<<q<<',';fifo_router[q]={r,index++};}f<<"},\n";}f<<"};\n";
  for(const auto&[q,owner]:fifo_router){f<<"static const uint8_t route_"<<q<<"[]={";for(unsigned entry:topology["routers"][owner.first]["inputs"][owner.second]["route_table"])f<<entry<<',';f<<"};\n";}
  for(unsigned lane=0;lane<workers;++lane){if(workers>1)f<<"static void publish_"<<lane<<"(State*s){Fifo*fifo=s->fifo;Router*router=s->router;\n";else f<<"static void publish(Model*m){Fifo*fifo=m->fifo;Router*router=m->router;\n";
  for(unsigned r=0;r<96;++r){if(router_owner[r]!=lane)continue;const auto&router=topology["routers"][r];unsigned n=router["inputs"].size(),targets=router["outputs"].size();f<<"{Router*x=&router["<<r<<"];if(x->dirty){x->dirty=false;unsigned requests[7]={0},taken=0,any=0;x->valid=0;";
   for(unsigned i=0;i<n;++i){unsigned q=router["inputs"][i]["fifo"];f<<"if(fifo["<<q<<"].full){unsigned mask=fifo["<<q<<"].request;any|=mask;while(mask){unsigned target=__builtin_ctz(mask);mask&=mask-1;requests[target]|="<<(1u<<i)<<";}}";}
   f<<"if(!any){memset(x->winner,0,7);}else{for(unsigned target=0;target<"<<targets<<";++target){unsigned request=requests[target]&~taken;unsigned upper=request&(~0u<<x->priority[target]);unsigned grant=(upper?upper:request);grant&=-grant;taken|=grant;x->winner[target]=grant?__builtin_ctz(grant):0;x->valid|=(grant!=0)<<target;}}}}\n";
  }f<<"}\n";}
  if(workers>1){f<<"static void publish(State*s){";for(unsigned lane=0;lane<workers;++lane)f<<"publish_"<<lane<<"(s);";f<<"}\n";}
  evaluate(false);for(unsigned lane=0;lane<workers;++lane)evaluate(true,lane);
  if(workers>1){f<<"static const snoc_work work[]={";for(unsigned lane=0;lane<workers;++lane)f<<"snoc_lane_"<<lane<<",";f<<"};\nvoid*snoc_create(void){Model*m=aligned_alloc(64,sizeof(Model));if(!m)return NULL;memset(m,0,sizeof(*m));if(!snoc_pool_init(&m->pool,m,work,8)){free(m);return NULL;}return m;}\nvoid snoc_destroy(void*context){Model*m=context;snoc_pool_destroy(&m->pool);free(m);}\nvoid snoc_step(void*context){Model*m=context;snoc_pool_run(&m->pool);m->bank^=1;}\n";}
  else f<<"void*snoc_create(void){return calloc(1,sizeof(Model));}\nvoid snoc_destroy(void*m){free(m);}\n";
  f<<"unsigned snoc_workers(void){return "<<workers<<";}\nuint64_t*snoc_inputs(void*context){return ((Model*)context)->inputs;}\nconst uint64_t*snoc_outputs(void*context){return ((Model*)context)->outputs;}\nsize_t snoc_storage(void){return sizeof(Model);}\n";
 }
};
int main(int argc,char**argv){try{require(argc==3||argc==4,"soc-noc-software-codegen TOPOLOGY.json GLUE.json [1|8]");Generator g;g.run(argv[1],argv[2],argc==4?std::stoul(argv[3]):1);}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
