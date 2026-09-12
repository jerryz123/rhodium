// Checks demanded structural provenance, CSE/pruning remaps, and unchanged execution images.
#include "../../rhodium/sim/compiler/model.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace rds;
static void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
template <class F> static void rejects(F action) {
  bool rejected=false;
  try { action(); } catch (const std::exception &) { rejected=true; }
  check(rejected,"invalid semantic mapping accepted");
}
static Json fixture() {
  return {{"format","rhodium-simulation-ir-v2"},
    {"opcodes",{"constant","copy","not","and","or","xor","add","sub","mul","shl","shru","shrs","eq","ult","slt","mux_lookup","onehot_mux","extract","zext","sext","pack","vector_index","vector_inject","vector_write_set","memory_read_async","decode","set_clear","balance","counter_step","object_query","alu","byte_merge","record_create","record_get","vector_create","vector_get","reinterpret"}},
    {"types",Json::array({Json::array({"bits",1}),Json::array({"bits",8}),
      Json::array({"record",Json::array({Json::array({"hi",1}),Json::array({"lo",1})})})})},
    {"values",{1,8,8,8,8,16,16,16,8,16,8,16}},
    {"value_types",{0,1,1,1,1,2,2,2,1,2,1,2}},
    {"operations",{{32,5,{1,2},Json::array()},{32,6,{3,4},Json::array()},
      {15,7,{0,5,6},{1}},{33,8,{7},{1}},
      {15,9,{0,5,6},{1}},{33,10,{9},{1}},
      {15,11,{0,5,6},{0}}}},
    {"ports",{{0,0,"select"},{0,1,"a"},{0,2,"b"},{0,3,"c"},{0,4,"d"},
              {1,8,"lo"},{1,10,"duplicate"}}},
    {"registers",Json::array()},{"memories",Json::array()},
    {"writes",Json::array()},{"reads",Json::array()},
    {"assertions",Json::array()},{"objects",Json::array()},
    {"origins",Json::array()},{"inventory",Json::array()},
    {"occurrences",{"top"}},{"conditional_groups",{{0,7,0,{1},"update"},
      {0,9,0,{1},"update"},{0,11,0,{0},"dead"}}}};
}
static Model model(Json j, bool cse=true) {
  Model m;m.metadata=lower_semantic(std::move(j),cse);
  m.widths=m.metadata["values"].get<std::vector<uint32_t>>();
  for (const auto &o:m.metadata["operations"])
    m.ops.push_back({o[0],o[1],o[2].get<std::vector<Id>>(),o[3].get<std::vector<uint64_t>>()});
  m.validate();return m;
}
static std::string bytes(const std::filesystem::path &path) {
  std::ifstream stream(path,std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),{}};
}
int main(int argc,char **argv) {
  try {
    check(argc==2,"supply output directory");
    std::filesystem::path dir=argv[1];std::filesystem::create_directories(dir);
    for (bool cse:{false,true}) {
      auto m=model(fixture(),cse), control=m;
      control.metadata.erase("semantic_structure");
      const auto &groups=m.metadata["semantic_structure"]["conditionals"];
      check(groups.size()==2,"dead conditional retained or live field lost");
      check(groups[0][2]==0 && groups[0][3]==8,"record low field mapping wrong");
      check(groups[0][6]==Json::array({2,4}),"conditional low field arms wrong");
      check(!cse || groups[0][4]==groups[1][4],"source groups lost shared CSE result");
      for (const auto &r:m.metadata["semantic_structure"]["ranges"])
        check(r[0]!=11,"metadata demanded a dead source result");
      auto compare=[&] {
        m.validate();control.validate();
        m.write_binary((dir/"retained.rsim").string());
        control.write_binary((dir/"control.rsim").string());
        check(bytes(dir/"retained.rsim")==bytes(dir/"control.rsim"),"metadata changed execution image");
      };
      compare();
      optimize_body(m);optimize_body(control);compare();
      recover_words(m);recover_words(control);compare();
      regroup_bits(m);regroup_bits(control);compare();
      optimize_body(m);optimize_body(control);compare();
      auto file=dir/"roundtrip.json";std::ofstream(file)<<m.json();
      check(Model::read(file.string()).json()==m.json(),"structural metadata roundtrip changed");
      m.metadata["ports"].erase(m.metadata["ports"].begin()+5,m.metadata["ports"].end());
      release_body(m);m.validate();
      check(m.metadata["semantic_structure"]["conditionals"].empty(),"dead groups survived pruning");
    }
    // A whole record cycle is legal when each demanded field resolves to input.
    auto feedback=fixture();
    feedback["values"]={8,16,8,8};feedback["value_types"]={1,2,1,1};
    feedback["operations"]={{32,1,{0,2},Json::array()},{33,2,{1},{0}},{33,3,{1},{1}}};
    feedback["ports"]={{0,0,"x"},{1,3,"y"}};
    feedback["conditional_groups"]=Json::array();
    auto loop=model(feedback);
    check(loop.metadata["ports"][0][1]==loop.metadata["ports"][1][1],"legal field feedback changed");
    check(loop.metadata["semantic_structure"]["ranges"].size()==2,"field feedback ranges lost");
    feedback["operations"][0][2][0]=2;
    rejects([&]{model(feedback);});
    // Vector element zero is low; reinterpret/project do not retain whole vectors.
    auto vector=fixture();vector["types"].push_back({"vector",2,1});
    vector["values"]={8,8,16,8};vector["value_types"]={1,1,3,1};
    vector["operations"]={{34,2,{0,1},Json::array()},{35,3,{2},{1}}};
    vector["ports"]={{0,0,"a"},{0,1,"b"},{1,3,"y"}};
    vector["conditional_groups"]=Json::array();
    auto vm=model(vector);check(vm.metadata["semantic_structure"]["ranges"][0]==Json::array({2,8,8,1}),"vector field mapping wrong");
    // Lookup keys remain ordered multiword integers, including duplicate keys.
    auto wide=fixture();wide["types"].push_back({"bits",129});
    wide["values"][0]=129;wide["value_types"][0]=3;
    for (unsigned op:{2u,4u,6u}) wide["operations"][op][3]={1,0,1};
    for (auto &g:wide["conditional_groups"])g[3]={1,0,1};
    auto wm=model(wide);optimize_body(wm);wm.validate();
    auto duplicate=fixture();
    duplicate["operations"][2][2].push_back(5);duplicate["operations"][2][3]={1,1};
    duplicate["conditional_groups"][0][3]={1,1};model(duplicate);
    for (unsigned mutation=0;mutation<4;++mutation) {
      auto bad=fixture();
      if (mutation==0)bad["conditional_groups"][0][2]=1;
      if (mutation==1)bad["conditional_groups"][0][3]={0};
      if (mutation==2)bad["conditional_groups"][0][0]=1;
      if (mutation==3)bad["conditional_groups"][0][1]=UINT32_MAX;
      rejects([&]{model(bad);});
    }
    for (unsigned mutation=0;mutation<6;++mutation) {
      auto bad=model(fixture());auto &s=bad.metadata["semantic_structure"];
      if (mutation==0)s["ranges"][0][3]=UINT32_MAX;
      if (mutation==1)s["ranges"][0][1]=UINT32_MAX;
      if (mutation==2)s["conditionals"][0][6][0]=0;
      if (mutation==3)s["conditionals"][0][7]=Json::array();
      if (mutation==4)s["source_types"][0]=UINT32_MAX;
      if (mutation==5) {
        s["ranges"][0][3]=bad.widths.size();
        bad.widths.push_back(s["ranges"][0][2]);
      }
      rejects([&]{bad.validate();});
    }
    std::cout<<"semantic structure: demanded fields, feedback, CSE/pruning, wide keys, malformed mappings and identical images passed\n";
  } catch (const std::exception &e) { std::cerr<<e.what()<<'\n';return 1; }
}
