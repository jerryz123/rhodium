// Regroups guarded selector columns into one bounded word matrix without one-hot assumptions.
#include "model.hpp"
#include <algorithm>
#include <map>
#include <optional>
#include <stdexcept>

namespace rds {
void regroup_selector_columns(Model &m) {
  std::vector<const Op *> defs(m.widths.size());
  for (const auto &o : m.ops) defs[o.out] = &o;
  using Rows = std::vector<std::pair<Id,Id>>; // selector, Boolean enable
  struct Column { Id output; unsigned key; };
  std::map<Rows,std::vector<Column>> groups;
  auto literal = [&](Id v) -> std::optional<Big> {
    const auto *p = defs[v];
    if (p && p->code == 0) return number(p->imm);
    return {};
  };
  for (const auto &o : m.ops) {
    if (o.code != 20 || o.args.size() < 2 || o.args.size() > 32 ||
        m.widths[o.out] != o.args.size()) continue;
    Rows rows; unsigned selector_bits = 0; std::optional<unsigned> key;
    bool good = true;
    for (Id v : o.args) {
      if (m.widths[v] != 1) { good = false; break; }
      if (auto n = literal(v); n && *n == 0) { rows.emplace_back(none,none); continue; }
      const auto *both = defs[v];
      if (!both || both->code != 3) { good = false; break; }
      bool found = false;
      for (unsigned side = 0; side < 2 && !found; ++side) {
        Id enable = both->args[side]; const auto *eq = defs[both->args[side^1]];
        if (m.widths[enable] != 1 || !eq || eq->code != 12) continue;
        for (unsigned s = 0; s < 2 && !found; ++s) {
          auto n = literal(eq->args[s]); Id selector = eq->args[s^1];
          unsigned bits = m.widths[selector];
          if (!n || bits > 5 || (selector_bits && selector_bits != bits) ||
              (uint64_t(1) << bits) * o.args.size() > 64) continue;
          unsigned k = n->convert_to<unsigned>();
          if (key && *key != k) continue;
          selector_bits = bits; key = k; rows.emplace_back(selector,enable); found = true;
        }
      }
      if (!found) { good = false; break; }
    }
    if (good && key) groups[rows].push_back({o.out,*key});
  }
  std::vector<Op> added; std::map<Id,Op> replacements; Json details=Json::array();
  auto emit = [&](unsigned code,unsigned width,std::vector<Id> args,
                  std::vector<uint64_t> imm=std::vector<uint64_t>{}) {
    Id id=m.widths.size();m.widths.push_back(width);
    added.push_back({code,id,std::move(args),std::move(imm)});return id;
  };
  for (const auto &[rows,columns] : groups) {
    if (columns.size() < 2) continue;
    unsigned bits=0;for(auto [selector,enable]:rows)if(selector!=none){(void)enable;bits=m.widths[selector];break;}
    unsigned count=rows.size(), width=count*(1u<<bits);
    Id stride=emit(0,width,{}, {count}), matrix=none;
    for(unsigned row=0;row<count;++row){auto [selector,enable]=rows[row];if(selector==none)continue;
      Id offset=emit(8,width,{emit(18,width,{selector}),stride});
      Id lane=emit(9,width,{emit(18,width,{enable}),emit(0,width,{}, {row})});
      Id deposited=emit(9,width,{lane,offset});
      matrix=matrix==none?deposited:emit(4,width,{matrix,deposited});
    }
    Json outputs=Json::array();
    for(auto column:columns){replacements[column.output]={17,column.output,{matrix},{column.key*count}};
      outputs.push_back({column.output,column.key});}
    details.push_back({{"rows",rows},{"matrix_bits",width},{"columns",outputs}});
  }
  m.metadata["selector_column_summary"]={{"groups",details},{"outputs",replacements.size()}};
  if(replacements.empty())return;
  std::vector<Op> all;
  for(auto o:m.ops){if(replacements.count(o.out))o=replacements.at(o.out);all.push_back(std::move(o));}
  all.insert(all.end(),added.begin(),added.end());
  std::vector<Id> producer(m.widths.size(),none);
  for(Id i=0;i<all.size();++i)producer[all[i].out]=i;
  std::vector<unsigned char> seen(all.size());m.ops.clear();
  std::function<void(Id)> visit=[&](Id i){
    if(i==none||seen[i]==2)return;
    if(seen[i])throw std::runtime_error("selector regrouping introduced a cycle");
    seen[i]=1;for(Id a:all[i].args)visit(producer[a]);seen[i]=2;m.ops.push_back(std::move(all[i]));
  };
  for(Id i=0;i<all.size();++i)visit(i);
  m.validate();optimize_body(m);
}
} // namespace rds
