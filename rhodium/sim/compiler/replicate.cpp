// Applies bounded pure-cone replication only when component cost improves.
#include "model.hpp"
#include "kernel.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <set>
namespace rds {
static uint64_t cost(const Model &m, const Op &op) {
  if(op.code==RDS_KERNEL_OPCODE){auto child=kernel_model(m,op);uint64_t total=0;
    for(const auto &o:child.ops)total+=cost(child,o);
    return total;
  }
  uint64_t n = (uint64_t(m.widths[op.out]) + 63) / 64;
  return op.code == 8 ? n * n : n + op.args.size();
}
static uint64_t largest(const Model &m) {
  std::vector<Id> parent(m.widths.size(), none);
  for (const auto &o : m.ops)
    if (o.code)
      parent[o.out] = o.out;
  auto root = [&](Id id) {
    Id r = id;
    while (parent[r] != r)
      r = parent[r];
    while (parent[id] != id) {
      Id next = parent[id];
      parent[id] = r;
      id = next;
    }
    return r;
  };
  for (const auto &o : m.ops)
    if (o.code)
      for (auto a : o.args)
        if (parent[a] != none) {
          auto x = root(o.out), y = root(a);
          parent[std::max(x, y)] = std::min(x, y);
        }
  std::vector<uint64_t> totals(m.widths.size());
  uint64_t max = 0;
  for (const auto &o : m.ops)
    if (o.code)
      max = std::max(max, totals[root(o.out)] += cost(m, o));
  return max;
}
void replicate(Model &m, uint64_t byte_budget, uint64_t work_budget,
               uint64_t cone_work) {
  std::vector<const Op *> d(m.widths.size());
  std::vector<size_t> uses(m.widths.size());
  for (const auto &o : m.ops) {
    d[o.out] = &o;
    for (auto a : o.args)
      ++uses[a];
  }
  using Summary = std::optional<std::pair<uint64_t, uint64_t>>;
  std::map<Id, Summary> summaries;
  auto cone = [&](Id id) -> Summary {
    if (summaries.count(id))
      return summaries[id];
    std::set<Id> seen;
    uint64_t work = 0, bytes = 0;
    std::function<void(Id)> visit = [&](Id v) {
      auto p = d[v];
      if (work <= cone_work && p && p->code && seen.insert(v).second) {
        work += cost(m, *p);
        bytes += 28 + 4 * p->args.size() + 8 * p->imm.size();
        for (auto a : p->args)
          visit(a);
      }
    };
    visit(id);
    Summary result;
    if (work && work <= cone_work)
      result = {{bytes, work}};
    summaries[id] = result;
    return result;
  };
  Model candidate = m;
  candidate.ops.clear();
  std::set<Id> used;
  uint64_t bytes = 0, work = 0;
  for (auto op : m.ops) {
    std::map<Id, Id> local;
    std::function<Id(Id)> clone = [&](Id id) -> Id {
      auto p = d[id];
      if (!p || !p->code)
        return id;
      if (local.count(id))
        return local[id];
      auto copy = *p;
      for (auto &a : copy.args)
        a = clone(a);
      copy.out = candidate.widths.size();
      candidate.widths.push_back(m.widths[id]);
      local[id] = copy.out;
      candidate.ops.push_back(copy);
      return copy.out;
    };
    for (auto &a : op.args) {
      auto old = a;
      auto c = uses[a] > 1 && used.count(a) ? cone(a) : Summary{};
      if (c && c->first <= byte_budget - bytes &&
          c->second <= work_budget - work) {
        bytes += c->first;
        work += c->second;
        a = clone(a);
      }
      used.insert(old);
    }
    candidate.ops.push_back(std::move(op));
  }
  uint64_t before=largest(m),after=largest(candidate);
  Json attempt={{"bytes",bytes},{"work",work},{"largest_before",before},{"largest_after",after},{"added_operations",candidate.ops.size()-m.ops.size()},{"accepted",bytes&&after<before}};
  m.metadata["replication_attempt"]=attempt;
  if (bytes && after < before) {
    candidate.metadata["replication_attempt"]=attempt;
    candidate.metadata["replicated_bytes"] = bytes;
    candidate.metadata["replicated_work"] = work;
    m = std::move(candidate);
  }
}
} // namespace rds
