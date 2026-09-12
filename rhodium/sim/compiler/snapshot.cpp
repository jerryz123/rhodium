// Replicates bounded snapshot prefixes per component around deferred broadcast
// reductions.
#include "model.hpp"
#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
namespace rds {
void split_snapshot_prefixes(Model &m, uint32_t limit) {
  if (!limit || limit > 256)
    throw std::runtime_error("snapshot prefix work must be 1..256");
  unsigned n = m.widths.size();
  std::vector<const Op *> def(n);
  std::vector<bool> prefix(n);
  std::vector<unsigned> cost(n), parent(n);
  for (unsigned v = 0; v < n; ++v)
    parent[v] = v;
  auto root = [&](Id v) {
    while (parent[v] != v) {
      parent[v] = parent[parent[v]];
      v = parent[v];
    }
    return v;
  };
  for (auto &o : m.ops) {
    def[o.out] = &o;
    if (!o.code)
      continue;
    // Contract bodies are already closed regions; do not duplicate a hidden
    // program under this pass's intentionally tiny prefix budget.
    bool total = o.code != RDS_KERNEL_OPCODE && o.code != 16 && o.code != 33 && !(o.code >= 21 && o.code <= 24);
    unsigned c = std::min<uint64_t>(limit + 1,
        (uint64_t(m.widths[o.out]) + 63) / 64 + o.args.size());
    for (auto a : o.args)
      if (def[a] && def[a]->code) {
        if (!prefix[a])
          total = false;
        c = std::min(limit + 1, c + cost[a]);
      }
    cost[o.out] = c;
    prefix[o.out] = total && c <= limit;
  }

  std::vector<bool> tail(n);
  std::vector<std::vector<Id>> users(n);
  for (auto &o : m.ops)
    for (auto a : o.args)
      users[a].push_back(o.out);
  for (auto &o : m.ops)
    if (o.code == 29 && o.imm[1] == 0 &&
        m.metadata["objects"][o.imm[0]][0] == 9) {
      std::set<Id> cone;
      std::queue<Id> q;
      q.push(o.out);
      while (!q.empty()) {
        Id v = q.front();
        q.pop();
        if (!cone.insert(v).second)
          continue;
        for (auto a : users[v])
          q.push(a);
      }
      bool total = true;
      for (auto v : cone)
        if (def[v]->code == 16 || def[v]->code == 33 || (def[v]->code >= 21 && def[v]->code <= 24))
          total = false;
      if (total && cone.size() <= 256)
        for (auto v : cone)
          tail[v] = true;
    }
  if (std::none_of(tail.begin(), tail.end(), [](bool x) { return x; }))
    return;
  for (auto &o : m.ops)
    if (o.code && !prefix[o.out] && !tail[o.out])
      for (auto a : o.args)
        if (def[a] && def[a]->code && !prefix[a] && !tail[a])
          parent[root(o.out)] = root(a);
  Model candidate = m;
  candidate.ops.clear();
  std::map<std::pair<Id, Id>, Id> copies;
  unsigned clone_count = 0;
  std::function<Id(Id, Id)> copy = [&](Id v, Id component) -> Id {
    if (v == none || !prefix[v])
      return v;
    auto key = std::make_pair(v, component);
    auto it = copies.find(key);
    if (it != copies.end())
      return it->second;
    auto o = *def[v];
    for (auto &a : o.args)
      a = copy(a, component);
    if (component != none) {
      o.out = candidate.widths.size();
      candidate.widths.push_back(m.widths[v]);
      ++clone_count;
    }
    copies[key] = o.out;
    candidate.ops.push_back(o);
    return o.out;
  };
  for (auto o : m.ops)
    if (!o.code || !prefix[o.out]) {
      for (auto &a : o.args)
        a = copy(a, root(o.out));
      candidate.ops.push_back(o);
    }
  auto keep = [&](const Json &v) { copy(v.get<Id>(), none); };
  for (auto &r : m.metadata["registers"]) {
    keep(r[1]);
    keep(r[2]);
    keep(r[3]);
  }
  for (auto &r : m.metadata["objects"])
    for (auto &a : r[4])
      keep(a);
  for (auto &r : m.metadata["ports"])
    keep(r[1]);
  for (auto &r : m.metadata["writes"])
    for (unsigned k = 1; k < 5; ++k)
      keep(r[k]);
  for (auto &r : m.metadata["reads"])
    for (unsigned k = 1; k < 5; ++k)
      keep(r[k]);
  for (auto &r : m.metadata["assertions"])
    for (unsigned k = 0; k < 3; ++k)
      keep(r[k]);
  candidate.metadata["prefix_split"] = {
      {"limit", limit},
      {"before_operations", m.ops.size()},
      {"after_operations", candidate.ops.size()},
      {"prefix_values", std::count(prefix.begin(), prefix.end(), true)},
      {"cloned_values", clone_count}};

  // Compacts undefined obsolete prefix IDs while remapping effects and optional
  // provenance.
  std::vector<bool> defined(candidate.widths.size());
  for (unsigned v = 0; v < n; ++v)
    if (!def[v])
      defined[v] = true;
  for (auto &o : candidate.ops)
    defined[o.out] = true;
  std::vector<Id> remap(defined.size(), none);
  std::vector<unsigned> widths;
  for (Id v = 0; v < defined.size(); ++v)
    if (defined[v]) {
      remap[v] = widths.size();
      widths.push_back(candidate.widths[v]);
    }
  auto mapped = [&](Id v) { return v == none ? none : remap[v]; };
  for (auto &o : candidate.ops) {
    o.out = mapped(o.out);
    for (auto &a : o.args)
      a = mapped(a);
  }
  for (auto &r : candidate.metadata["registers"])
    for (auto &a : r)
      a = mapped(a);
  for (auto &r : candidate.metadata["ports"])
    r[1] = mapped(r[1]);
  for (auto name : {"writes", "reads"})
    for (auto &r : candidate.metadata[name])
      for (unsigned k = 1; k < 5; ++k)
        r[k] = mapped(r[k]);
  for (auto &r : candidate.metadata["assertions"])
    for (unsigned k = 0; k < 3; ++k)
      r[k] = mapped(r[k]);
  for (auto &r : candidate.metadata["objects"])
    for (auto &a : r[4])
      a = mapped(a);
  Json origins = Json::array();
  for (auto o : candidate.metadata["origins"]) {
    Id v = o[0];
    if (mapped(v) != none) {
      o[0] = mapped(v);
      origins.push_back(o);
    }
    auto it = copies.lower_bound({v, 0});
    for (; it != copies.end() && it->first.first == v; ++it)
      if (it->first.second != none) {
        o[0] = mapped(it->second);
        origins.push_back(o);
      }
  }
  candidate.metadata["origins"] = origins;
  if (candidate.metadata.contains("contracts"))
    for (auto &c : candidate.metadata["contracts"])
      for (auto &b : c[2])
        b[1] = mapped(b[1]);
  remap_semantic_structure(candidate, mapped);
  candidate.widths = widths;

  candidate.metadata["prefix_split"]["accepted"] =
      candidate.ops.size() <= m.ops.size() + m.ops.size() / 4;
  if (!candidate.metadata["prefix_split"]["accepted"].get<bool>()) {
    m.metadata["prefix_split"] = candidate.metadata["prefix_split"];
    return;
  }
  candidate.validate();
  m = std::move(candidate);
}
} // namespace rds
