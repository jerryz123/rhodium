// Checks contract-guided grouping against independent arithmetic and real DFG sharing.
#include "../../rhodium/sim/compiler/model.hpp"
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>
using namespace rds;
static void check(bool p,const char*s){if(!p)throw std::runtime_error(s);}
static std::vector<uint64_t> evaluate(const Model&m,uint64_t x,uint64_t y){
 std::vector<uint64_t> v(m.widths.size());std::set<Id> ready={0,1};v[0]=x;v[1]=y;
 for(const auto&o:m.ops){for(Id a:o.args)check(ready.count(a),"use before definition");
  switch(o.code){case 0:v[o.out]=o.imm[0];break;case 5:v[o.out]=v[o.args[0]]^v[o.args[1]];break;case 6:v[o.out]=v[o.args[0]]+v[o.args[1]];break;case 8:v[o.out]=v[o.args[0]]*v[o.args[1]];break;default:throw std::runtime_error("fixture operation");}ready.insert(o.out);}
 return v;
}
int main(){try{
 Model m;m.widths={64,64};m.metadata={{"format","rhodium-simulation-ir-v1"},{"ports",Json::array({{0,0,"x"},{0,1,"y"}})},{"occurrences",Json::array({"flow"})}};
 for(auto k:{"registers","memories","writes","reads","assertions","objects","origins","inventory"})m.metadata[k]=Json::array();
 auto op=[&](unsigned c,std::vector<Id>a,std::vector<uint64_t>im=std::vector<uint64_t>{}){Id id=m.widths.size();m.widths.push_back(64);m.ops.push_back({c,id,a,im});return id;};
 Id odd=op(0,{}, {3}),left=0,right=1,shared=op(5,{0,1});
 for(unsigned i=0;i<40;++i){left=op(i%2?6:8,{left,odd});right=op(i%2?5:6,{right,shared});}
 Id outside=op(5,{left,right});m.metadata["ports"].push_back({1,outside,"result"});m.metadata["ports"].push_back({1,shared,"tap"});
 auto contract=[&](Id input,Id output){return Json::array({0,"map",Json::array({{"in0.bits",input},{"out0.bits",output},{"out0.ready",none}}),Json::array()});};
 m.metadata["contracts"]=Json::array({contract(0,left),contract(1,right),contract(0,left)});m.validate();
 Model original=m;plan_flow_regions(m,true);m.validate();
 check(m.metadata["flow_region_summary"]["groups"]>0,"fixture must exercise grouping");
 check(m.ops.size()==original.ops.size(),"ordering must not duplicate work");
 check(m.metadata["flow_region_summary"]["candidates"].size()==2,"duplicate wrapper region");
 std::mt19937_64 random(1783);for(unsigned i=0;i<10000;++i){auto x=random(),y=random();check(evaluate(original,x,y)==evaluate(m,x,y),"changed arithmetic or shared observer");}
 Model inspected=original;plan_flow_regions(inspected,false);for(size_t i=0;i<m.ops.size();++i)check(inspected.ops[i].out==original.ops[i].out,"analysis changed order");
 Model invalid=original;invalid.metadata["contracts"][0][2][0][1]=invalid.widths.size();bool rejected=false;try{invalid.validate();}catch(const std::exception&){rejected=true;}check(rejected,"dangling contract accepted");
 std::cout<<"flow regions: 10000 independent arithmetic/sharing comparisons passed\n";
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
