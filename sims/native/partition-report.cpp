// Compares complete partition benchmark signatures and reports worker scaling medians.
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
using Json=nlohmann::json;
int main(int argc,char**argv){try{if(argc<2)throw std::runtime_error("supply JSONL measurement files");std::map<std::string,Json> signatures;Json report=Json::object();
 for(int a=1;a<argc;++a){std::ifstream f(argv[a]);if(!f)throw std::runtime_error("cannot open measurements");std::string line;std::map<std::string,std::vector<double>>times;
  while(std::getline(f,line)){auto row=Json::parse(line);std::string workload=row.value("kind","star")+"/"+std::to_string(row.value("nodes",size_t(0)))+"/"+std::to_string(row.at("work").get<unsigned>())+"/"+std::to_string(row.at("traffic").get<unsigned>())+"/"+std::to_string(row.at("cycles").get<uint64_t>());
   if(signatures.count(workload)&&signatures.at(workload)!=row.at("signature"))throw std::runtime_error("full signature mismatch in "+std::string(argv[a]));signatures[workload]=row.at("signature");
   if(row.at("actual_workers")!=row.at("requested_workers"))throw std::runtime_error("worker count mismatch");std::string key=workload+"/"+row.at("mode").get<std::string>()+"/"+std::to_string(row.at("actual_workers").get<unsigned>());times[key].push_back(row.at("ns_per_cycle"));
  }
  if(times.empty())throw std::runtime_error("no samples");for(auto &[key,v]:times){if(v.size()<3)throw std::runtime_error("need three samples for "+key);std::sort(v.begin(),v.end());report[argv[a]][key]={{"trials",v.size()},{"median_ns",(v[(v.size()-1)/2]+v[v.size()/2])/2},{"min_ns",v.front()},{"max_ns",v.back()}};}
 }
 report["checked_workloads"]=signatures.size();std::cout<<report.dump(2)<<'\n';
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
