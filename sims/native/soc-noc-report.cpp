// Validates repeated actual-SoC NoC trace measurements before comparing native policies and worker counts.
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
using Json=nlohmann::json;
int main(int argc,char**argv){try{
 if(argc!=2)throw std::runtime_error("soc-noc-report measurements.jsonl");std::ifstream input(argv[1]);if(!input)throw std::runtime_error("cannot read measurements");
 using Key=std::pair<std::string,unsigned>;std::map<Key,std::vector<double>> groups;Json signature;
 for(std::string line;std::getline(input,line);){if(line.empty())continue;auto row=Json::parse(line);
  if(row.at("mode")!="bulk"||row.at("checks_inside_timing")!=false||row.at("trace_decode_inside_timing")!=false||row.at("input_delivery_inside_timing")!=true)throw std::runtime_error("measurement policies differ");
  Json current;for(auto name:{"reference","frames","measured_cycles","terminal_digest","changed_words","fixed_input_runs"})current[name]=row.at(name);
  if(signature.is_null())signature=current;else if(signature!=current)throw std::runtime_error("trace signatures differ");
  unsigned workers=row.at("workers");if(workers!=1&&workers!=8)throw std::runtime_error("unexpected worker count");
  double ns=row.at("ns_per_cycle");if(!std::isfinite(ns)||ns<=0)throw std::runtime_error("invalid timing");groups[{row.at("model").get<std::string>(),workers}].push_back(ns);
 }
 if(groups.size()!=4)throw std::runtime_error("requires two models with one/eight workers");
 std::map<Key,double> medians;for(auto &[key,values]:groups){if(!groups.count({key.first,1})||!groups.count({key.first,8}))throw std::runtime_error("incomplete matrix");if(values.size()<3)throw std::runtime_error("requires three trials");std::sort(values.begin(),values.end());medians[key]=(values[(values.size()-1)/2]+values[values.size()/2])/2;}
 Json rows=Json::array();std::string baseline=groups.begin()->first.first;
 for(const auto &[key,values]:groups){if(!medians.count({key.first,1})||!medians.count({baseline,key.second}))throw std::runtime_error("incomplete matrix");rows.push_back({{"model",key.first},{"workers",key.second},{"trials",values.size()},{"median_ns_per_cycle",medians.at(key)},{"min_ns_per_cycle",values.front()},{"max_ns_per_cycle",values.back()},{"speedup_over_same_model_one_worker",medians.at({key.first,1})/medians.at(key)},{"speedup_over_baseline_same_workers",medians.at({baseline,key.second})/medians.at(key)}});}
 std::cout<<Json({{"signature",signature},{"baseline",baseline},{"rows",rows},{"functional_software_comparison",false},{"includes_input_delivery",true}}).dump(2)<<'\n';
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
