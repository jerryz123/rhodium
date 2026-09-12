// Recovers concrete queue, crossbar and routing-table semantics for the eight-hart SoC software reference.
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
using J=nlohmann::json;
using Id=uint32_t;
constexpr Id none=UINT32_MAX;
static void require(bool ok,const std::string&s){if(!ok)throw std::runtime_error(s);}
struct Wire{Id object=none,bit=0;};
struct Export {
 J model,routers=J::array();std::vector<Id> defs;std::map<std::string,Id> objects;std::map<Id,Id> fifo_router;std::map<Id,std::vector<unsigned>> routes;std::map<Id,unsigned> route_offsets;
 const J&op(Id v)const{require(v<defs.size()&&defs[v]!=none,"undefined value "+std::to_string(v));return model["operations"][defs[v]];}
 Wire wire(Id v,unsigned bit,unsigned depth=0)const{
  require(depth<100,"deep route selector");if(bit>=model["values"][v].get<unsigned>())return{};
  const auto&o=op(v);unsigned code=o[0];auto args=o[2].get<std::vector<Id>>();auto im=o[3].get<std::vector<uint64_t>>();
  if(!code)return{none,unsigned((im.at(bit/64)>>(bit%64))&1)};
  if(code==1||code==18)return wire(args[0],bit,depth+1);
  if(code==17)return wire(args[0],bit+im[0],depth+1);
  if(code==20){for(Id a:args){unsigned width=model["values"][a];if(bit<width)return wire(a,bit,depth+1);bit-=width;}throw std::runtime_error("pack overflow");}
  if(code==29&&im[1]==3&&model["objects"][im[0]][0]==1)return{Id(im[0]),bit};
  throw std::runtime_error("non-wire route selector v"+std::to_string(v)+" opcode "+std::to_string(code));
 }
 Id query_object(Id v,unsigned kind)const{const auto&o=op(v);require(o[0]==29&&o[3][1]==kind,"crossbar input not a FIFO query");return o[3][0];}
 J glue(J&description){
  std::map<Id,J> cuts;J outputs=J::array();
  for(const auto&p:model["ports"])if(p[0]==0)cuts[p[1].get<Id>()]={{"source","endpoint"},{"name",p[2]}};else outputs.push_back({{"value",p[1]},{"source","endpoint"},{"name",p[2]}});
  for(const auto&o:model["operations"])if(o[0]==29&&model["objects"][o[3][0].get<Id>()][0]==1){require(o[2].empty(),"ordinary FIFO query has operands");cuts[o[1].get<Id>()]={{"source","fifo"},{"object",o[3][0]},{"query",o[3][1]}};}
  for(unsigned index=0;index<routers.size();++index){const auto&r=routers[index];for(unsigned target=0;target<r["outputs"].size();++target){const auto&o=r["outputs"][target];for(auto field:{"valid","bits"}){Id v=o[field];if(v!=none&&op(v)[0]!=0&&!cuts.count(v))cuts[v]={{"source","router"},{"router",index},{"target",target},{"field",field}};}if(o["ready"]!=none)outputs.push_back({{"value",o["ready"]},{"source","target_ready"},{"router",index},{"target",target}});}}
  for(Id q=0;q<model["objects"].size();++q){const auto&o=model["objects"][q];if(o[0]!=1)continue;for(unsigned input=0;input<o[4].size();++input){if(input==3&&fifo_router.count(q))continue;outputs.push_back({{"value",o[4][input]},{"source","fifo_input"},{"object",q},{"input",input}});}}
  std::vector<unsigned char> needed(defs.size());std::vector<Id> todo;for(auto&o:outputs)todo.push_back(o["value"]);while(!todo.empty()){Id v=todo.back();todo.pop_back();require(v!=none,"missing required glue output");if(needed[v])continue;needed[v]=1;if(cuts.count(v))continue;const auto&o=op(v);require(o[0]!=29,"router replacement left an object query v"+std::to_string(v));for(Id a:o[2])todo.push_back(a);}
  J result=model;for(auto name:{"operations","ports","objects","contracts","origins","inventory"})result[name]=J::array();result.erase("semantic_structure");
  J inputs=J::array();unsigned input_words=0,output_words=0;for(auto&[v,binding]:cuts)if(needed[v]){binding["value"]=v;binding["width"]=model["values"][v];binding["offset"]=input_words;input_words+=(model["values"][v].get<unsigned>()+63)/64;inputs.push_back(binding);result["ports"].push_back({0,v,"cut_"+std::to_string(v)});}
  for(auto&binding:outputs){Id v=binding["value"];binding["width"]=model["values"][v];binding["offset"]=output_words;output_words+=(model["values"][v].get<unsigned>()+63)/64;result["ports"].push_back({1,v,"result_"+std::to_string(result["ports"].size())});}
  std::map<std::string,unsigned>codes;for(const auto&o:model["operations"]){Id v=o[1];if(needed[v]&&!cuts.count(v)){result["operations"].push_back(o);++codes[std::to_string(o[0].get<unsigned>())];}}
  description={{"inputs",inputs},{"outputs",outputs},{"input_words",input_words},{"output_words",output_words},{"operations",result["operations"].size()},{"opcodes",codes}};return result;
 }
 void run(const std::string&path,const std::string&glue_path){std::ifstream(path)>>model;require(model["format"]=="rhodium-simulation-ir-v1","requires pre-contract word IR");defs.assign(model["values"].size(),none);for(Id i=0;i<model["operations"].size();++i)defs[model["operations"][i][1].get<Id>()]=i;
  for(auto section:{"registers","memories","reads","writes","assertions"})require(model[section].empty(),std::string("unexpected ")+section);
  for(Id i=0;i<model["objects"].size();++i){const auto&o=model["objects"][i];require((o[0]==1&&o[1]>0&&o[1]<=256&&o[2]==1&&o[3]==0)||(o[0]==10&&o[1]>0&&o[1]<=7&&o[2]>0&&o[2]<=7&&o[3]==32),"state contract exceeds the reference model's queue/router shape");require(objects.emplace(o[5].get<std::string>(),i).second,"duplicate object path");}
  std::set<Id> matchers;
  for(const auto&c:model["contracts"]){if(c[1]!="grant-crossbar")continue;std::string path=model["occurrences"][c[0].get<Id>()];auto found=objects.find(path+"/allocator/matcher");require(found!=objects.end(),"unowned crossbar "+path);Id matcher=found->second;require(matchers.insert(matcher).second,"duplicate crossbar");const auto&o=model["objects"][matcher];unsigned inputs=o[1],outputs=o[2];std::map<std::string,Id>b;for(const auto&v:c[2])b[v[0]]=v[1];J r={{"path",path},{"matcher",matcher},{"inputs",J::array()},{"outputs",J::array()}};
   for(unsigned i=0;i<inputs;++i){std::string stem="in"+std::to_string(i);Id q=query_object(b.at(stem+".bits"),3);require(query_object(b.at(stem+".valid"),2)==q,"crossbar valid/payload mismatch");require(fifo_router.emplace(q,routers.size()).second,"shared ingress FIFO");require(model["objects"][q][5].get<std::string>().rfind(path+"/queue_stage",0)==0,"nonlocal ingress FIFO");r["inputs"].push_back({{"fifo",q},{"valid",b.at(stem+".valid")},{"bits",b.at(stem+".bits")},{"ready",b.at(stem+".ready")}});}
   for(unsigned i=0;i<outputs;++i){std::string stem="out"+std::to_string(i);r["outputs"].push_back({{"valid",b.at(stem+".valid")},{"bits",b.at(stem+".bits")},{"ready",b.at(stem+".ready")}});}routers.push_back(std::move(r));
  }
  require(routers.size()==96,"expected 96 channel routers");
  for(const auto&o:model["operations"]){if(o[0]!=25)continue;Id v=o[1];unsigned width=model["values"][v];if(width!=13&&width!=15)continue;Id selector=o[2][0];unsigned sw=model["values"][selector];require(sw<=64,"wide route selector");std::vector<Wire>wires;Id queue=none;unsigned bits=0,low=UINT32_MAX;bool routing=true;
   try{for(unsigned i=0;i<sw;++i){auto w=wire(selector,i);wires.push_back(w);if(w.object!=none){if(queue!=none&&queue!=w.object){routing=false;break;}queue=w.object;bits=std::max(bits,w.bit+1);low=std::min(low,w.bit);}}}catch(const std::exception&){continue;}
   if(!routing||queue==none||!fifo_router.count(queue))continue;require(bits==model["objects"][queue][1].get<unsigned>()&&bits-low<=9,"routing reads beyond the high route field");bits-=low;route_offsets[queue]=low;unsigned targets=routers[fifo_router.at(queue)]["outputs"].size();require(width==1+2*targets,"route result width mismatch");auto im=o[3].get<std::vector<uint64_t>>();require(im.size()==2+3*im[0],"route decoder shape");uint64_t mask=(1u<<targets)-1;auto target=[&](uint64_t value){require(((value>>1)&mask)==((value>>(1+targets))&mask),"adaptive routing needs a different software kernel");return unsigned((value&1)?(value>>1)&mask:0);};target(im[1]);for(unsigned row=0;row<im[0];++row)target(im[4+3*row]);std::vector<unsigned> table(1u<<bits);
   for(unsigned key=0;key<table.size();++key){uint64_t select=0;for(unsigned i=0;i<sw;++i)select|=uint64_t(wires[i].object==none?wires[i].bit:(key>>(wires[i].bit-low))&1)<<i;uint64_t value=im[1];for(unsigned row=0;row<im[0];++row)if((select&im[3+3*row])==im[2+3*row]){value=im[4+3*row];break;}table[key]=target(value);}auto[it,added]=routes.emplace(queue,table);require(added||it->second==table,"multiple routing functions for FIFO");
  }
  unsigned inactive=0;for(auto&r:routers)for(unsigned index=0;index<r["inputs"].size();++index){auto&input=r["inputs"][index];Id q=input["fifo"];if(!routes.count(q)){const auto&matcher=model["objects"][r["matcher"].get<Id>()];bool zero=true;for(unsigned target=0;target<r["outputs"].size();++target){try{auto w=wire(matcher[4][1+target],index);zero&=w.object==none&&!w.bit;}catch(const std::exception&){zero=false;}}require(zero,"missing route table for "+model["objects"][q][5].get<std::string>());routes[q]={0};++inactive;}input["route_table"]=routes.at(q);input["route_offset"]=route_offsets[q];}
  J result={{"format","rhodium-eight-hart-noc-software-v1"},{"source",path},{"routers",routers},{"objects",model["objects"]},{"ports",model["ports"]},{"zero_request_inputs",inactive},{"routed_inputs",routes.size()-inactive},{"timing_claim",false}};
  if(!glue_path.empty()){J graph=glue(result["glue"]);std::ofstream output(glue_path);output<<graph.dump()<<'\n';require(bool(output),"cannot write glue graph");}
  std::cout<<result.dump(2)<<'\n';
 }
};
int main(int argc,char**argv){try{require(argc==2||argc==3,"soc-noc-software-export PRE_CONTRACT_WORD_IR.json [GLUE.json]");Export e;e.run(argv[1],argc==3?argv[2]:"");}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
