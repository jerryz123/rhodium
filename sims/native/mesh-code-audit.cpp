// Correlates retained mesh IR counts with compiled storage and emitted C materialization sites.
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <nlohmann/json.hpp>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
using Json=nlohmann::json;
static size_t occurrences(const std::string&s,const std::string&needle){size_t result=0,pos=0;while((pos=s.find(needle,pos))!=std::string::npos){++result;pos+=needle.size();}return result;}
int main(int argc,char**argv){try{if(argc!=2)throw std::runtime_error("mesh-code-audit FIXTURE");std::string dir=argv[1];Json ir;std::ifstream input(dir+"/model.json");if(!input)throw std::runtime_error("missing model JSON");input>>ir;std::map<unsigned,size_t> ops,widths;for(const auto&o:ir.at("operations"))++ops[o[0].get<unsigned>()];for(const auto&w:ir.at("values"))++widths[w.get<unsigned>()];Json kinds=Json::object(),width_counts=Json::object();for(auto[k,v]:ops)kinds[std::to_string(k)]=v;for(auto[k,v]:widths)width_counts[std::to_string(k)]=v;
 Json result_widths=Json::object();for(const auto&o:ir.at("operations")){auto key=std::to_string(ir.at("values").at(o[1].get<unsigned>()).get<unsigned>());result_widths[key]=result_widths.value(key,0)+1;}
 unsigned policy; if(!(std::ifstream(dir+"/policy")>>policy))throw std::runtime_error("missing policy");char error[512];rds_options options{1,policy};rds_sim*s=rds_load_with_options((dir+"/model.rsim").c_str(),&options,error,sizeof error);if(!s)throw std::runtime_error(error);if(rds_use_compiled(s,(dir+"/w1.so").c_str()))throw std::runtime_error(rds_error(s));auto stats=rds_get_stats(s);auto execution=rds_get_execution_stats(s);
 void*library=dlopen((dir+"/w1.so").c_str(),RTLD_NOW|RTLD_LOCAL);if(!library)throw std::runtime_error(dlerror());auto object_bytes=reinterpret_cast<size_t(*)()>(dlsym(library,"rds_generated_object_bytes"));auto arena_words=reinterpret_cast<size_t(*)()>(dlsym(library,"rds_generated_arena_words"));if(!object_bytes||!arena_words)throw std::runtime_error("missing storage exports");
 std::ifstream source(dir+"/w1.c");if(!source)throw std::runtime_error("missing generated C");std::string code((std::istreambuf_iterator<char>(source)),{});
 Json out={{"operations",ir.at("operations").size()},{"values",ir.at("values").size()},{"op_kinds",kinds},{"value_widths",width_counts},{"semantic_objects",stats.objects},{"registers",stats.registers},{"scheduled_operations",execution.scheduled_operations},{"replicated_operations",execution.replicated_operations},{"arena_bytes",arena_words()*8},{"compiled_object_bytes",object_bytes()},{"register_banks_bytes",stats.update_bytes},{"runtime_object_bytes",stats.object_bytes},{"descriptor_bytes",stats.descriptor_bytes},{"schedule_bytes",stats.schedule_bytes},{"generated_c_bytes",code.size()},{"static_materialization_markers",occurrences(code,"/* rds-store ")},{"static_payload_dirty_tests",occurrences(code,"if(h->payload_dirty_")},{"static_selection_guards",occurrences(code,"if(RDS_UNLIKELY(c->strict||")}};
 out["operation_result_widths"]=result_widths;std::cout<<out.dump(2)<<'\n';dlclose(library);rds_free(s);
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
