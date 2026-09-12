// Recovers packed Boolean computations and redirects scalar users through safe
// word views.
#include "model.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>

namespace rds {
namespace {
struct Group {
  enum Kind { Gather, Broadcast, Constant, Slice, Boolean, Mux } kind = Gather;
  std::vector<Id> lanes;
  std::vector<std::shared_ptr<Group>> children;
  uint32_t code = 0;
  Id source = none;
  uint64_t immediate = 0;
  size_t cost = 0;
  std::set<Id> scalars;
};
} // namespace

void regroup_bits(Model &m) {
  const size_t original_values = m.widths.size();
  std::vector<std::optional<Op>> defs(original_values);
  for (const auto &op : m.ops)
    defs[op.out] = op;
  using Plan = std::shared_ptr<Group>;
  unsigned search_budget = 0;
  std::function<Plan(const std::vector<Id> &, unsigned)> plan;
  plan = [&](const std::vector<Id> &lanes, unsigned fuel) -> Plan {
    auto g = std::make_shared<Group>();
    g->lanes = lanes;
    g->cost = lanes.size(); // A gather is not a free word-level input.
    if (!search_budget)
      return g;
    --search_budget;
    if (std::all_of(lanes.begin(), lanes.end(),
                    [&](Id id) { return defs[id] && defs[id]->code == 0; })) {
      g->kind = Group::Constant;
      g->cost = 0;
      for (size_t i = 0; i < lanes.size(); ++i)
        g->immediate |= defs[lanes[i]]->imm[0] << i;
      return g;
    }
    if (std::all_of(lanes.begin(), lanes.end(),
                    [&](Id id) { return id == lanes[0]; })) {
      g->kind = Group::Broadcast;
      g->source = lanes[0];
      g->cost = 1;
      return g;
    }
    const auto first = defs[lanes[0]];
    if (!first)
      return g;
    if (first->code == 17 &&
        std::all_of(lanes.begin(), lanes.end(), [&](Id id) {
          return defs[id] && defs[id]->code == 17 &&
                 defs[id]->args == first->args;
        })) {
      bool contiguous = true;
      for (size_t i = 0; i < lanes.size(); ++i)
        contiguous &= defs[lanes[i]]->imm[0] == first->imm[0] + i;
      if (contiguous) {
        g->kind = Group::Slice;
        g->source = first->args[0];
        g->immediate = first->imm[0];
        g->cost = 1;
        g->scalars.insert(lanes.begin(), lanes.end());
        return g;
      }
    }
    if (!fuel)
      return g;
    bool boolean = first->code == 2 || first->code == 3 || first->code == 4 ||
                   first->code == 5 || first->code == 26;
    bool mux = first->code == 15 && first->args.size() == 3 &&
               m.widths[first->args[0]] == 1 && first->imm.size() == 1 &&
               first->imm[0] <= 1;
    if ((!boolean && !mux) ||
        !std::all_of(lanes.begin(), lanes.end(), [&](Id id) {
          const auto &p = defs[id];
          return p && p->code == first->code && p->imm == first->imm &&
                 p->args.size() == first->args.size() &&
                 (!mux || p->args[0] == first->args[0]);
        }))
      return g;
    std::vector<std::vector<Id>> operands;
    for (auto id : lanes)
      operands.push_back(defs[id]->args);
    // Canonical scalar operand order can put a common guard on different sides.
    if (first->code >= 3 && first->code <= 5)
      for (Id common : first->args)
        if (std::all_of(operands.begin(), operands.end(), [&](const auto &a) {
              return a[0] == common || a[1] == common;
            })) {
          for (auto &a : operands)
            if (a[0] != common)
              std::swap(a[0], a[1]);
          break;
        }
    g->kind = mux ? Group::Mux : Group::Boolean;
    g->code = first->code;
    g->source = mux ? first->args[0] : none;
    g->immediate = mux ? first->imm[0] : 0;
    g->cost = 1;
    g->scalars.insert(lanes.begin(), lanes.end());
    for (size_t i = mux ? 1 : 0; i < first->args.size(); ++i) {
      std::vector<Id> child;
      for (const auto &a : operands)
        child.push_back(a[i]);
      auto p = plan(child, fuel - 1);
      g->cost += p->cost;
      g->scalars.insert(p->scalars.begin(), p->scalars.end());
      g->children.push_back(std::move(p));
    }
    return g;
  };
  std::vector<Op> added;
  auto emit = [&](uint32_t code, uint32_t width, std::vector<Id> args,
                  std::vector<uint64_t> imm = std::vector<uint64_t>{}) {
    Id out = m.widths.size();
    m.widths.push_back(width);
    Op op{code, out, std::move(args), std::move(imm)};
    defs.push_back(op);
    added.push_back(std::move(op));
    return out;
  };
  std::map<std::vector<Id>, Id> packed;
  struct View {
    Id scalar, word;
    uint32_t bit;
  };
  std::vector<View> views;
  std::function<Id(const Plan &)> materialize;
  materialize = [&](const Plan &g) -> Id {
    auto found = packed.find(g->lanes);
    if (found != packed.end())
      return found->second;
    uint32_t width = g->lanes.size();
    Id out;
    switch (g->kind) {
    case Group::Constant:
      out = emit(0, width, {}, {g->immediate});
      break;
    case Group::Broadcast:
      out = emit(19, width, {g->source});
      break;
    case Group::Slice:
      out = g->immediate == 0 && m.widths[g->source] == width
                ? g->source
                : emit(17, width, {g->source}, {g->immediate});
      break;
    case Group::Gather:
      out = emit(20, width, g->lanes);
      break;
    default: {
      std::vector<Id> args;
      if (g->kind == Group::Mux)
        args.push_back(g->source);
      for (const auto &p : g->children)
        args.push_back(materialize(p));
      out = emit(g->code, width, std::move(args),
                 g->kind == Group::Mux ? std::vector<uint64_t>{g->immediate}
                                       : std::vector<uint64_t>{});
      for (uint32_t i = 0; i < width; ++i)
        views.push_back({g->lanes[i], out, i});
      break;
    }
    }
    packed.emplace(g->lanes, out);
    return out;
  };
  for (auto &op : m.ops) {
    if (op.code != 20 || op.args.size() < 2 || op.args.size() > 64 ||
        !std::all_of(op.args.begin(), op.args.end(),
                     [&](Id id) { return m.widths[id] == 1; }))
      continue;
    search_budget = 512;
    auto g = plan(op.args, 12);
    if (g->scalars.size() + op.args.size() <= g->cost)
      continue;
    Id word = materialize(g);
    op = {1, op.out, {word}, {}};
    defs[op.out] = op;
  }
  if (added.empty())
    return;
  // Packing changes readiness. Reject each scalar redirect that would introduce
  // a dependency cycle, including interactions with previously accepted groups.
  std::vector<size_t> seen(defs.size());
  std::vector<bool> redirected(original_values);
  size_t epoch = 0;
  for (auto view : views) {
    if (redirected[view.scalar])
      continue;
    ++epoch;
    bool cycle = false;
    std::vector<Id> pending{view.word};
    while (!pending.empty() && !cycle) {
      Id id = pending.back();
      pending.pop_back();
      if (id == view.scalar) {
        cycle = true;
        break;
      }
      if (seen[id] == epoch)
        continue;
      seen[id] = epoch;
      if (defs[id])
        pending.insert(pending.end(), defs[id]->args.begin(),
                       defs[id]->args.end());
    }
    if (!cycle) {
      defs[view.scalar] = Op{17, view.scalar, {view.word}, {view.bit}};
      redirected[view.scalar] = true;
    }
  }
  // Keep the serialized graph topological after moving scalar consumers to
  // words.
  m.ops.insert(m.ops.end(), added.begin(), added.end());
  std::vector<Op> ordered;
  std::vector<unsigned char> state(defs.size());
  for (const auto &root : m.ops) {
    std::vector<std::pair<Id, bool>> pending{{root.out, false}};
    while (!pending.empty()) {
      auto [id, finish] = pending.back();
      pending.pop_back();
      if (!defs[id] || state[id] == 2)
        continue;
      if (finish) {
        state[id] = 2;
        ordered.push_back(*defs[id]);
        continue;
      }
      if (state[id] == 1)
        throw std::logic_error("Boolean regrouping introduced a cycle");
      state[id] = 1;
      pending.emplace_back(id, true);
      for (auto it = defs[id]->args.rbegin(); it != defs[id]->args.rend(); ++it)
        pending.emplace_back(*it, false);
    }
  }
  m.ops = std::move(ordered);
}
} // namespace rds
