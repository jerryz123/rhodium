// Checks NoC software/control equivalence and summarizes repeated timing ablations.
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
using Json=nlohmann::json;
int main(int argc,char**argv){try{if(argc<2)throw std::runtime_error("supply measurement JSONL files");std::map<std::string,Json>controls,payloads;std::map<std::string,std::vector<double>>samples;Json storage=Json::object();
 for(int i=1;i<argc;++i){std::ifstream in(argv[i]);if(!in)throw std::runtime_error("missing measurement file");std::string line;while(std::getline(in,line)){auto r=Json::parse(line);
  if(r.at("mode")=="recorded-boundary"){
   std::string workload="replay/"+std::to_string(r.at("window").get<unsigned>())+"/"+std::to_string(r.at("repeats").get<unsigned>());
   if(payloads.count(workload)&&payloads[workload]!=r.at("signature"))throw std::runtime_error("replay signature mismatch");payloads[workload]=r.at("signature");
   if(controls.count(workload)&&controls[workload]!=r.at("checksum"))throw std::runtime_error("replay checksum mismatch");controls[workload]=r.at("checksum");
   std::string key=workload+"/"+r.at("layout").get<std::string>()+"/w"+std::to_string(r.at("workers").get<unsigned>());
   samples[key].push_back(r.at("ns_per_cycle"));continue;
  }
  std::string workload=std::to_string(r.at("nodes").get<unsigned>())+"/"+std::to_string(r.at("cycles").get<uint64_t>())+"/"+std::to_string(r.at("traffic").get<unsigned>());
  if(controls.count(workload)&&controls[workload]!=r.at("controls"))throw std::runtime_error("control mismatch");controls[workload]=r.at("controls");
  if(r.at("mode")!="perf"){if(payloads.count(workload)&&payloads[workload]!=r.at("signature"))throw std::runtime_error("functional signature mismatch");payloads[workload]=r.at("signature");}
  std::string key=workload+"/"+r.at("mode").get<std::string>()+"/"+r.at("layout").get<std::string>()+"/"+r.at("sync").get<std::string>()+"/u"+std::to_string(r.at("unroll").get<unsigned>())+"/w"+std::to_string(r.at("workers").get<unsigned>());
  samples[key].push_back(r.at("ns_per_cycle"));if(r.contains("storage_bytes"))storage[key]=r.at("storage_bytes");
 }}Json out=Json::object();for(auto&[key,v]:samples){if(v.size()<3)throw std::runtime_error("need three trials: "+key);std::sort(v.begin(),v.end());out[key]={{"median_ns",(v[(v.size()-1)/2]+v[v.size()/2])/2},{"min_ns",v.front()},{"max_ns",v.back()},{"trials",v.size()}};if(storage.contains(key))out[key]["storage_bytes"]=storage[key];}out["checked_workloads"]=controls.size();std::cout<<out.dump(2)<<'\n';
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
