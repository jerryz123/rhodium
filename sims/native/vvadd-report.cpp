// Checks complete multicore replay signatures and summarizes matched timing or
// static worker plans.
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
using Json = nlohmann::json;
static Json read(const char *path) {
  std::ifstream f(path);
  if (!f)
    throw std::runtime_error(std::string("cannot read ") + path);
  Json j;
  f >> j;
  return j;
}
static double median(std::vector<double> a) {
  std::sort(a.begin(), a.end());
  return (a[(a.size() - 1) / 2] + a[a.size() / 2]) / 2;
}
static std::string scope(std::string path) {
  size_t end = 0;
  for (unsigned depth = 0; depth < 3; ++depth) {
    end = path.find('/', end);
    if (end == std::string::npos)
      break;
    ++end;
  }
  return path.substr(0, end);
}
int main(int argc, char **argv) {
  try {
    if (argc < 2)
      throw std::runtime_error(
          "usage: vvadd-report samples.json... | --plan plan.json");
    if (std::string(argv[1]) == "--minimum-seconds") {
      if (argc != 4)
        throw std::runtime_error("--minimum-seconds requires seconds and a Verilator sample");
      size_t consumed = 0;
      double minimum = std::stod(argv[2], &consumed);
      if (consumed != std::string(argv[2]).size() || !std::isfinite(minimum) || minimum < 0)
        throw std::runtime_error("minimum duration must be finite and nonnegative");
      auto sample = read(argv[3]);
      double seconds = sample.at("seconds");
      if (sample.at("engine") != "verilator" || !std::isfinite(seconds) || seconds <= minimum)
        throw std::runtime_error("Verilator measured interval must exceed " +
                                 std::to_string(minimum) + " seconds; increase vvadd rounds");
      return 0;
    }
    if (std::string(argv[1]) == "--components") {
      if (argc != 3)
        throw std::runtime_error("--components requires optimized IR");
      auto ir = read(argv[2]);
      size_t n = ir.at("values").size();
      std::vector<unsigned> parent(n, UINT32_MAX), uses(n);
      std::vector<uint64_t> cost(n);
      std::map<unsigned, std::set<std::string>> origins, consumers;
      for (const auto &o : ir.at("origins"))
        origins[o.at(0)].insert(scope(o.at(1)));
      for (const auto &o : ir.at("operations"))
        if (o.at(0) != 0)
          parent[o.at(1)] = o.at(1);
      auto root = [&](unsigned id) {
        while (parent[id] != id) {
          parent[id] = parent[parent[id]];
          id = parent[id];
        }
        return id;
      };
      for (const auto &o : ir.at("operations"))
        if (o.at(0) != 0)
          for (const auto &a : o.at(2)) {
            unsigned v = a;
            ++uses[v];
            for (const auto &name : origins[o.at(1)])
              consumers[v].insert(name);
            if (parent[v] != UINT32_MAX)
              parent[root(v)] = root(o.at(1));
          }
      std::map<unsigned, Json> groups;
      for (const auto &o : ir.at("operations"))
        if (o.at(0) != 0) {
          unsigned id = o.at(1), r = root(id);
          auto &g = groups[r];
          if (g.is_null())
            g = {{"root", r},
                 {"operations", 0},
                 {"cost", 0},
                 {"scopes", Json::object()}};
          g["operations"] = g["operations"].get<unsigned>() + 1;
          g["cost"] = g["cost"].get<uint64_t>() +
                      (ir.at("values").at(id).get<uint64_t>() + 63) / 64 +
                      o.at(2).size();
          for (const auto &name : origins[id]) {
            unsigned count = g["scopes"].value(name, 0u);
            g["scopes"][name] = count + 1;
          }
        }
      std::vector<Json> ranked;
      for (const auto &[id, g] : groups)
        ranked.push_back(g);
      std::sort(ranked.begin(), ranked.end(), [](const Json &a, const Json &b) {
        return a["cost"].get<uint64_t>() > b["cost"].get<uint64_t>();
      });
      if (ranked.size() > 12)
        ranked.resize(12);
      Json cuts = Json::array();
      for (const auto &o : ir.at("operations")) {
        unsigned id = o.at(1);
        if (o.at(0) != 0 && consumers[id].size() >= 4)
          cuts.push_back({{"value", id},
                          {"width", ir.at("values").at(id)},
                          {"immediates", o.at(3)},
                          {"opcode", o.at(0)},
                          {"args", o.at(2)},
                          {"uses", uses[id]},
                          {"consumer_scopes", consumers[id]}});
      }
      std::sort(cuts.begin(), cuts.end(), [](const Json &a, const Json &b) {
        return a["uses"].get<unsigned>() > b["uses"].get<unsigned>();
      });
      if (cuts.size() > 24)
        cuts.erase(cuts.begin() + 24, cuts.end());
      std::cout
          << Json({{"components", ranked}, {"shared_controls", cuts}}).dump(2)
          << '\n';
      return 0;
    }
    if (std::string(argv[1]) == "--correlate") {
      if (argc != 4)
        throw std::runtime_error(
            "--correlate requires optimized IR and plan JSON");
      auto ir = read(argv[2]), plan = read(argv[3]);
      std::map<unsigned, std::set<std::string>> origins;
      for (const auto &o : ir.at("origins"))
        origins[o.at(0)].insert(scope(o.at(1)));
      Json output = Json::array();
      for (const auto &w : plan.at("workers")) {
        std::map<std::string, unsigned> scopes;
        for (const auto &b : w.at("batches"))
          for (const auto &i : b.at(2)) {
            const auto &names = origins[i.at(4)];
            if (names.empty())
              ++scopes["<no origin>"];
            else if (names.size() > 1)
              ++scopes["<shared origins>"];
            else
              ++scopes[*names.begin()];
          }
        output.push_back(
            {{"worker", w.at("id")}, {"instruction_scopes", scopes}});
      }
      std::cout << output.dump(2) << '\n';
      return 0;
    }
    if (std::string(argv[1]) == "--ir") {
      if (argc != 3)
        throw std::runtime_error("--ir requires an IR JSON");
      auto p = read(argv[2]);
      Json result;
      std::map<std::string, unsigned> widths, opcodes, objects, kinds, contracts, candidates;
      if (p.contains("replication_attempt"))
        result["replication_attempt"] = p["replication_attempt"];
      for (auto key :
           {"values", "operations", "objects", "registers", "memories",
            "writes", "reads", "assertions", "origins", "occurrences"})
        if (p.contains(key))
          result[key] = p[key].size();
      for (const auto &w : p.at("values"))
        ++widths[w.dump()];
      for (const auto &o : p.at("operations"))
        ++opcodes[o.at(0).dump()];
      const char *names[]={"invalid","fifo","pipeline","offer","scoreboard",
          "credit","arbiter","counter","host","broadcast","matcher","tlb"};
      for (const auto &o : p.at("objects")) {
        ++objects[scope(o.at(5))];
        unsigned kind=o.at(0), width=o.at(1), depth=o.at(2);
        if(kind==0 || kind>=sizeof(names)/sizeof(*names))
          throw std::runtime_error("unknown semantic object kind");
        ++kinds[names[kind]];
        if(kind==4&&width<=64)++candidates["scoreboard_word_proposals"];
        if(kind==2&&depth<=64)++candidates["pipeline_control_masks"];
        if(kind==9&&depth<=64)++candidates["broadcast_control_masks"];
        if(kind==6&&depth<=64&&(o.at(3).get<unsigned>()&2))++candidates["arbiter_update_bitsets"];
        if(kind==10){++candidates["sparse_matchers"];candidates["matcher_priority_words"]+=depth;}
      }
      for(const auto &c:p.value("contracts",Json::array()))++contracts[c.at(1).get<std::string>()];
      result["width_counts"] = widths;
      result["opcode_counts"] = opcodes;
      result["object_scopes"] = objects;
      result["object_kind_counts"] = kinds;
      result["contract_kind_counts"] = contracts;
      // Static eligibility is not a dynamic activation count or a speedup claim.
      result["lift_candidates"] = candidates;
      std::cout << result.dump(2) << '\n';
      return 0;
    }
    if (std::string(argv[1]) == "--plan") {
      if (argc != 3)
        throw std::runtime_error("--plan requires a plan JSON");
      auto p = read(argv[2]);
      Json result;
      if (p.contains("partition"))
        result["partition"] = p["partition"];
      result["serial_registers"] = 0;
      for (const auto &r : p.at("registers"))
        if (r.at(1) == UINT32_MAX)
          result["serial_registers"] =
              result["serial_registers"].get<unsigned>() + 1;
      for (auto key : {"replicated_operations", "estimated_work", "peak_work"})
        result[key] = p.at(key);
      result["retained_operation_descriptors"] = p.at("operations").size();
      result["values"] = p.at("values").size();
      unsigned scheduled = 0;
      result["workers"] = Json::array();
      for (const auto &w : p.at("workers")) {
        unsigned id = w.at("id"), ops = 0, regs = 0;
        std::map<std::string, unsigned> scopes;
        for (const auto &b : w.at("batches"))
          ops += b.at(2).size();
        scheduled += ops;
        for (const auto &r : p.at("registers"))
          regs += r.at(1) == id;
        for (const auto &o : p.at("objects"))
          if (o.at(4) == id) {
            ++scopes[scope(o.at(5))];
          }
        result["workers"].push_back({{"id", id},
                                     {"operations", ops},
                                     {"registers", regs},
                                     {"object_scopes", scopes}});
      }
      unsigned epilogue = p.contains("epilogue") ? p.at("epilogue").size() : 0;
      result["epilogue_operations"] = epilogue;
      result["scheduled_instructions"] = scheduled + epilogue;
      std::cout << result.dump(2) << '\n';
      return 0;
    }
    Json reference, output;
    std::map<std::string, std::vector<double>> samples, whole;
    for (int i = 1; i < argc; ++i) {
      auto j = read(argv[i]);
      Json signature;
      signature["workload"] = j.value("workload", "vvadd");
      for (auto key : {"harts", "rounds", "cycles", "total_cycles", "polls",
                       "digest", "harts_detail"})
        signature[key] = j.at(key);
      if (i == 1)
        reference = signature;
      else if (signature != reference)
        throw std::runtime_error(std::string("cycle/trace/hart mismatch in ") +
                                 argv[i]);
      unsigned workers = j.at("workers");
      std::string engine = j.at("engine");
      if (j.contains("requested_workers") && j["requested_workers"] != workers)
        throw std::runtime_error("native worker count differs from request");
      if (j.contains("context_threads") && j["context_threads"] != workers)
        throw std::runtime_error("Verilator context count mismatch");
      double seconds = j.at("seconds"), total = j.at("whole_seconds");
      if (!(seconds > 0 && total >= seconds))
        throw std::runtime_error("invalid timing");
      std::string key = engine + "-" + std::to_string(workers);
      samples[key].push_back(seconds);
      whole[key].push_back(total);
    }
    output["signature"] = reference;
    for (const auto &[key, values] : samples) {
      double m = median(values);
      output["timings"][key] = {
          {"samples", values},
          {"median_seconds", m},
          {"cycles_per_second", reference.at("cycles").get<double>() / m},
          {"whole_median_seconds", median(whole.at(key))}};
    }
    for (unsigned workers : {1u, 2u, 4u, 8u}) {
      std::string n = "native-" + std::to_string(workers),
                  v = "verilator-" + std::to_string(workers);
      if (samples.count(n) && samples.count(v))
        output["native_speedup"][std::to_string(workers)] =
            median(samples[v]) / median(samples[n]);
    }
    std::cout << output.dump(2) << '\n';
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
