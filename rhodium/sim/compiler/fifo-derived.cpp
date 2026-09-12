// Stores proved pure payload-derived fields in FIFO slots and computes them only on enqueue.
#include "model.hpp"
#include <algorithm>
#include <optional>
#include <stdexcept>

namespace rds {
namespace {
// These operations are total. Partial selections, memory and state-dependent
// queries cannot be evaluated early at a payload's enqueue event.
bool total(unsigned code) {
  return code <= 10 || code == 12 || code == 13 || code == 15 ||
         code == 17 || code == 18 || code == 19 || code == 20 || code == 25;
}
Big evaluate(const Model &m, const Op &o, const std::vector<Big> &zero) {
  auto a = [&](size_t i) -> const Big & { return zero[o.args[i]]; };
  unsigned width = m.widths[o.out];
  Big value = 0;
  switch (o.code) {
  case 0: value = number(o.imm); break;
  case 1: case 18: value = a(0); break;
  case 2: value = a(0) ^ mask(width); break;
  case 3: value = a(0) & a(1); break;
  case 4: value = a(0) | a(1); break;
  case 5: value = a(0) ^ a(1); break;
  case 6: value = a(0) + a(1); break;
  case 7: value = a(0) - a(1); break;
  case 8: value = a(0) * a(1); break;
  case 9: if (a(1) < width) value = a(0) << a(1).convert_to<unsigned>(); break;
  case 10: if (a(1) < width) value = a(0) >> a(1).convert_to<unsigned>(); break;
  case 12: value = a(0) == a(1); break;
  case 13: value = a(0) < a(1); break;
  case 15: {
    value = a(1);
    auto selector = words(a(0), m.widths[o.args[0]]);
    for (size_t i = 2; i < o.args.size(); ++i)
      if (std::equal(selector.begin(), selector.end(),
                     o.imm.begin() + (i - 2) * selector.size())) {
        value = a(i); break;
      }
    break;
  }
  case 17: value = a(0) >> o.imm[0]; break;
  case 19: {
    Big sign = Big(1) << (m.widths[o.args[0]] - 1);
    value = (a(0) ^ sign) - sign; break;
  }
  case 20: {
    unsigned offset = 0;
    for (Id id : o.args) { value |= zero[id] << offset; offset += m.widths[id]; }
    break;
  }
  case 25: {
    const unsigned sw=(m.widths[o.args[0]]+63)/64,ow=(width+63)/64;
    auto read=[&](size_t at,unsigned count){Big result=0;for(unsigned i=0;i<count;++i)result|=Big(o.imm.at(at+i))<<(64*i);return result;};
    value=read(1,ow);
    for(size_t row=0;row<o.imm[0];++row){size_t at=1+ow+row*(2*sw+ow);
      if((a(0)&read(at+sw,sw))==read(at,sw)){value=read(at+2*sw,ow);break;}
    }
    break;
  }
  default: throw std::runtime_error("unsupported derived FIFO operation");
  }
  return value & mask(width);
}
void order(Model &m) {
  std::vector<Id> defs(m.widths.size(), none);
  for (Id i = 0; i < m.ops.size(); ++i) defs[m.ops[i].out] = i;
  std::vector<unsigned char> seen(m.ops.size());
  std::vector<Op> ordered;
  std::function<void(Id)> visit = [&](Id i) {
    if (i == none || seen[i] == 2) return;
    if (seen[i]) throw std::runtime_error("derived FIFO combinational cycle");
    seen[i] = 1;
    for (Id a : m.ops[i].args) visit(defs[a]);
    seen[i] = 2; ordered.push_back(m.ops[i]);
  };
  for (Id i = 0; i < m.ops.size(); ++i) visit(i);
  m.ops = std::move(ordered);
}
} // namespace

void cache_fifo_derived(Model &m) {
  const auto original = m.ops;
  const auto objects = m.metadata["objects"];
  const Id constant = none - 1;
  std::vector<Id> domain(m.widths.size(), none), defs(m.widths.size(), none);
  std::vector<Big> zero(m.widths.size());
  for (Id i = 0; i < original.size(); ++i) {
    const auto &o = original[i]; defs[o.out] = i;
    Id d = constant;
    if (o.code == 29 && o.args.empty() && o.imm[1] == 3 &&
        objects[o.imm[0]][0] == 1 && objects[o.imm[0]][3] == 0) {
      domain[o.out] = o.imm[0]; continue;
    }
    if (!total(o.code)) continue;
    for (Id a : o.args) {
      if (domain[a] == constant) continue;
      if (domain[a] == none || (d != constant && d != domain[a])) { d = none; break; }
      d = domain[a];
    }
    if (d != none) { domain[o.out] = d; zero[o.out] = evaluate(m, o, zero); }
  }
  std::vector<bool> frontier(domain.size());
  auto sink = [&](Id v) {
    if (v != none && domain[v] < objects.size() && m.widths[v] <= 64)
      frontier[v] = true;
  };
  for (const auto &o : original)
    for (Id a : o.args) if (domain[a] != domain[o.out]) sink(a);
  for (const auto &p : m.metadata["ports"]) sink(p[1]);
  for (const auto &r : m.metadata["registers"]) for (const auto &v : r) sink(v);
  for (const auto &o : objects) for (const auto &v : o[4]) sink(v);
  // Other effect consumers are left untouched by keeping their existing IDs;
  // eligible roots can still be shared with one of the above consumers.
  std::vector<std::vector<Id>> roots(objects.size());
  for (Id v = 0; v < frontier.size(); ++v) if (frontier[v]) roots[domain[v]].push_back(v);
  auto emit = [&](unsigned code, unsigned width, std::vector<Id> args,
                  std::vector<uint64_t> imm = {}) {
    Id id = m.widths.size(); m.widths.push_back(width);
    m.ops.push_back({code, id, std::move(args), std::move(imm)}); return id;
  };
  Json summary = Json::array();
  for (Id id = 0; id < objects.size(); ++id) {
    if (roots[id].empty()) continue;
    unsigned width = objects[id][1], extra = 0;
    for (Id v : roots[id]) extra += m.widths[v];
    // Avoid a larger physical slot for this first policy. Sparse regions can
    // favor recomputation; the option stays explicit and is benchmarked.
    if (extra > 64 || width + extra > ((width + 63) / 64) * 64) continue;
    std::vector<bool> needed(original.size()); unsigned operations = 0;uint64_t work = 0;
    std::function<void(Id)> collect = [&](Id v) {
      if (domain[v] == constant || defs[v] == none) return;
      Id at = defs[v]; if (needed[at] || original[at].code == 29) return;
      needed[at] = true; ++operations;
      // A decoder is one IR node but performs table matching. Charge its rows
      // so route lookup can justify enqueue caching without expanding the DFG.
      work += original[at].code==25?4+2*original[at].imm[0]:1;
      for (Id a : original[at].args) collect(a);
    };
    for (Id v : roots[id]) collect(v);
    if (work < roots[id].size() * 2 + 4 || work > 256) continue;
    Id incoming = objects[id][4][2];
    std::vector<Id> mapped(domain.size(), none);
    for (const auto &o : original) {
      if (domain[o.out] == constant) mapped[o.out] = o.out;
      if (domain[o.out] == id && o.code == 29) mapped[o.out] = incoming;
    }
    for (Id at = 0; at < original.size(); ++at) if (needed[at]) {
      auto o = original[at];
      for (Id &a : o.args) {
        if (mapped[a] == none) throw std::runtime_error("unclosed derived FIFO cone");
        a = mapped[a];
      }
      mapped[o.out] = emit(o.code, m.widths[o.out], o.args, o.imm);
    }
    std::vector<Id> payload{incoming};
    Id query = emit(29, width + extra, {}, {id, 3});
    unsigned offset = width;
    for (Id v : roots[id]) {
      unsigned bits = m.widths[v]; Id encoded = mapped[v];
      Id decoded = emit(17, bits, {query}, {offset});
      // Empty and reset slots retain observable old payloads. Encode f(x)^f(0)
      // so the native zero-initialized slot also represents f(0) exactly.
      if (zero[v] != 0) {
        Id seed = emit(0, bits, {}, words(zero[v], bits));
        encoded = emit(5, bits, {encoded, seed});
        decoded = emit(5, bits, {decoded, seed});
      }
      payload.push_back(encoded); m.ops[defs[v]] = {1, v, {decoded}, {}};
      offset += bits;
    }
    for (Id at = 0; at < original.size(); ++at) {
      const auto &o = original[at];
      if (domain[o.out] == id && o.code == 29)
        m.ops[at] = {17, o.out, {query}, {0}};
    }
    Id packed = emit(20, width + extra, payload);
    m.metadata["objects"][id][1] = width + extra;
    m.metadata["objects"][id][4][2] = packed;
    summary.push_back({{"name",objects[id][5]},{"payload_width",width},
                       {"derived_bits",extra},{"moved_operations",operations},
                       {"estimated_work",work},{"roots",roots[id]}});
  }
  m.metadata["fifo_derived_summary"] = summary;
  if (!summary.empty()) { order(m); m.validate(); optimize_body(m); }
}
} // namespace rds
