// Extracts the complete retained eight-hart CHI NoC at its endpoint boundaries without rebuilding RTL.
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
using Json = nlohmann::json;
using Id = uint32_t;
static constexpr Id none = UINT32_MAX;
static void require(bool ok, const std::string &why) { if (!ok) throw std::runtime_error(why); }
static bool starts(const std::string &s, const std::string &prefix) { return s.rfind(prefix, 0) == 0; }
static bool ends(const std::string &s, const std::string &suffix) { return s.size() >= suffix.size() && s.compare(s.size()-suffix.size(), suffix.size(), suffix)==0; }
int main(int argc, char **argv) { try {
  require(argc == 4 || argc == 5, "soc-noc-extract INPUT.json OUTPUT.json MANIFEST.json [OBSERVED_SOC.json]");
  Json source; std::ifstream input(argv[1]); require(bool(input), "cannot open input"); input >> source;
  require(source.at("format") == "rhodium-simulation-ir-v2", "requires original typed extraction with unmerged interface provenance");
  const auto &objects = source.at("objects");
  std::set<std::string> roots, channels, harts;
  for (const auto &o : objects) {
    std::string path = o[5];
    for (const char *channel : {"req", "rsp", "snp", "dat"}) {
      std::string marker = std::string("/router/") + channel + "_router/";
      auto at = path.find(marker);
      if (at == std::string::npos) continue;
      std::string root = path.substr(0, at + 7);
      roots.insert(root); channels.insert(root + "/" + channel);
      auto parent = path.substr(0, at);
      auto name = parent.substr(parent.find_last_of('/') + 1);
      if (name == "rv5stage" || starts(name, "rv5stage_")) harts.insert(parent);
    }
  }
  require(harts.size() == 8 && roots.size() == 24 && channels.size() == 96,
          "expected retained eight-hart, 24-site, four-channel CHI NoC");
  auto inside = [&](const std::string &path) {
    for (const auto &root : roots) if (path == root || starts(path, root + "/")) return true;
    return false;
  };
  const size_t nv = source.at("values").size();
  std::vector<Id> definitions(nv, none), object_map(objects.size(), none);
  for (Id i=0; i<source["operations"].size(); ++i) definitions.at(source["operations"][i][1].get<Id>()) = i;
  std::map<Id, std::string> inputs, outputs;
  for (const auto &p : source.at("ports")) if (p[0] == 0 && (p[2] == "reset" || p[2] == "clock")) inputs[p[1]] = p[2];
  auto bind = [&](std::map<Id,std::string> &ports, const Json &origin, const std::string &name) {
    Id id = origin[0];
    require(origin[5] == 0 && origin[6] == source["values"][id], "partial endpoint origin: " + name);
    auto [it, added] = ports.emplace(id, name);
    require(added || it->second == name, "merged endpoint origins: " + name);
  };
  for (const auto &o : source.at("origins")) {
    std::string path = o[1], name = o[3], opcode = o[4];
    if (roots.count(path) && opcode == "rtl.input_port" && name.find("_local_") != std::string::npos && ends(name,"_in"))
      bind(inputs, o, path + "/" + name);
    if (opcode == "rtl.instance" && starts(name, "router.") && name.find("_local_") != std::string::npos && ends(name,"_out") && roots.count(path + "/router"))
      bind(outputs, o, path + "/" + name);
  }
  require(inputs.size() > 2 && outputs.size() > 0, "missing endpoint boundaries");
  Json result = source, manifest;
  for (const char *section : {"objects","ports","registers","memories","reads","writes","assertions","contracts","operations","origins","conditional_groups"}) result[section] = Json::array();
  std::vector<unsigned char> needed(nv), cuts(nv);
  std::vector<Id> stack;
  auto need = [&](Id v) { if (v != none && !needed.at(v)) {needed[v]=1;stack.push_back(v);} };
  for (const auto &[id,name] : inputs) { cuts[id]=1; result["ports"].push_back({0,id,name}); }
  for (const auto &[id,name] : outputs) { result["ports"].push_back({1,id,name}); need(id); }
  std::map<std::string,unsigned> kinds, geometry;
  for (Id i=0; i<objects.size(); ++i) if (inside(objects[i][5])) {
    object_map[i] = result["objects"].size(); result["objects"].push_back(objects[i]);
    for (Id v : objects[i][4]) need(v);
    kinds[std::to_string(objects[i][0].get<unsigned>())]++;
    geometry[std::to_string(objects[i][0].get<unsigned>()) + ":" + std::to_string(objects[i][1].get<unsigned>()) + ":" + std::to_string(objects[i][2].get<unsigned>())]++;
  }
  for (const auto &a : source.at("assertions")) if (inside(a[3])) {
    result["assertions"].push_back(a); for (unsigned i=0;i<3;++i) need(a[i]);
  }
  // Keep annotation bindings only for the selected hardware, so later compiler
  // passes can use the same contract semantics as the complete SoC.
  for (const auto &c : source.at("contracts")) if (inside(source["occurrences"][c[0].get<Id>()])) {
    result["contracts"].push_back(c); for (const auto &binding : c[2]) need(binding[1]);
  }
  while (!stack.empty()) {
    Id v=stack.back(); stack.pop_back(); if (cuts[v]) continue;
    require(definitions[v] != none, "unaccounted external state/value " + std::to_string(v));
    const auto &o=source["operations"][definitions[v]];
    if (o[0] == 29) require(object_map.at(o[3][0].get<Id>()) != none, "cone escaped NoC at object " + objects[o[3][0].get<Id>()][5].get<std::string>());
    require(o[0] != 24, "NoC boundary unexpectedly includes SRAM");
    for (Id a : o[2]) need(a);
  }
  for (auto o : source["operations"]) if (needed[o[1].get<Id>()] && !cuts[o[1].get<Id>()]) {
    if (o[0] == 29) o[3][0] = object_map[o[3][0].get<Id>()];
    result["operations"].push_back(std::move(o));
  }
  for (const auto &o : source["origins"]) if (needed[o[0].get<Id>()] || cuts[o[0].get<Id>()]) result["origins"].push_back(o);
  for (const auto &g : source["conditional_groups"]) if (needed[g[1].get<Id>()] && !cuts[g[1].get<Id>()]) result["conditional_groups"].push_back(g);
  manifest = {{"source",argv[1]}, {"reference","eight-hart banked RV5Stage CHI NoC"},
    {"sites",roots},{"channel_instances",channels},{"hart_tiles",harts},
    {"input_boundary",Json::array()},{"output_boundary",Json::array()},
    {"object_kinds",kinds},{"object_geometry_kind_width_depth",geometry},
    {"objects",result["objects"].size()},{"operations",result["operations"].size()},
    {"assertions",result["assertions"].size()},{"contracts",result["contracts"].size()},
    {"timing_claim",false},{"scope","all CHI links and endpoint attachment stages; separate interrupt network excluded"}};
  for (const auto &[id,name] : inputs) manifest["input_boundary"].push_back({id,source["values"][id],name});
  for (const auto &[id,name] : outputs) manifest["output_boundary"].push_back({id,source["values"][id],name});
  std::ofstream output(argv[2]); output << result.dump() << '\n'; require(bool(output),"cannot write output");
  std::ofstream report(argv[3]); report << manifest.dump(2) << '\n'; require(bool(report),"cannot write manifest");
  if (argc == 5) {
    for (const auto &[id,name] : inputs) source["ports"].push_back({1,id,"noc_input/" + name});
    for (const auto &[id,name] : outputs) source["ports"].push_back({1,id,"noc_output/" + name});
    std::ofstream observed(argv[4]); observed << source.dump() << '\n'; require(bool(observed),"cannot write observed SoC");
  }
  std::cout << "Extracted " << result["objects"].size() << " objects, " << result["operations"].size()
            << " operations, " << inputs.size() << " inputs, " << outputs.size() << " outputs\n";
} catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
