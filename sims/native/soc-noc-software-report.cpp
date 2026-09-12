// Validates repeated actual-NoC native/software timings and reports the 20-percent timing gate.
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>
using J=nlohmann::json;
static void require(bool b,const char*s){if(!b)throw std::runtime_error(s);}
int main(int argc,char**argv){try{
 require(argc>=4,"soc-noc-software-report DIR VARIANT VARIANT [VARIANT ...]");J signature,rows=J::array();std::set<unsigned>software_workers;double native=std::numeric_limits<double>::infinity(),software=native;
 for(int item=2;item<argc;++item){std::vector<double>times,seconds;std::string engine;unsigned workers=0;
  for(unsigned trial=0;trial<3;++trial){J row;std::ifstream file(std::string(argv[1])+"/"+argv[item]+"-"+std::to_string(trial)+".json");require(bool(file),"missing trial");file>>row;
   J sig;for(auto name:{"reference","frames","measured_cycles","terminal_digest","changed_words","fixed_input_runs","checks_inside_timing","trace_decode_inside_timing","input_delivery_inside_timing"})sig[name]=row.at(name);
   require(sig.at("reference")=="eight-hart RV5Stage CHI NoC recorded endpoints","requires the actual eight-hart SoC NoC reference");
   if(signature.is_null())signature=sig;else require(signature==sig,"traffic signatures differ");
   require(row.at("checks_inside_timing")==false&&row.at("trace_decode_inside_timing")==false&&row.at("input_delivery_inside_timing")==true,"incompatible measurement policies");
   std::string current;if(row.contains("mode")){require(row.at("mode")=="bulk","native engine must use bulk stepping");current="native";}else{require(row.at("engine")=="functional-software"&&row.at("timing_claim")==true,"not a software measurement");current="software";}
   unsigned count=row.at("workers");if(trial)require(engine==current&&workers==count,"variant engine changed");engine=current;workers=count;
   double ns=row.at("ns_per_cycle"),elapsed=row.at("seconds");uint64_t cycles=row.at("measured_cycles");require(cycles&&std::isfinite(ns)&&ns>0&&std::isfinite(elapsed)&&elapsed>0,"invalid timing");require(std::abs(ns-elapsed*1e9/cycles)<1e-5,"timing fields disagree");times.push_back(ns);seconds.push_back(elapsed);
  }
  std::sort(times.begin(),times.end());if(engine=="native"){require(workers==8,"native comparison needs eight workers");native=std::min(native,times[1]);}else{require(workers==1||workers==8,"unexpected software workers");software_workers.insert(workers);software=std::min(software,times[1]);}
  rows.push_back({{"variant",argv[item]},{"engine",engine},{"workers",workers},{"trials",times.size()},{"median_ns_per_cycle",times[1]},{"min_ns_per_cycle",times.front()},{"max_ns_per_cycle",times.back()},{"shortest_measured_seconds",*std::min_element(seconds.begin(),seconds.end())}});
 }
 require(std::isfinite(native)&&software_workers==std::set<unsigned>({1,8}),"requires native eight and software one/eight workers");
 std::cout<<J({{"signature",signature},{"rows",rows},{"best_native_eight_ns",native},{"best_software_ns",software},{"native_to_software_cycle_time",native/software},{"within_20_percent_timing",native<=1.2*software},{"gate_scope","timings only; functional coverage, compiler flags and artifact correspondence require separate validation"}}).dump(2)<<'\n';
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
