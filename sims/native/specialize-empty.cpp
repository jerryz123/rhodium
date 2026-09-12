// Specializes snapshot conditions using exact emitted SSA, immutable FF and store provenance.
#include "../../rhodium/sim/compiler/model.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
using namespace rds;
namespace {
std::string read(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot read " + path);
  return {std::istreambuf_iterator<char>(f), {}};
}
std::string hash(const std::string &bytes) {
  uint64_t h = UINT64_C(14695981039346656037);
  for (unsigned char c : bytes) h = (h ^ c) * UINT64_C(1099511628211);
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << h;
  return out.str();
}
std::string image_key(const Model &model) {
  char path[] = "/tmp/rhodium-empty-image.XXXXXX";
  int fd = mkstemp(path);
  if (fd < 0) throw std::runtime_error("cannot allocate image verification file");
  close(fd);
  try {
    model.write_binary(path);
    auto result = hash(read(path));
    unlink(path);
    return result;
  } catch (...) { unlink(path); throw; }
}
Json comment(const std::string &code, const std::string &tag, size_t start = 0) {
  const std::string prefix = "/* " + tag + " ";
  auto p = code.find(prefix, start);
  if (p == std::string::npos) throw std::runtime_error("missing " + tag);
  auto end = code.find(" */", p);
  if (end == std::string::npos) throw std::runtime_error("unterminated " + tag);
  return Json::parse(code.substr(p + prefix.size(), end - p - prefix.size()));
}
struct Region {
  Json info;
  size_t begin, end;
  std::string body;
};
std::vector<Region> regions(const std::string &code) {
  std::vector<Region> result;
  for (size_t p = 0; (p = code.find("/* rds-region ", p)) != std::string::npos; ++p) {
    Json info = comment(code, "rds-region", p);
    auto header = code.find("\nstatic ", p);
    auto begin = code.find("{\n", header);
    auto end = code.find("/* rds-region-end */", begin);
    auto next = code.find("/* rds-region ", begin);
    if(end==std::string::npos || (next!=std::string::npos && next<end))
      end=code.find("\n}\n",begin); // Legacy emitters have no explicit boundary.
    if (header == std::string::npos || begin == std::string::npos || end == std::string::npos)
      throw std::runtime_error("malformed region body");
    // A lifted transition consumes the selected body's stores and must run
    // exactly once on both paths; only the preceding pure body is replaceable.
    auto tail = code.find("/* rds-transition-tail */", begin);
    if (tail != std::string::npos && tail < end) end = tail;
    // New emitters delimit the pure body explicitly: contract programs contain
    // nested branches whose closing braces can occupy their own lines.
    if (code.substr(header, begin-header).find(" void bound_" + std::to_string(info.at("id").get<unsigned>()) + "(") == std::string::npos)
      throw std::runtime_error("region identity mismatch");
    result.push_back({info, begin + 2, end, code.substr(begin + 2, end - begin - 2)});
  }
  return result;
}
struct Completion { unsigned operation, first, count; size_t position; };
std::vector<Completion> completions(const Region &r) {
  std::regex re(R"(/\* rds-complete (\d+) (\d+) (\d+) \*/)" );
  std::vector<Completion> out;
  unsigned next = 0;
  for (auto i = std::sregex_iterator(r.body.begin(), r.body.end(), re); i != std::sregex_iterator(); ++i) {
    auto &m = *i;
    Completion c{unsigned(std::stoul(m[1])), unsigned(std::stoul(m[2])), unsigned(std::stoul(m[3])), size_t(m.position())};
    if (c.operation != out.size() || c.first != next || !c.count || c.count > 4096 || c.first > 4096-c.count)
      throw std::runtime_error("invalid temporary completion correspondence");
    next += c.count; out.push_back(c);
  }
  if (out.size() != r.info.at("operations").size())
    throw std::runtime_error("incomplete temporary provenance");
  return out;
}
struct Store { unsigned temporary; std::string target; };
std::vector<Store> stores(const Region &r, unsigned temporaries) {
  std::regex marked(R"(/\* rds-store (\d+) \*/\n([^\n]+))");
  std::regex assignment(R"(^((?:v|n)\[\d+\]|\(\(uint8_t\*\)v\)\[\d+\]|h->o\d+\.pending\[\d+\])\s*=\s*t(\d+);$)");
  std::vector<Store> result;
  for (auto i = std::sregex_iterator(r.body.begin(), r.body.end(), marked); i != std::sregex_iterator(); ++i) {
    unsigned t = std::stoul((*i)[1]); std::smatch m; std::string text = (*i)[2];
    if (t >= temporaries || !std::regex_match(text, m, assignment) || std::stoul(m[2]) != t)
      throw std::runtime_error("unknown or inconsistent storage correspondence");
    result.push_back({t, m[1]});
  }
  // Reject an unmarked store rather than silently losing an effect in the fast path.
  std::istringstream lines(r.body); std::string line, previous;
  size_t actual = 0;
  while (std::getline(lines, line)) {
    std::smatch m;
    if (std::regex_match(line, m, assignment)) {
      if (previous != "/* rds-store " + m[2].str() + " */")
        throw std::runtime_error("unmarked generated store");
      ++actual;
    }
    previous = line;
  }
  if (actual != result.size()) throw std::runtime_error("store count mismatch");
  return result;
}
bool partial_operation(const Model &m, const Op &op) {
  // A vector lookup is total only when its complete type domain is in bounds.
  // This proof is independent of the runtime specialization assumption.
  if (op.code == 21) {
    unsigned bits = m.widths.at(op.args.at(1));
    return bits >= 64 || (UINT64_C(1) << bits) > op.imm.at(0);
  }
  return op.code == 16 || op.code == 33 || op.code == 22 || op.code == 23 || op.code == 24;
}
std::string literal(uint64_t value) { return "UINT64_C(" + std::to_string(value) + ")"; }
}
int main(int argc, char **argv) {
 try {
  if (argc < 6) {
    std::cerr << "usage: specialize-empty model.json plan.json input.c output.c report.json [--partial] [--matcher-ancestors] [--local-guards 8..256] [--invalid-pipe ID] [--state ID VALUE]\n";
    return 2;
  }
  bool partial = false, matcher_ancestors = false; unsigned local_guard_ops=0; std::optional<unsigned> invalid_pipe;
  std::optional<std::pair<Id,uint64_t>> state;
  auto unsigned_argument=[](const std::string &s) {
    if(s.empty()||s.find_first_not_of("0123456789")!=std::string::npos)
      throw std::runtime_error("expected unsigned decimal argument");
    return std::stoull(s);
  };
  for (int i=6;i<argc;++i) {
    std::string option=argv[i];
    if(option=="--partial"&&!partial) partial=true;
    else if(option=="--matcher-ancestors"&&!matcher_ancestors) matcher_ancestors=true;
    else if(option=="--local-guards"&&!local_guard_ops&&i+1<argc) {
      auto n=unsigned_argument(argv[++i]);if(n<8||n>256)throw std::runtime_error("local guard span must be 8..256 operations");local_guard_ops=n;
    }
    else if(option=="--invalid-pipe"&&!invalid_pipe&&i+1<argc) {
      std::string id=argv[++i];
      if(id.empty()||id.find_first_not_of("0123456789")!=std::string::npos||std::stoull(id)>UINT32_MAX)
        throw std::runtime_error("invalid PIPE object ID");
      invalid_pipe=std::stoul(id);
    } else if(option=="--state"&&!state&&i+2<argc) {
      auto id=unsigned_argument(argv[++i]);auto value=unsigned_argument(argv[++i]);
      if(id>UINT32_MAX) throw std::runtime_error("invalid state value ID");
      state=std::make_pair(Id(id),uint64_t(value));
    } else throw std::runtime_error("unknown or repeated specialization option");
  }
  Model original = Model::read(argv[1]); original.validate();
  Json plan = Json::parse(read(argv[2])); std::string code = read(argv[3]);
  if (code.find("/* rds-empty-specialized ") != std::string::npos)
    throw std::runtime_error("input is already specialized");
  auto footer = code.rfind("/* rds-file-fnv64 ");
  if (footer == std::string::npos || code.substr(footer) != "/* rds-file-fnv64 " + hash(code.substr(0, footer)) + " */\n")
    throw std::runtime_error("generated source hash mismatch");
  Json provenance = comment(code, "rds-provenance");
  if (provenance.at("version") != 1 || provenance.at("workers") != 1 || provenance.at("shared") != false)
    throw std::runtime_error("specialization requires one worker and specialized bodies");
  if (provenance.at("source_key") != image_key(original) || plan.at("source_key") != provenance.at("source_key"))
    throw std::runtime_error("source model hash mismatch");
  if (plan.at("compiled_key") != provenance.at("compiled_key"))
    throw std::runtime_error("compiled plan hash mismatch");
  if (plan.at("format") != "rhodium-static-plan-v1" || plan.at("workers").size() != 1)
    throw std::runtime_error("unsupported fixed plan");
  std::map<unsigned, std::string> sources;
  std::regex view(R"(h->counts_bool\[\d+\])");
  for (auto &entry : provenance.at("sources")) {
    unsigned id = entry[0]; std::string expression = entry[1];
    const auto &o = original.metadata.at("objects").at(id);
    if (o[0] != 1 || o[2] != 1 || o[3] != 0 || !std::regex_match(expression, view) || !sources.emplace(id, expression).second)
      throw std::runtime_error("unknown current occupancy storage");
  }
  std::string pipe_view;
  if(invalid_pipe) {
    const auto &o=original.metadata.at("objects").at(*invalid_pipe);
    if(o[0]!=2) throw std::runtime_error("invalid-pipe requires a PIPE object");
    for(const auto &entry:provenance.at("query_views")) if(entry.at(0)==*invalid_pipe&&entry.at(1)==2) {
      if(!pipe_view.empty()) throw std::runtime_error("duplicate PIPE query view");
      pipe_view=entry.at(2);
    }
    std::string expected="h->valid_"+std::to_string(*invalid_pipe)+"["+std::to_string(o[2].get<unsigned>()-1)+"]";
    if(pipe_view!=expected) throw std::runtime_error("unknown current PIPE valid storage");
  }
  std::map<Id, Op> defs; for (auto &o : original.ops) defs.emplace(o.out, o);
  std::set<Id> current_ff;
  std::map<Id,unsigned> qoffset;
  for(const auto &r:original.metadata.at("registers")) current_ff.insert(r[0].get<Id>());
  for(const auto &v:plan.at("values")) if(!v.at(2).is_null()) {
    Id origin=v.at(3);
    if(!current_ff.count(origin)) continue;
    if(v.at(0)!=original.widths.at(origin)||defs.count(origin)||qoffset.count(origin))
      throw std::runtime_error("invalid immutable FF binding");
    qoffset[origin]=v.at(2);
  }
  if(state) {
    std::map<Id,unsigned> emitted;
    for(const auto &v:provenance.at("state_views")) {
      Id origin=v.at(0);
      if(!qoffset.count(origin)||v.at(1)!=original.widths.at(origin)||v.at(2)!=qoffset.at(origin)||emitted.count(origin))
        throw std::runtime_error("emitted current FF storage mismatch");
      emitted[origin]=v.at(2);
    }
    if(emitted!=qoffset) throw std::runtime_error("incomplete current FF storage provenance");
    Id id=state->first;
    if(!qoffset.count(id)||original.widths.at(id)>64||Big(state->second)>mask(original.widths.at(id)))
      throw std::runtime_error("state condition requires an in-range scalar current FF");
  }
  std::set<Id> already_constant;
  std::map<Id,Id> already_alias;
  if(invalid_pipe||state) {
    Model baseline=original; size_t first=baseline.metadata["ports"].size();
    for(const auto &op:original.ops) baseline.metadata["ports"].push_back({1,op.out,"baseline_probe"});
    optimize_body(baseline); std::set<Id> constants;
    for(const auto &op:baseline.ops) if(!op.code) constants.insert(op.out);
    for(unsigned i=0;i<original.ops.size();++i)
      if(constants.count(baseline.metadata["ports"][first+i][1])) already_constant.insert(original.ops[i].out);
    std::map<Id,Id> roots;std::set<Id> defined;
    for(const auto &op:baseline.ops) defined.insert(op.out);
    for(unsigned i=0;i<original.metadata["registers"].size();++i) {
      Id old=original.metadata["registers"][i][0],now=baseline.metadata["registers"][i][0];
      if(!defined.count(now)&&qoffset.count(old)) roots[now]=old;
    }
    for(unsigned i=0;i<original.ops.size();++i) {
      Id now=baseline.metadata["ports"][first+i][1];
      if(roots.count(now)) already_alias[original.ops[i].out]=roots.at(now);
    }
  }
  std::vector<Id> instructions;
  for (auto &w : plan.at("workers")) for (auto &b : w.at("batches")) for (auto &i : b[2]) instructions.push_back(i[4]);
  Json decisions = Json::array(); std::vector<std::pair<Region, std::string>> replacements;
  struct Assumption { std::optional<unsigned> pipe; std::optional<std::pair<Id,uint64_t>> ff; };
  std::vector<Assumption> assumptions;
  if(state) assumptions.push_back({std::nullopt,state});
  if(invalid_pipe) assumptions.push_back({invalid_pipe,std::nullopt});
  if(partial||assumptions.empty()) assumptions.push_back({std::nullopt,std::nullopt});
  // Only the replaced empty-state query leaves are proof assumptions. A root
  // cannot depend on assumptions outside its actual combinational operands.
  const unsigned owner_words=(original.metadata["objects"].size()+63)/64;
  std::vector<std::vector<uint64_t>> empty_dependencies;
  if(local_guard_ops){
    if(state||invalid_pipe)throw std::runtime_error("local guards require empty-queue specialization");
    empty_dependencies.assign(original.widths.size(),std::vector<uint64_t>(owner_words));
    for(const auto&o:original.ops){auto&out=empty_dependencies[o.out];
      for(Id a:o.args)for(unsigned w=0;w<owner_words;++w)out[w]|=empty_dependencies[a][w];
      if(o.code==29&&o.args.empty()&&o.imm[1]<=2&&sources.count(o.imm[0]))out[o.imm[0]/64]|=UINT64_C(1)<<(o.imm[0]%64);
    }
  }
  for (const Region &r : regions(code)) for(const auto &assumption:assumptions) {
    invalid_pipe=assumption.pipe;state=assumption.ff;
    const auto &ops = r.info.at("operations"); unsigned begin = r.info.at("begin"), end = r.info.at("end");
    if (ops.empty() || end < begin || end > instructions.size() || end-begin != ops.size())
      throw std::runtime_error("plan instruction span mismatch");
    auto complete = completions(r); unsigned temporaries = complete.back().first + complete.back().count;
    auto destinations = stores(r, temporaries);
    // Snapshot specialization stays inside the original region: its existing
    // schedule guard and every store remain, including for an invalid PIPE.
    std::set<unsigned> matchers; bool eligible = (state||invalid_pipe||r.info.at("guard") == 0) && ops.size() >= (invalid_pipe?8:32) && ops.size() <= 1024;
    for (unsigned i = 0; i < ops.size(); ++i) {
      Id origin = ops[i][0];
      if (origin != instructions[begin+i] || !defs.count(origin)) throw std::runtime_error("plan origin mismatch");
      const Op &op = defs.at(origin); unsigned width = original.widths.at(origin);
      if (((ops[i][1].get<unsigned>() != 0) || width <= 64) && ops[i][2] != width)
        throw std::runtime_error("emitted result width mismatch");
      unsigned expected = (ops[i][1].get<unsigned>() != 0) ? (width+63)/64 : 1;
      if (complete[i].count != expected || (!(ops[i][1].get<unsigned>() != 0) && width > 64)) eligible = false;
      if (partial_operation(original,op)) eligible = false;
      if (op.code == 29) {
        const auto &object = original.metadata["objects"][op.imm.at(0)];
        if (object[0] == 10 && (object[3].get<unsigned>() & 32)) matchers.insert(op.imm[0]);
      }
    }
    bool ancestor_region = false;
    if (eligible && matcher_ancestors && !state && !invalid_pipe && matchers.empty()) {
      // C body boundaries are not semantic boundaries. Follow immutable DFG
      // operands to find the matcher that controls a downstream pure region.
      std::vector<Id> todo; for (const auto &o : ops) todo.push_back(o[0]);
      std::set<Id> seen;
      while (!todo.empty() && seen.size() < 65536) {
        Id v=todo.back();todo.pop_back();
        if (!seen.insert(v).second || !defs.count(v)) continue;
        const Op &o=defs.at(v);
        if (o.code==29) {
          const auto &object=original.metadata["objects"][o.imm.at(0)];
          if (object[0]==10 && (object[3].get<unsigned>()&32)) matchers.insert(o.imm[0]);
        }
        todo.insert(todo.end(),o.args.begin(),o.args.end());
      }
      if (!todo.empty()) matchers.clear();
      ancestor_region = !matchers.empty();
    }
    if (!eligible || (!state&&!invalid_pipe&&matchers.empty()) || destinations.empty()) continue;
    std::set<unsigned> owners;
    if(!state&&!invalid_pipe) for (unsigned id : matchers) {
      std::vector<Id> todo;
      for (const auto &o : original.ops) if (o.code == 29 && o.imm[0] == id) todo.insert(todo.end(), o.args.begin(), o.args.end());
      std::set<Id> seen;
      while (!todo.empty()) {
        Id idv = todo.back(); todo.pop_back();
        if (!seen.insert(idv).second || !defs.count(idv)) continue;
        const Op &o = defs.at(idv);
        if (o.code == 29 && sources.count(o.imm[0]) && o.imm[1] == 3 && o.args.empty()) owners.insert(o.imm[0]);
        todo.insert(todo.end(), o.args.begin(), o.args.end());
      }
    }
    if (!state&&!invalid_pipe&&(owners.empty() || owners.size() > (matcher_ancestors?256u:64u))) continue;
    Model m = original; size_t first = m.metadata["ports"].size();
    for (auto &o : ops) m.metadata["ports"].push_back({1,o[0],"specialization_probe"});
    if(state) m.ops.insert(m.ops.begin(),{0,state->first,{},words(state->second,m.widths.at(state->first))});
    for (auto &o : m.ops) if (o.code == 29 && o.args.empty()) {
      if(invalid_pipe&&o.imm[0]==*invalid_pipe&&o.imm[1]==2)
        o={0,o.out,{},words(0,m.widths[o.out])};
      else if(!state&&!invalid_pipe&&owners.count(o.imm[0])&&o.imm[1]<=2)
        o={0,o.out,{},words(o.imm[1]==1?1:0,m.widths[o.out])};
    }
    for (unsigned pass = 0; pass < 16; ++pass) {
      optimize_body(m); std::map<Id,Big> constants;
      for (auto &o : m.ops) if (o.code == 0) constants[o.out] = number(o.imm);
      bool changed = false;
      for (auto &o : m.ops) if (o.code == 29 && matchers.count(o.imm[0])) {
        unsigned depth = original.metadata["objects"][o.imm[0]][2];
        bool zero = o.imm[1] >= depth ? constants.count(o.args.at(1)) && constants[o.args[1]] == 0 : true;
        if (o.imm[1] < depth) for (Id v : o.args) zero &= constants.count(v) && constants[v] == 0;
        if (zero) { o = {0,o.out,{},words(0,m.widths[o.out])}; changed = true; }
      }
      if (!changed) break;
    }
    std::map<Id,Big> constants; for (auto &o : m.ops) if (o.code == 0) constants[o.out] = number(o.imm);
    std::map<Id,Id> roots;
    if(state) {
      std::set<Id> defined;for(const auto &o:m.ops) defined.insert(o.out);
      for(unsigned i=0;i<original.metadata["registers"].size();++i) {
        Id old=original.metadata["registers"][i][0],now=m.metadata["registers"][i][0];
        if(!defined.count(now)&&qoffset.count(old)) roots[now]=old;
      }
    }
    std::vector<std::optional<std::string>> known(temporaries); unsigned folded = 0,new_results=0,aliases=0;
    for (unsigned i = 0; i < complete.size(); ++i) {
      Id value = m.metadata["ports"][first+i][1];
      Id origin=ops[i][0];unsigned width=original.widths.at(origin);
      if(constants.count(value)) {
        if(!already_constant.count(origin)) ++new_results;
        auto parts = words(constants[value], width);
        for (unsigned j = 0; j < complete[i].count; ++j) { known[complete[i].first+j] = literal(parts.at(j)); ++folded; }
      } else if(roots.count(value)) {
        Id root=roots.at(value);
        if(width!=original.widths.at(root)) throw std::runtime_error("FF alias changes declared width");
        if(already_alias.count(origin)&&already_alias.at(origin)==root) continue;
        ++new_results;++aliases;
        for(unsigned j=0;j<complete[i].count;++j) {
          known[complete[i].first+j]="(q["+std::to_string(qoffset.at(root)+j)+"]&"+literal(mask(std::min(64u,width-64*j)).convert_to<uint64_t>())+")";
          ++folded;
        }
      }
    }
    bool all = folded == temporaries;
    if(state ? new_results<32 : invalid_pipe ? new_results<8 : (folded < 32 || (!all && (!partial || folded < 64 || folded*2 < temporaries)))) continue;
    std::string guard=invalid_pipe?pipe_view:"";
    if(state) guard="((q["+std::to_string(qoffset.at(state->first))+"]&"+literal(mask(original.widths.at(state->first)).convert_to<uint64_t>())+")!="+literal(state->second)+")";
    for (unsigned id : owners) { if (!guard.empty()) guard += "|"; guard += sources.at(id); }
    std::string fast;
    if (all) {
      for (const Store &store : destinations) fast += store.target + "=" + *known[store.temporary] + ";\n";
    } else {
      fast = r.body;
      for (auto i = complete.rbegin(); i != complete.rend(); ++i) {
        std::string assignments;
        for (unsigned t = i->first; t < i->first+i->count; ++t)
          if (known[t]) assignments += "t" + std::to_string(t) + "=" + *known[t] + ";\n";
        fast.insert(i->position, assignments);
      }
    }
    Json local_details;
    std::string replacement="if(!("+guard+")){\n"+fast+"}else{\n"+r.body+"\n}";
    if(local_guard_ops){
      // Hoist only numbered result locals. This keeps all existing stores and
      // lets later slices consume either branch's result without arena traffic.
      const std::regex declaration(R"(\buint64_t (t(\d+))\b)");
      std::vector<unsigned>declared(temporaries);
      for(auto it=std::sregex_iterator(r.body.begin(),r.body.end(),declaration);it!=std::sregex_iterator();++it){
        unsigned t=std::stoul((*it)[2]);if(t>=temporaries||declared[t]++)throw std::runtime_error("unexpected result declaration in local guard body");
      }
      if(std::find(declared.begin(),declared.end(),0u)!=declared.end())throw std::runtime_error("missing result declaration in local guard body");
      replacement="uint64_t ";for(unsigned t=0;t<temporaries;++t)replacement+=(t?",t":"t")+std::to_string(t);replacement+=";\n";
      size_t start=0;unsigned guarded=0,localized=0,largest=0,preserved=0;Json owner_counts=Json::array();
      for(unsigned first_op=0;first_op<complete.size();first_op+=local_guard_ops){
        unsigned last=std::min<unsigned>(complete.size(),first_op+local_guard_ops),count=0,total=0;
        size_t endpos=r.body.find('\n',complete[last-1].position);if(endpos==std::string::npos)throw std::runtime_error("unterminated completion marker");++endpos;
        std::vector<uint64_t>needed(owner_words);
        for(unsigned i=first_op;i<last;++i){bool used=false;total+=complete[i].count;
          for(unsigned t=complete[i].first;t<complete[i].first+complete[i].count;++t)if(known[t]){++count;used=true;}
          if(used)for(unsigned w=0;w<owner_words;++w)needed[w]|=empty_dependencies[ops[i][0].get<Id>()][w];
        }
        std::string local_guard;unsigned owner_count=0;
        for(unsigned owner:owners)if((needed[owner/64]>>(owner%64))&1){if(!local_guard.empty())local_guard+='|';local_guard+=sources.at(owner);++owner_count;}
        std::string slow=std::regex_replace(r.body.substr(start,endpos-start),declaration,"$1");
        if(count<8||count*2<total||owner_count>16){replacement+=slow;++preserved;}
        else{
          std::string selected=r.body.substr(start,endpos-start);
          for(unsigned i=last;i-- > first_op;){std::string assignments;
            for(unsigned t=complete[i].first;t<complete[i].first+complete[i].count;++t)if(known[t])assignments+="t"+std::to_string(t)+"="+*known[t]+";\n";
            selected.insert(complete[i].position-start,assignments);
          }
          selected=std::regex_replace(selected,declaration,"$1");
          if(local_guard.empty())replacement+=selected;
          else {replacement+="if(!("+local_guard+")){\n"+selected+"}else{\n"+slow+"}\n";++guarded;}
          localized+=count;largest=std::max(largest,owner_count);owner_counts.push_back(owner_count);
        }
        start=endpos;
      }
      replacement+=r.body.substr(start);
      if(!localized)continue;
      // Keep the cheap all-empty path. Local branches are useful only after
      // some input in the broad region is active; they must not tax idle cycles.
      replacement="if(!("+guard+")){\n"+fast+"}else{\n"+replacement+"}\n";
      local_details={{"operation_span",local_guard_ops},{"guards",guarded},{"folded_words",localized},{"largest_owner_guard",largest},{"owner_counts",owner_counts},{"preserved_chunks",preserved},{"preserves_global_fast_path",true}};
    }
    replacements.push_back({r,std::move(replacement)});
    decisions.push_back({{"command",r.info["id"]},{"owners",owners},{"folded_words",folded},{"new_constant_results",new_results-aliases},{"current_ff_aliases",aliases},{"words",temporaries},{"stores",destinations.size()},{"all_constant",all&&!aliases}});
    decisions.back()["ancestor_region"]=ancestor_region;
    if(local_guard_ops)decisions.back()["local_guards"]=std::move(local_details);
    if(invalid_pipe) decisions.back()["invalid_pipe"]=*invalid_pipe;
    if(state) decisions.back()["state"]={state->first,state->second};
    break;
  }
  std::string original_hash = hash(code);
  code.erase(footer);
  for (auto i = replacements.rbegin(); i != replacements.rend(); ++i)
    code.replace(i->first.begin, i->first.end-i->first.begin, i->second);
  code += "/* rds-empty-specialized " + decisions.dump() + " */\n";
  code += "/* rds-file-fnv64 " + hash(code) + " */\n";
  Json report = {{"source_key",provenance["source_key"]},{"compiled_key",provenance["compiled_key"]},{"input_hash",original_hash},{"output_hash",hash(code)},{"regions",decisions}};
  std::ofstream output(argv[4]), report_file(argv[5]);
  if (!output || !report_file) throw std::runtime_error("cannot open specialization output/report");
  output << code; report_file << report.dump(2) << '\n';
  output.close(); report_file.close();
  if (!output || !report_file) throw std::runtime_error("cannot finish specialization output/report");
  std::cout << report.dump(2) << '\n';
 } catch (const std::exception &e) { std::cerr << "specialization: " << e.what() << '\n'; return 1; }
}
