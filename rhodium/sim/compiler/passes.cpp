// Implements semantic recognition, Boolean/CSE cleanup, decoder sharing, and word/view recovery.
#include "model.hpp"
#include "kernel.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <tuple>
namespace rds {
using Defs = std::vector<std::optional<Op>>;
static Defs definitions(const Model &m) {
  Defs d(m.widths.size());
  for (const auto &o : m.ops)
    d[o.out] = o;
  return d;
}
static const Op *get(const Defs &d, Id id, int code = -1) {
  if (id >= d.size() || !d[id] || (code >= 0 && d[id]->code != unsigned(code)))
    return nullptr;
  return &*d[id];
}
static std::optional<std::vector<Id>> select(const Defs &d, Id id,
                                             const Model &m) {
  auto p = get(d, id, 15);
  if (!p || p->args.size() != 3 || m.widths[p->args[0]] != 1 ||
      p->imm.size() != 1 || p->imm[0] > 1)
    return {};
  return std::vector<Id>{p->args[0], p->args[p->imm[0] == 1 ? 2 : 1],
                         p->args[p->imm[0] == 1 ? 1 : 2]};
}
// Reuse one effect-root and metadata-remapping contract for debug and release.
template <class Canonical>
static void compact_live(Model &m, const Defs &d, const std::vector<Op> &unique,
                         Canonical canonical, bool diagnostics) {
  std::vector<bool> live(m.widths.size());
  auto retain = [&](Id original) {
    std::vector<Id> pending{original};
    while (!pending.empty()) {
      Id id = canonical(pending.back());
      pending.pop_back();
      if (id == none || live[id])
        continue;
      live[id] = true;
      if (auto p = get(d, id))
        pending.insert(pending.end(), p->args.begin(), p->args.end());
    }
  };
  for (const auto &p : m.metadata["ports"])
    retain(p[1]);
  for (const auto &r : m.metadata["registers"])
    for (const auto &a : r)
      retain(a);
  for (const auto &r : m.metadata["writes"])
    for (size_t i = 1; i < 5; ++i)
      retain(r[i]);
  for (const auto &r : m.metadata["reads"])
    for (size_t i = 1; i < 5; ++i)
      retain(r[i]);
  for (const auto &r : m.metadata["assertions"])
    for (size_t i = 0; i < 3; ++i)
      retain(r[i]);
  for (const auto &o : m.metadata["objects"])
    for (const auto &a : o[4])
      retain(a);
  for (const auto &op : unique)
    if (diagnostics && (op.code == 16 || op.code == 33 || (op.code >= 21 && op.code <= 24)))
      retain(op.out);
  std::vector<Id> remap(m.widths.size(), none);
  std::vector<uint32_t> widths;
  for (Id i = 0; i < m.widths.size(); ++i)
    if (live[i]) {
      remap[i] = widths.size();
      widths.push_back(m.widths[i]);
    }
  auto mapped = [&](Id id) {
    id = canonical(id);
    return id == none ? none : remap[id];
  };
  m.ops.clear();
  for (auto op : unique)
    if (live[op.out]) {
      op.out = remap[op.out];
      for (auto &a : op.args)
        a = mapped(a);
      m.ops.push_back(std::move(op));
    }
  for (auto &p : m.metadata["ports"])
    p[1] = mapped(p[1]);
  for (auto &r : m.metadata["registers"])
    for (auto &a : r)
      a = mapped(a);
  for (auto name : {"writes", "reads"})
    for (auto &r : m.metadata[name])
      for (size_t i = 1; i < 5; ++i)
        r[i] = mapped(r[i]);
  for (auto &r : m.metadata["assertions"])
    for (size_t i = 0; i < 3; ++i)
      r[i] = mapped(r[i]);
  for (auto &o : m.metadata["objects"])
    for (auto &a : o[4])
      a = mapped(a);
  Json origins = Json::array();
  for (auto o : m.metadata["origins"])
    if (live[canonical(o[0])]) {
      o[0] = mapped(o[0]);
      origins.push_back(std::move(o));
    }
  m.metadata["origins"] = std::move(origins);
  if(m.metadata.contains("contracts"))for(auto &contract:m.metadata["contracts"])
    for(auto &binding:contract[2])binding[1]=mapped(binding[1]);
  if(m.metadata.contains("contract_kernel_summary"))
    for(auto &kernel:m.metadata["contract_kernel_summary"]["kernels"])
      if(kernel.contains("output_value"))kernel["output_value"]=mapped(kernel["output_value"]);
  remap_semantic_structure(m, mapped);
  m.metadata["replicated_bytes"] = 0;
  m.metadata["replicated_work"] = 0;
  m.widths = std::move(widths);
}
void release_body(Model &m) {
  m.metadata["assertions"] = Json::array();
  auto d = definitions(m);
  auto ops = m.ops;
  compact_live(m, d, ops, [](Id id) { return id; }, false);
}
void compact_graph(Model &m) {
  auto d=definitions(m);auto ops=m.ops;
  compact_live(m,d,ops,[](Id id){return id;},true);
}
// Fixed-priority arbitration observes requests only through its explicit query
// arguments. Its update descriptor has no transfer or payload state to change.
void prune_stateless_inputs(Model &m) {
  std::map<uint32_t,Id> zeros;
  for(const auto &op:m.ops)
    if(op.code==0&&number(op.imm)==0)zeros.emplace(m.widths[op.out],op.out);
  for(auto &object:m.metadata["objects"]){
    if(object[0]!=6||(object[3].get<uint32_t>()&2))continue;
    // Keep reset provenance; public trace count/enqueue/dequeue remain zero.
    for(size_t i=1;i<object[4].size();++i){
      Id original=object[4][i];
      if(original==none)continue;
      uint32_t width=m.widths[original];
      auto found=zeros.find(width);
      if(found==zeros.end()){
        Id id=m.widths.size();m.widths.push_back(width);
        m.ops.push_back({0,id,{},words(Big(0),width)});
        found=zeros.emplace(width,id).first;
      }
      object[4][i]=found->second;
    }
  }
  auto d=definitions(m);auto ops=m.ops;
  compact_live(m,d,ops,[](Id id){return id;},true);
}
void fold_idle_fifos(Model &m) {
  constexpr unsigned fifo_drop=1u,fifo_flow=4u,fifo_flags=7u;
  Json removed=Json::array();unsigned rounds=0,queries=0;
  for(;;){
    auto d=definitions(m);std::vector<bool>idle(m.metadata["objects"].size());unsigned count=0;
    for(Id id=0;id<idle.size();++id){const auto&o=m.metadata["objects"][id];
      if(o[0]!=1||(o[3].get<unsigned>()&~fifo_flags))continue;
      Id en=o[4][1];while(get(d,en)&&get(d,en)->code==1)en=get(d,en)->args[0];
      const auto*p=get(d,en);if(!p||p->code!=0||number(p->imm)!=0)continue;
      // Native FIFOs start empty with zeroed storage. No accepted enqueue can
      // occur, so reset/pop cannot change that invariant at any later cycle.
      idle[id]=true;++count;
      removed.push_back({{"round",rounds},{"object",id},{"occurrence",o[5]},{"width",o[1]},{"depth",o[2]},{"flags",o[3]}});
    }
    if(!count)break;
    for(auto&o:m.ops)if(o.code==29&&idle[o.imm[0]]){unsigned query=o.imm[1],flags=m.metadata["objects"][o.imm[0]][3];
      if((flags&fifo_flow)&&(query==2||(query==3&&!(flags&fifo_drop)))){
        // Flow-through previews use the query's actual operands, which may
        // intentionally differ from the object's bound update inputs.
        o={1,o.out,{o.args.at(0)},{}};
      }else o={0,o.out,{},words(query==1?Big(1):Big(0),m.widths[o.out])};
      ++queries;
    }
    Json kept=Json::array();std::vector<Id>remap(idle.size(),none);
    for(Id id=0;id<idle.size();++id)if(!idle[id]){remap[id]=kept.size();kept.push_back(m.metadata["objects"][id]);}
    m.metadata["objects"]=std::move(kept);
    for(auto&o:m.ops)if(o.code==29)o.imm[0]=remap.at(o.imm[0]);
    // Strict roots survive even when an eliminated object's inputs go dead.
    // Simplification exposes chains fed by an earlier permanently empty FIFO.
    optimize_body(m);++rounds;
  }
  m.metadata["idle_fifo_summary"]={{"removed",removed},{"rounds",rounds},{"queries_folded",queries},
    {"proof","zero initialization and permanently false bound enqueue; preserve independent flow-through query operands"}};
}
// Release-only recovery of one selected vector update at a register boundary.
void fuse_register_updates(Model &m) {
  // Verification supplies index bounds; one-element vectors become wires/muxes.
  for (auto &op : m.ops) {
    if (op.code == 21 && op.imm[0] == 1)
      op = {1, op.out, {op.args[0]}, {}};
    else if (op.code == 22 && op.imm[0] == 1)
      op = {1, op.out, {op.args[2]}, {}};
    else if (op.code == 23 && op.imm[0] == 1 && op.imm[2] == 1)
      op = {15, op.out, {op.args[1], op.args[0], op.args[3]}, {1}};
  }
  optimize_body(m);
  release_body(m);
  auto d = definitions(m);
  auto emit = [&](uint32_t code, uint32_t width, std::vector<Id> args,
                  std::vector<uint64_t> imm) {
    Id id = m.widths.size();
    m.widths.push_back(width);
    m.ops.push_back({code, id, std::move(args), std::move(imm)});
    return id;
  };
  for (auto &reg : m.metadata["registers"]) {
    Id out = reg[1];
    if (!select(d, out, m)) continue;
    std::vector<uint64_t> geometry;
    unsigned visits = 0, updates = 0;
    std::function<bool(Id)> inspect = [&](Id id) {
      if (++visits > 63) return false;
      if (auto mux = select(d, id, m))
        return inspect((*mux)[1]) && inspect((*mux)[2]);
      if (auto write = get(d, id, 23)) {
        if (write->imm[2] != 1) return false;
        if (geometry.empty()) geometry = write->imm;
        if (geometry != write->imm) return false;
        ++updates;
      }
      return true;
    };
    if (!inspect(out) || !updates) continue;
    auto literal = [&](uint32_t width, unsigned value) {
      return emit(0, width, {}, words(value, width));
    };
    Id disabled = literal(1, 0), index_zero = literal(geometry[3], 0),
       data_zero = literal(geometry[1], 0);
    // Tuple: base, enable, index, replacement. Preserve the original mux priority.
    using Update = std::vector<Id>;
    std::function<Update(Id)> lower = [&](Id id) -> Update {
      if (auto mux = select(d, id, m)) {
        auto yes = lower((*mux)[1]), no = lower((*mux)[2]);
        Update result;
        for (unsigned k = 0; k < 4; ++k)
          result.push_back(no[k] == yes[k] ? no[k] :
                           emit(15, m.widths[no[k]], {(*mux)[0], no[k], yes[k]}, {1}));
        return result;
      }
      if (auto write = get(d, id, 23)) return write->args;
      return {id, disabled, index_zero, data_zero};
    };
    auto fields = lower(out);
    Id replacement = emit(23, m.widths[out], std::move(fields), geometry);
    reg[1] = replacement;
    Json origins = Json::array();
    for (auto origin : m.metadata["origins"])
      if (origin[0] == out) {
        origin[0] = replacement;
        origins.push_back(std::move(origin));
      }
    for (auto &origin : origins)
      m.metadata["origins"].push_back(std::move(origin));
  }
  // New tuples are appended after existing definitions. Remove obsolete partial
  // candidates before ordinary simplification would preserve strict roots.
  release_body(m);
  optimize_body(m);
  release_body(m);
}
void optimize_body(Model &m) {
  // Modulo-two arithmetic is Boolean logic. Expose that before constructing
  // definitions so packed bit lanes can later recover one word operation.
  for(auto &op:m.ops)if(m.widths[op.out]==1){
    if(op.code==6||op.code==7)op.code=5;
    else if(op.code==8)op.code=3;
  }
  auto d = definitions(m);
  std::vector<Op> rewritten;
  auto one = [&](Id id) {
    auto p = get(d, id, 0);
    return p && p->imm == words(1, m.widths[id]);
  };
  for (const auto &original : m.ops) {
    Op op = original;
    const auto &a = original.args;
    if (op.code == 3)
      for (unsigned i = 0; i < 2; ++i) {
        auto combined = get(d, a[i], 4), inverted = get(d, a[1 - i], 2);
        if (combined && inverted)
          op = {26,
                op.out,
                {combined->args[0], combined->args[1], inverted->args[0]},
                {}};
      }
    if (original.code == 15)
      if (auto outer = select(d, op.out, m)) {
        auto event = get(d, (*outer)[0], 5);
        auto inner = select(d, (*outer)[1], m);
        if (event && inner) {
          auto plus = get(d, (*inner)[1], 6), minus = get(d, (*inner)[2], 7);
          if (plus && minus && plus->args[0] == (*outer)[2] &&
              minus->args[0] == (*outer)[2] && one(plus->args[1]) &&
              one(minus->args[1]))
            for (unsigned i = 0; i < 2; ++i)
              if ((*inner)[0] == event->args[i])
                op = {27,
                      op.out,
                      {(*outer)[2], (*inner)[0], event->args[1 - i]},
                      {}};
        }
        if (inner && m.widths[op.out] <= 64) {
          auto last = get(d, (*inner)[0], 12), zero = get(d, (*inner)[1], 0),
               plus = get(d, (*inner)[2], 6);
          if (last && zero && plus && zero->imm == std::vector<uint64_t>{0} &&
              plus->args[0] == (*outer)[2] && one(plus->args[1]))
            for (unsigned i = 0; i < 2; ++i)
              if (last->args[i] == (*outer)[2] && get(d, last->args[1 - i], 0))
                op = {28,
                      op.out,
                      {(*outer)[2], (*outer)[0], last->args[1 - i]},
                      {}};
        }
      }
    if (original.code == 17) {
      Id source = a[0];
      auto low = op.imm[0];
      auto width = m.widths[op.out];
      bool following = true;
      while (following) {
        auto p = get(d, source);
        following = false;
        if (p && p->code == 1) {
          source = p->args[0];
          following = true;
        } else if (p && p->code == 17) {
          source = p->args[0];
          low += p->imm[0];
          following = true;
        } else if (p && (p->code == 18 || p->code == 19) &&
                   low + width <= m.widths[p->args[0]]) {
          // An extension does not change bits within its original operand.
          // Follow that operand before emitting a slice, so narrowing a FIFO
          // or selection does not repack its zero/sign extension first.
          source = p->args[0];
          following = true;
        } else if (p && p->code == 20) {
          uint64_t pos = 0;
          for (auto field : p->args) {
            auto w = m.widths[field];
            if (!following && low >= pos && low + width <= pos + w) {
              source = field;
              low -= pos;
              following = true;
            }
            pos += w;
          }
        }
      }
      op = low == 0 && width == m.widths[source]
               ? Op{1, op.out, {source}, {}}
               : Op{17, op.out, {source}, {low}};
    }
    rewritten.push_back(std::move(op));
  }
  std::vector<Id> aliases(m.widths.size());
  for (Id i = 0; i < aliases.size(); ++i)
    aliases[i] = i;
  auto canonical = [&](Id id) { return id == none ? none : aliases[id]; };
  using Key =
      std::tuple<uint32_t, uint32_t, std::vector<Id>, std::vector<uint64_t>>;
  std::map<Key, Id> expressions;
  std::vector<std::optional<Big>> constants(m.widths.size());
  std::function<std::optional<Big>(Id, Id, unsigned, unsigned)> known;
  known = [&](Id id, Id guard, unsigned truth,
              unsigned fuel) -> std::optional<Big> {
    if (id == guard)
      return Big(truth);
    if (constants[id])
      return constants[id];
    if (!fuel)
      return {};
    auto g = get(d, guard), p = get(d, id);
    if (g && g->code == 2)
      return known(id, g->args[0], 1 - truth, fuel - 1);
    if (g && ((g->code == 3 && truth == 1) || (g->code == 4 && truth == 0))) {
      auto left = known(id, g->args[0], truth, fuel - 1);
      return left ? left : known(id, g->args[1], truth, fuel - 1);
    }
    if (p && p->code == 2) {
      auto v = known(p->args[0], guard, truth, fuel - 1);
      if (v)
        return Big(1 - *v);
    }
    return {};
  };
  auto guarded = [&](Id id, Id guard, unsigned truth) {
    while (true) {
      auto p = get(d, id);
      if (!p || p->code != 15 || p->args.size() != 3 ||
          m.widths[p->args[0]] != 1)
        return id;
      auto v = known(p->args[0], guard, truth, 6);
      if (!v)
        return id;
      id = p->args[*v == p->imm[0] ? 2 : 1];
    }
  };
  auto simplify = [&](Op op) -> Op {
    const auto &a = op.args;
    auto c = op.code, w = m.widths[op.out];
    Big full = mask(w);
    auto literal = [&](Big v) { return Op{0, op.out, {}, words(v, w)}; };
    auto copy = [&](Id id) { return Op{1, op.out, {id}, {}}; };
    if(c==33 && constants[a[0]]){Big selector=*constants[a[0]];
      if(selector>0 && (selector&(selector-1))==0){unsigned index=boost::multiprecision::lsb(selector);
        return {17,op.out,{a[1]},{op.imm.at(index)}};}}
    if(c==RDS_KERNEL_OPCODE && std::any_of(a.begin(),a.end(),[&](Id v){return constants[v].has_value();})){
      Model child=kernel_model(m,op);std::vector<Op> prefix;
      child.metadata["ports"]=Json::array();
      for(Id i=0;i<a.size();++i){
        if(constants[a[i]])prefix.push_back({0,i,{},words(*constants[a[i]],child.widths[i])});
        else child.metadata["ports"].push_back({0,i,"input"+std::to_string(i)});
      }
      child.metadata["ports"].push_back({1,child.ops.back().out,"result"});
      prefix.insert(prefix.end(),child.ops.begin(),child.ops.end());child.ops=std::move(prefix);
      optimize_body(child);Id result=child.metadata["ports"].back()[1];
      for(const auto &o:child.ops)if(o.out==result && o.code==0)return literal(number(o.imm));
    }
    std::optional<Id> redundant, absorbed;
    std::optional<Op> absorbed_op;
    if (c == 15 && a.size() == 3 && m.widths[a[0]] == 1 && op.imm.size() == 1 &&
        op.imm[0] <= 1)
      for (unsigned arm = 1; arm <= 2; ++arm) {
        auto p = get(d, a[arm], 15);
        if (p && p->args.size() == 3 && m.widths[p->args[0]] == 1) {
          unsigned truth = arm == 1 ? op.imm[0] : 1 - op.imm[0];
          auto selector = known(p->args[0], a[0], truth, 6);
          if (selector && p->args[*selector == p->imm[0] ? 2 : 1] == a[3 - arm])
            redundant = a[arm];
        }
      }
    if (c == 3 || c == 4)
      for (unsigned i = 0; i < 2; ++i) {
        auto p = get(d, a[1 - i]);
        if (p && (p->code == 3 || p->code == 4) &&
            std::find(p->args.begin(), p->args.end(), a[i]) != p->args.end()) {
          absorbed = c == p->code ? a[1 - i] : a[i];
          absorbed_op.reset();
        }
        if (p && p->code == 2 && p->args[0] == a[i]) {
          absorbed_op = literal(c == 3 ? Big(0) : full);
          absorbed.reset();
        }
      }
    auto x = a.empty() ? std::optional<Big>{} : constants[a[0]],
         y = a.size() < 2 ? std::optional<Big>{} : constants[a[1]];
    auto eq = [](const std::optional<Big> &v, const Big &n) {
      return v && *v == n;
    };
    if (redundant)
      return copy(*redundant);
    if (absorbed_op)
      return *absorbed_op;
    if (absorbed)
      return copy(*absorbed);
    if (c == 2 && x)
      return literal(*x ^ full);
    if (c == 17 && x)
      return literal(*x >> op.imm[0]);
    if (c == 18 && x)
      return literal(*x);
    if (c == 19 && x) {
      Big sign = Big(1) << (m.widths[a[0]] - 1);
      return literal((*x ^ sign) - sign);
    }
    if (c == 20 && std::all_of(a.begin(), a.end(), [&](Id id) {
          return constants[id].has_value();
        })) {
      Big v = 0;
      uint64_t offset = 0;
      for (auto id : a) {
        v |= *constants[id] << offset;
        offset += m.widths[id];
      }
      return literal(v);
    }
    if (x && y)
      switch (c) {
      case 3:
        return literal(*x & *y);
      case 4:
        return literal(*x | *y);
      case 5:
        return literal(*x ^ *y);
      case 6:
        return literal(*x + *y);
      case 7:
        return literal(*x - *y);
      case 8:
        return literal(*x * *y);
      case 9:
        return literal(*y >= w ? Big(0) : Big(*x << y->convert_to<uint32_t>()));
      case 10:
        return literal(*y >= w ? Big(0) : Big(*x >> y->convert_to<uint32_t>()));
      case 12:
        return literal(Big(*x == *y));
      case 13:
        return literal(Big(*x < *y));
      default:
        break;
      }
    if ((c == 3 || c == 4) && a[0] == a[1])
      return copy(a[0]);
    if ((c == 5 || c == 7 || c == 13) && a[0] == a[1])
      return literal(0);
    if (c == 12 && a[0] == a[1])
      return literal(1);
    if (c == 3 && (eq(x, 0) || eq(y, 0)))
      return literal(0);
    if (c == 3 && eq(x, full))
      return copy(a[1]);
    if (c == 3 && eq(y, full))
      return copy(a[0]);
    if (c == 4 && (eq(x, full) || eq(y, full)))
      return literal(full);
    if ((c == 4 || c == 5 || c == 6) && eq(x, 0))
      return copy(a[1]);
    if ((c == 4 || c == 5 || c == 6 || c == 7 || c == 9 || c == 10) && eq(y, 0))
      return copy(a[0]);
    if (c == 15 && x) {
      auto key = words(*x, m.widths[a[0]]);
      for (size_t i = 0; i < a.size() - 2; ++i)
        if (std::equal(key.begin(), key.end(), op.imm.begin() + i * key.size()))
          return copy(a[i + 2]);
      return copy(a[1]);
    }
    if (c == 15 &&
        std::all_of(a.begin() + 1, a.end(), [&](Id id) { return id == a[1]; }))
      return copy(a[1]);
    if (c == 15 && w == 1 && a.size() == 3 && m.widths[a[0]] == 1 &&
        op.imm == std::vector<uint64_t>{1}) {
      if (eq(y, 0))
        return {3, op.out, {a[0], a[2]}, {}};
      if (eq(constants[a[2]], 1))
        return {4, op.out, {a[0], a[1]}, {}};
    }
    return op;
  };
  std::vector<Op> unique;
  for (auto op : rewritten) {
    for (auto &a : op.args)
      a = canonical(a);
    if (op.code == 15 && op.args.size() == 3 && m.widths[op.args[0]] == 1 &&
        op.imm.size() == 1 && op.imm[0] <= 1) {
      op.args[1] = guarded(op.args[1], op.args[0], 1 - op.imm[0]);
      op.args[2] = guarded(op.args[2], op.args[0], op.imm[0]);
    }
    if ((op.code == 3 || op.code == 4 || op.code == 5 || op.code == 6 ||
         op.code == 8 || op.code == 12) &&
        op.args[0] > op.args[1])
      std::reverse(op.args.begin(), op.args.end());
    op = simplify(std::move(op));
    Key key{op.code, m.widths[op.out], op.args, op.imm};
    auto previous = expressions.find(key);
    if (op.code == 1)
      aliases[op.out] = op.args[0];
    else if (previous != expressions.end())
      aliases[op.out] = previous->second;
    else {
      expressions.emplace(std::move(key), op.out);
      d[op.out] = op;
      if (op.code == 0)
        constants[op.out] = number(op.imm);
      unique.push_back(std::move(op));
    }
  }
  compact_live(m, d, unique, canonical, true);
}
void specialize_decoders(Model &m) {
  const auto defs=definitions(m);
  struct Bit { Id source; unsigned offset; };
  // Constant fields belong to the caller, even when a shared decoder definition
  // includes all sites and input ports. Follow projections without inspecting
  // state, object identity or source names.
  std::function<Bit(Id,unsigned,unsigned)> bit=[&](Id v,unsigned b,unsigned fuel)->Bit {
    const auto *o=get(defs,v);if(!o||!fuel)return {v,b};
    switch(o->code){
      case 0:return {none,unsigned((o->imm[b/64]>>(b%64))&1)};
      case 1:return bit(o->args[0],b,fuel-1);
      case 17:return bit(o->args[0],b+o->imm[0],fuel-1);
      case 18:case 19:{unsigned w=m.widths[o->args[0]];
        return b<w?bit(o->args[0],b,fuel-1):o->code==18?Bit{none,0}:bit(o->args[0],w-1,fuel-1);}
      case 20:{unsigned low=0;for(Id a:o->args){unsigned w=m.widths[a];if(b-low<w)return bit(a,b-low,fuel-1);low+=w;}break;}
      case 2:{auto a=bit(o->args[0],b,fuel-1);if(a.source==none)return {none,a.offset^1};break;}
    }
    return {v,b};
  };
  std::vector<Op> ops;Json details=Json::array();
  auto emit=[&](unsigned code,unsigned width,std::vector<Id> args,std::vector<uint64_t> imm=std::vector<uint64_t>{}) {
    Id id=m.widths.size();m.widths.push_back(width);ops.push_back({code,id,std::move(args),std::move(imm)});return id;
  };
  for(auto o:m.ops){
    if(o.code!=25||m.widths[o.args[0]]>64){ops.push_back(std::move(o));continue;}
    unsigned sw=m.widths[o.args[0]],nw=(m.widths[o.out]+63)/64;
    uint64_t known=0,value=0;std::vector<unsigned>positions;std::vector<Bit>live;
    for(unsigned b=0;b<sw;++b){auto x=bit(o.args[0],b,96);if(x.source==none){known|=UINT64_C(1)<<b;value|=uint64_t(x.offset)<<b;}else{positions.push_back(b);live.push_back(x);}}
    if(!known){ops.push_back(std::move(o));continue;}
    unsigned before=o.imm[0];
    // Bits outside the declared selector are also zero in every legal value.
    if(sw<64)known|=UINT64_MAX<<sw;
    auto project=[&](uint64_t x){uint64_t result=0;for(unsigned b=0;b<positions.size();++b)result|=((x>>positions[b])&1)<<b;return result;};
    std::vector<uint64_t>im(o.imm.begin(),o.imm.begin()+1+nw);im[0]=0;
    for(unsigned row=0;row<before;++row){const uint64_t*r=o.imm.data()+1+nw+row*(2+nw);
      if((r[0]&~r[1])||((r[0]^value)&known&r[1]))continue;
      ++im[0];im.push_back(project(r[0]));im.push_back(project(r[1]));im.insert(im.end(),r+2,r+2+nw);
      if(!im[im.size()-nw-1])break; // First unconditional match hides all later rows.
    }
    if(live.empty()||!im[0]||(im[0]==1&&im[2+nw]==0)){
      auto first=im.begin()+(im[0]?3+nw:1);o.code=0;o.args.clear();o.imm.assign(first,first+nw);
    }else{
      std::vector<Id>parts;
      for(unsigned b=0;b<live.size();){unsigned end=b+1;while(end<live.size()&&live[end].source==live[b].source&&live[end].offset==live[b].offset+end-b)++end;
        unsigned width=end-b;Id source=live[b].source;
        parts.push_back(live[b].offset==0&&width==m.widths[source]?source:emit(17,width,{source},{live[b].offset}));b=end;
      }
      o.args={parts.size()==1?parts[0]:emit(20,live.size(),std::move(parts))};o.imm=im;
    }
    details.push_back({{"output",o.out},{"selector_bits_before",sw},{"selector_bits_after",live.size()},
                       {"rows_before",before},{"rows_after",im[0]},{"constant",o.code==0}});
    ops.push_back(std::move(o));
  }
  m.ops=std::move(ops);m.metadata["decoder_specialization_summary"]={{"decoders",details},{"ids","pass input values"}};
  if(!details.empty())optimize_body(m);
}
void share_decoders(Model &m) {
  using Key = std::pair<Id, std::vector<uint64_t>>;
  std::map<Key, size_t> groups;
  std::map<Key, Id> decoders;
  for (const auto &op : m.ops)
    if (op.code == 15 && op.args.size() > 9)
      ++groups[{op.args[0], op.imm}];
  std::vector<Op> ops;
  for (auto op : m.ops) {
    if (op.code != 15 || groups[{op.args[0], op.imm}] <= 1) {
      ops.push_back(std::move(op));
      continue;
    }
    Key key{op.args[0], op.imm};
    size_t cases = op.args.size() - 2;
    if (!decoders.count(key)) {
      uint32_t width = 1;
      while ((uint64_t(1) << width) <= cases)
        ++width;
      Id id = m.widths.size();
      m.widths.push_back(width);
      decoders[key] = id;
      auto sw = m.widths[op.args[0]];
      auto full = words(mask(sw), sw);
      size_t nw = full.size();
      std::vector<uint64_t> rows{cases, 0};
      for (size_t i = 0; i < cases; ++i) {
        rows.insert(rows.end(), op.imm.begin() + i * nw,
                    op.imm.begin() + (i + 1) * nw);
        rows.insert(rows.end(), full.begin(), full.end());
        rows.push_back(i + 1);
      }
      ops.push_back({25, id, {op.args[0]}, std::move(rows)});
    }
    std::vector<Id> args{decoders[key], op.args[1]};
    args.insert(args.end(), op.args.begin() + 1, op.args.end());
    op.args = std::move(args);
    op.imm.clear();
    for (size_t i = 0; i <= cases; ++i)
      op.imm.push_back(i);
    ops.push_back(std::move(op));
  }
  m.ops = std::move(ops);
}
void share_matchers(Model &m) {
  using Key = std::tuple<uint64_t, uint64_t, std::vector<Id>>;
  std::map<Key, std::pair<Id, Id>> prefixes;
  std::vector<Op> ops;
  auto emit = [&](uint32_t c, uint32_t w, std::vector<Id> a,
                  std::vector<uint64_t> imm) {
    Id id = m.widths.size();
    m.widths.push_back(w);
    ops.push_back({c, id, std::move(a), std::move(imm)});
    return id;
  };
  for (auto op : m.ops) {
    if (op.code != 29) {
      ops.push_back(std::move(op));
      continue;
    }
    const auto &object = m.metadata["objects"][op.imm[0]];
    uint32_t depth = object[2], n = object[1];
    uint32_t stride = (object[3].get<uint32_t>() & 32) ? 1 : n;
    if (object[0] != 10 || op.imm[1] >= depth) {
      ops.push_back(std::move(op));
      continue;
    }
    Id taken = emit(0, n, {}, words(0, n)), grant = taken;
    for (uint64_t col = 0; col <= op.imm[1]; ++col) {
      Key key{
          op.imm[0], col,
          std::vector<Id>(op.args.begin(), op.args.begin() + (col + 1) * stride)};
      auto prior = prefixes.find(key);
      if (prior != prefixes.end()) {
        grant = prior->second.first;
        taken = prior->second.second;
      } else {
        std::vector<Id> args{taken};
        args.insert(args.end(), op.args.begin() + col * stride,
                    op.args.begin() + (col + 1) * stride);
        grant = emit(29, n, std::move(args), {op.imm[0], depth + col});
        taken = emit(4, n, {taken, grant}, {});
        prefixes[key] = {grant, taken};
      }
    }
    ops.push_back({1, op.out, {grant}, {}});
  }
  m.ops = std::move(ops);
}
void recover_words(Model &m) {
  Defs d(m.widths.size());
  std::vector<Op> ops;
  auto emit = [&](uint32_t c, uint32_t w, std::vector<Id> a,
                  std::vector<uint64_t> imm = std::vector<uint64_t>{}) {
    Id id = m.widths.size();
    m.widths.push_back(w);
    Op op{c, id, std::move(a), std::move(imm)};
    d.push_back(op);
    ops.push_back(std::move(op));
    return id;
  };
  std::function<Id(const std::vector<Id> &, unsigned)> compose;
  compose = [&](const std::vector<Id> &fields, unsigned fuel) -> Id {
    uint32_t width = 0;
    for (auto id : fields)
      width += m.widths[id];
    // Copy definitions: recursive emission may reallocate the definition arena.
    std::vector<std::optional<Op>> parts;
    for (auto id : fields)
      parts.push_back(d[id]);
    auto first = parts[0];
    bool contiguous = first && first->code == 17;
    uint64_t pos = contiguous ? first->imm[0] : 0;
    for (size_t i = 0; i < parts.size(); ++i) {
      const auto &p = parts[i];
      contiguous = contiguous && p && p->code == 17 &&
                   p->args[0] == first->args[0] && p->imm[0] == pos;
      pos += m.widths[fields[i]];
    }
    if (fields.size() == 1)
      return fields[0];
    if (contiguous)
      return first->imm[0] == 0 && width == m.widths[first->args[0]]
                 ? first->args[0]
                 : emit(17, width, first->args, first->imm);
    if (fuel && width <= 64 && first && first->code == 15 &&
        first->args.size() == 3 &&
        std::all_of(parts.begin(), parts.end(), [&](const auto &p) {
          return p && p->code == 15 && p->args.size() == 3 &&
                 p->args[0] == first->args[0] && p->imm == first->imm;
        })) {
      std::vector<Id> a, b;
      for (const auto &p : parts) {
        a.push_back(p->args[1]);
        b.push_back(p->args[2]);
      }
      Id fallback = compose(a, fuel - 1), selected = compose(b, fuel - 1);
      return emit(15, width, {first->args[0], fallback, selected}, first->imm);
    }
    if (width <= 64 &&
        std::all_of(fields.begin(), fields.end(),
                    [&](Id id) { return m.widths[id] == 8; }) &&
        std::all_of(parts.begin(), parts.end(), [&](const auto &p) {
          return p && p->code == 15 && p->args.size() == 3 &&
                 p->imm == std::vector<uint64_t>{1};
        })) {
      std::vector<std::optional<Op>> masks;
      for (const auto &p : parts)
        masks.push_back(d[p->args[0]]);
      bool lanes = masks[0] && masks[0]->code == 17;
      for (size_t i = 0; i < masks.size(); ++i) {
        const auto &p = masks[i];
        lanes = lanes && p && p->code == 17 && m.widths[p->out] == 1 &&
                p->args == masks[0]->args && p->imm == std::vector<uint64_t>{i};
      }
      if (lanes && m.widths[masks[0]->args[0]] == fields.size()) {
        std::vector<Id> a, b;
        for (const auto &p : parts) {
          a.push_back(p->args[1]);
          b.push_back(p->args[2]);
        }
        Id fallback = compose(a, 0), selected = compose(b, 0);
        return emit(31, width, {fallback, selected, masks[0]->args[0]});
      }
    }
    return emit(20, width, fields);
  };
  for (auto op : m.ops) {
    if (op.code == 20 && op.args.size() > 1)
      op = {1, op.out, {compose(op.args, 4)}, {}};
    d[op.out] = op;
    ops.push_back(std::move(op));
  }
  m.ops = std::move(ops);
}
} // namespace rds
