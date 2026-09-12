// Reads, validates, and writes pointer-free simulation graphs and runtime
// images.
#include "model.hpp"
#include "kernel.hpp"
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>
namespace rds {
Big mask(uint32_t width) { return (Big(1) << width) - 1; }
Big number(const std::vector<uint64_t> &v) {
  Big n = 0;
  for (size_t i = 0; i < v.size(); ++i)
    n |= Big(v[i]) << (64 * i);
  return n;
}
std::vector<uint64_t> words(Big n, uint32_t width) {
  n &= mask(width);
  std::vector<uint64_t> out;
  for (uint64_t i = 0; i < (uint64_t(width) + 63) / 64; ++i)
    out.push_back(((n >> (64 * i)) & mask(64)).convert_to<uint64_t>());
  return out;
}
static uint64_t natural(const Json &j, uint64_t limit = UINT32_MAX) {
  if (!j.is_number_integer() ||
      (j.is_number_integer() && !j.is_number_unsigned() &&
       j.get<int64_t>() < 0))
    throw std::runtime_error("expected nonnegative integer");
  auto n = j.get<uint64_t>();
  if (n > limit)
    throw std::runtime_error("integer exceeds format capacity");
  return n;
}
static void row(const Json &j, size_t n) {
  if (!j.is_array() || j.size() != n)
    throw std::runtime_error("invalid record arity");
}
static const std::vector<std::string>& opcode_names() {
  static const std::vector<std::string> names = {"constant",
                                            "copy",
                                            "not",
                                            "and",
                                            "or",
                                            "xor",
                                            "add",
                                            "sub",
                                            "mul",
                                            "shl",
                                            "shru",
                                            "shrs",
                                            "eq",
                                            "ult",
                                            "slt",
                                            "mux_lookup",
                                            "onehot_mux",
                                            "extract",
                                            "zext",
                                            "sext",
                                            "pack",
                                            "vector_index",
                                            "vector_inject",
                                            "vector_write_set",
                                            "memory_read_async",
                                            "decode",
                                            "set_clear",
                                            "balance",
                                            "counter_step",
                                            "object_query",
                                            "alu",
                                            "byte_merge"};
  return names;
}
Model Model::read(const std::string &path, bool semantic_optimize) {
  std::ifstream f(path);
  if (!f)
    throw std::runtime_error("cannot read " + path);
  Model m;
  f >> m.metadata;
  m.metadata = lower_semantic(std::move(m.metadata), semantic_optimize);
  if (m.metadata.at("format") != "rhodium-simulation-ir-v1")
    throw std::runtime_error("unsupported IR format");
  const auto &opcodes = opcode_names();

  auto extended = opcodes;
  extended.push_back("contract_kernel");
  auto views=extended;views.push_back("onehot_view");
  if (m.metadata.at("opcodes") != opcodes && m.metadata.at("opcodes") != extended && m.metadata.at("opcodes") != views)
    throw std::runtime_error("unsupported opcode contract");
  if (!m.metadata.at("values").is_array() ||
      !m.metadata.at("operations").is_array())
    throw std::runtime_error("invalid graph sections");
  for (const auto &x : m.metadata.at("values"))
    m.widths.push_back(natural(x));
  for (const auto &x : m.metadata.at("operations")) {
    row(x, 4);
    Op op{uint32_t(natural(x[0])), uint32_t(natural(x[1])), {}, {}};
    if(op.code>=m.metadata.at("opcodes").size())
      throw std::runtime_error("opcode missing from exchange contract");
    if (!x[2].is_array() || !x[3].is_array())
      throw std::runtime_error("invalid operands/immediates");
    for (const auto &a : x[2])
      op.args.push_back(natural(a));
    for (const auto &a : x[3])
      op.imm.push_back(natural(a, UINT64_MAX));
    m.ops.push_back(std::move(op));
  }
  for (auto name : {"registers", "memories", "writes", "reads", "assertions",
                    "ports", "objects", "origins", "inventory"})
    if (!m.metadata.at(name).is_array())
      throw std::runtime_error(std::string("invalid section ") + name);
  m.validate();
  return m;
}
Json Model::json() const {
  Json j = metadata;
  if(!j.contains("opcodes") || j["opcodes"].is_null()) j["opcodes"] = opcode_names();
  if(j["opcodes"].size()==32 && std::any_of(ops.begin(),ops.end(),[](const Op &o){return o.code>=32;}))
    j["opcodes"].push_back("contract_kernel");
  if(j["opcodes"].size()==33 && std::any_of(ops.begin(),ops.end(),[](const Op &o){return o.code==33;}))
    j["opcodes"].push_back("onehot_view");
  j["values"] = widths;
  j["operations"] = Json::array();
  for (const auto &op : ops)
    j["operations"].push_back(Json::array({op.code, op.out, op.args, op.imm}));
  return j;
}
void Model::validate() const {
  if (widths.size() >= none)
    throw std::runtime_error("too many values");
  for (auto w : widths)
    if (!w || w > UINT32_MAX - 63)
      throw std::runtime_error("invalid value width");
  auto id = [&](const Json &j, bool optional = false) {
    auto n = natural(j);
    if (n >= widths.size() && !(optional && n == none))
      throw std::runtime_error("invalid value ID");
    return Id(n);
  };
  auto width = [&](Id n) { return widths.at(n); };
  if(metadata.contains("contracts")) {
    if(!metadata["contracts"].is_array())throw std::runtime_error("invalid contracts section");
    for(const auto &c:metadata["contracts"]){
      row(c,4); auto occurrence=natural(c[0]);
      if(!metadata.contains("occurrences")||occurrence>=metadata["occurrences"].size()||!c[1].is_string()||!c[2].is_array()||!c[3].is_array())
        throw std::runtime_error("invalid simulation contract: " + c.dump());
      std::set<std::string> labels;
      for(const auto &b:c[2]){
        row(b,2);
        if(!b[0].is_string()||!labels.insert(b[0].get<std::string>()).second)throw std::runtime_error("invalid contract binding label");
        id(b[1],true);
      }
    }
  }
  std::string context = "metadata";
  auto check = [&](bool ok) {
    if (!ok)
      throw std::runtime_error("invalid IR shape, width, or dependency: " +
                               context);
  };
  std::vector<bool> defined(widths.size());
  auto source = [&](Id n) {
    check(!defined[n]);
    defined[n] = true;
  };
  std::set<std::string> names;
  for (const auto &p : metadata.at("ports")) {
    context = "ports " + p.dump();
    row(p, 3);
    check(natural(p[0]) <= 2 && p[2].is_string());
    auto n = id(p[1]);
    check(names.insert(p[2]).second);
    if (p[0] != 1)
      source(n);
  }
  for (const auto &r : metadata.at("registers")) {
    context = "registers " + r.dump();
    row(r, 4);
    auto q = id(r[0]), d = id(r[1]);
    check(width(q) == width(d));
    source(q);
    auto reset = id(r[2], true);
    auto v = id(r[3], true);
    if (reset != none)
      check(width(reset) == 1 && v != none && width(v) == width(q));
  }
  for (const auto &m : metadata.at("memories")) {
    context = "memories " + m.dump();
    row(m, 2);
    check(natural(m[0]) > 0 && natural(m[1]) > 0);
  }
  auto memory = [&](const Json &n) {
    auto x = natural(n);
    check(x < metadata.at("memories").size());
    return x;
  };
  for (const auto &r : metadata.at("reads")) {
    context = "reads " + r.dump();
    row(r, 5);
    auto m = memory(r[0]);
    id(r[1]);
    check(width(id(r[2])) == 1);
    source(id(r[3]));
    check(width(id(r[3])) == metadata["memories"][m][0]);
    auto wr = id(r[4], true);
    if (wr != none)
      check(width(wr) == 1);
  }
  for (const auto &w : metadata.at("writes")) {
    context = "writes " + w.dump();
    row(w, 6);
    auto m = memory(w[0]);
    id(w[1]);
    check(width(id(w[3])) == 1 &&
          width(id(w[2])) == metadata["memories"][m][0]);
    id(w[4], true);
    natural(w[5]);
  }
  for (const auto &c : metadata.at("assertions")) {
    context = "assertions " + c.dump();
    row(c, 4);
    for (size_t i = 0; i < 3; ++i) {
      auto n = id(c[i], true);
      if (n != none)
        check(width(n) == 1);
    }
    check(c[3].is_string());
  }
  for (const auto &o : metadata.at("objects")) {
    context = "objects " + o.dump();
    row(o, 6);
    check(natural(o[0]) >= 1 && natural(o[0]) <= 11);
    natural(o[1]);
    check(natural(o[2]) > 0);
    natural(o[3]);
    check(o[4].is_array() && o[5].is_string());
    for (const auto &a : o[4])
      id(a, true);
    if(o[0] == 11) {
      static const unsigned widths[]={1,1,1,1,27,53,64,1,2,2,1,1,64,1,2,1,1};
      auto depth=natural(o[2]);check(o[1]==81 && o[3]==0 && depth>=2 && !(depth&(depth-1)) && o[4].size()==17);
      for(size_t i=0;i<17;++i)check(width(id(o[4][i]))==widths[i]);
    }else if(o[0] == 10) {
      auto count = natural(o[1]), depth = natural(o[2]), flags = natural(o[3]);
      check(count > 0 && count <= 64 && depth <= 64 && !(flags & ~uint64_t(96)) && (!(flags & 64) || (flags & 32)));
      bool packed = (flags & 32) != 0;
      check(o[4].size() == (packed ? depth + 2 : 1 + count * depth + depth));
      for(size_t i=0;i<o[4].size();++i)
        check(width(id(o[4][i])) == (packed && i ? i<=depth ? count : depth : 1));
    }
  }
  for (const auto &op : ops) {
    context = "opcode " + std::to_string(op.code) + " output " +
              std::to_string(op.out);
    check(op.code <= 33 && op.out < widths.size() && !defined[op.out]);
    for (auto a : op.args)
      check(a < widths.size() && defined[a]);
    auto n = op.args.size(), k = op.imm.size();
    auto w = width(op.out);
    auto aw = [&](size_t i) { return width(op.args.at(i)); };
    switch (op.code) {
    case 32:
      kernel_model(*this,op).validate();
      break;
    case 0:
      check(!n && k == (uint64_t(w) + 63) / 64 &&
            (number(op.imm) & mask(w)) == number(op.imm));
      break;
    case 1:
    case 2:
      check(n == 1 && !k && aw(0) == w);
      break;
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
      check(n == 2 && !k && aw(0) == w && aw(1) == w);
      break;
    case 9:
    case 10:
    case 11:
      check(n == 2 && !k && aw(0) == w);
      break;
    case 12:
    case 13:
    case 14:
      check(n == 2 && !k && w == 1 && aw(0) == aw(1));
      break;
    case 15:
      check(n >= 2 && k == (n - 2) * ((uint64_t(aw(0)) + 63) / 64));
      for (size_t i = 1; i < n; ++i)
        check(aw(i) == w);
      break;
    case 16:
      check(n >= 2 && !k && aw(0) == n - 1);
      for (size_t i = 1; i < n; ++i)
        check(aw(i) == w);
      break;
    case 33:
      check(n==2 && aw(0)<=64 && k==aw(0));
      for(uint64_t low:op.imm)check(low<=aw(1) && uint64_t(w)+low<=aw(1));
      break;
    case 17:
      check(n == 1 && k == 1 && op.imm[0] <= aw(0) &&
            uint64_t(w) + op.imm[0] <= aw(0));
      break;
    case 18:
    case 19:
      check(n == 1 && !k && aw(0) <= w);
      break;
    case 20: {
      check(n > 0 && !k);
      uint64_t sum = 0;
      for (auto a : op.args)
        sum += width(a);
      check(sum == w);
      break;
    }
    case 21:
      check(n == 2 && k == 2 && op.imm[0] > 0 && op.imm[1] > 0 &&
            op.imm[1] == w && op.imm[0] <= UINT32_MAX / op.imm[1] &&
            op.imm[0] * op.imm[1] == aw(0));
      break;
    case 22:
      check(n == 3 && k == 2 && op.imm[0] > 0 && op.imm[1] > 0 && aw(0) == w &&
            op.imm[1] == aw(2) && op.imm[0] <= UINT32_MAX / op.imm[1] &&
            op.imm[0] * op.imm[1] == w);
      break;
    case 23:
      check(n == 4 && k == 4 && op.imm[0] && op.imm[1] && op.imm[2] &&
            op.imm[3] && op.imm[0] <= UINT32_MAX / op.imm[1] &&
            op.imm[2] <= UINT32_MAX / op.imm[3] &&
            op.imm[2] <= UINT32_MAX / op.imm[1] && w == op.imm[0] * op.imm[1] &&
            aw(0) == w && aw(1) == op.imm[2] &&
            aw(2) == op.imm[2] * op.imm[3] && aw(3) == op.imm[2] * op.imm[1] &&
            op.imm[3] <= 64);
      break;
    case 24:
      check(n == 1 && k == 1 && op.imm[0] < metadata.at("memories").size());
      break;
    case 25: {
      check(n == 1 && k >= 1);
      uint64_t nw = (uint64_t(w) + 63) / 64,
               stride = 2 * ((uint64_t(aw(0)) + 63) / 64) + nw;
      check(op.imm[0] <= (UINT32_MAX - 1 - nw) / stride &&
            k == 1 + nw + op.imm[0] * stride);
      break;
    }
    case 26:
      check(n == 3 && !k && aw(0) == w && aw(1) == w && aw(2) == w);
      break;
    case 27:
      check(n == 3 && !k && aw(0) == w && aw(1) == 1 && aw(2) == 1);
      break;
    case 28:
      check(n == 3 && !k && aw(0) == w && aw(1) == 1 && aw(2) == w && w <= 64);
      break;
    case 29:
      check(k == 2 && op.imm[0] < metadata.at("objects").size());
      {
        const auto &o = metadata["objects"][op.imm[0]];
        if(o[0]==11){
          auto q=op.imm[1];check(q<=5 && w==(q==0||q==5?128:1));
          check(n==(q==0||q==5?2:q==1?6:q==2?5:q==3?5:4));
          for(size_t i=0;i<n;++i){unsigned expected=q==0||q==5?(i?1:64):q<=2?(i==0?64:i==1?1:i<(q==1?4u:3u)?2:1):i==0?128:i<(q==3?3u:2u)?2:1;check(aw(i)==expected);}
        }
        if (o[0] == 10) {
          auto count = o[1].get<size_t>(), depth = o[2].get<size_t>();
          check(count > 0 && count <= 64 && op.imm[1] < 2 * depth);
          bool packed = (o[3].get<uint32_t>() & 32) != 0;
          check(n == (op.imm[1] < depth ? (op.imm[1] + 1) * (packed ? 1 : count) : packed ? 2 : count + 1));
          for(size_t i=0;i<n;++i)
            check(aw(i) == (packed || (op.imm[1]>=depth && i==0) ? count : 1));
        }
      }
      break;
    case 30:
      check(n == 3 && !k && (w == 32 || w == 64) && aw(0) == w && aw(1) == w &&
            aw(2) == 23);
      break;
    case 31:
      check(n == 3 && !k && w <= 64 && w % 8 == 0 && aw(0) == w && aw(1) == w &&
            aw(2) == w / 8);
      break;
    }
    defined[op.out] = true;
  }
  context = "undefined value declaration";
  for (bool value : defined)
    check(value);
  auto demand = [&](const Json &j) {
    auto n = id(j, true);
    check(n == none || defined[n]);
  };
  for (const auto &p : metadata.at("ports"))
    demand(p[1]);
  for (const auto &r : metadata.at("registers"))
    for (const auto &a : r)
      demand(a);
  for (const auto &r : metadata.at("reads"))
    for (size_t i = 1; i < 5; ++i)
      demand(r[i]);
  for (const auto &r : metadata.at("writes"))
    for (size_t i = 1; i < 5; ++i)
      demand(r[i]);
  for (const auto &r : metadata.at("assertions"))
    for (size_t i = 0; i < 3; ++i)
      demand(r[i]);
  for (const auto &o : metadata.at("objects"))
    for (const auto &a : o[4])
      demand(a);
  for (const auto &o : metadata.at("origins")) {
    check(o.is_array() && !o.empty());
    id(o[0]);
  }
  validate_semantic_structure(*this);
  if (metadata.contains("semantic_structure")) {
    // Validate references without turning provenance into liveness roots.
    for (const auto &r : metadata["semantic_structure"]["ranges"]) demand(r[3]);
    for (const auto &g : metadata["semantic_structure"]["conditionals"]) {
      demand(g[4]); demand(g[5]);
      for (const auto &a : g[6]) demand(a);
    }
  }
}
void Model::write_binary(const std::string &path) const {
  std::ofstream out(path, std::ios::binary);
  if (!out)
    throw std::runtime_error("cannot write " + path);
  auto word = [&](uint64_t n, unsigned bytes = 4) {
    if (bytes == 4 && n > UINT32_MAX)
      throw std::runtime_error("image exceeds format capacity");
    for (unsigned i = 0; i < bytes; ++i)
      out.put(char(n >> (8 * i)));
  };
  auto text = [&](const Json &j) {
    auto s = j.get<std::string>();
    word(s.size());
    out.write(s.data(), s.size());
  };
  uint64_t na = 0, ni = 0;
  for (const auto &op : ops) {
    na += op.args.size();
    ni += op.imm.size();
  }
  out.write("RHDMSIM\0", 8);
  word(std::any_of(ops.begin(),ops.end(),[](const Op &o){return o.code==33;}) ? 4 :
       std::any_of(ops.begin(),ops.end(),[](const Op &o){return o.code==32;}) ? 3 : 2);
  word(widths.size());
  word(ops.size());
  word(na);
  word(ni);
  for (auto name : {"registers", "memories", "writes", "reads", "assertions",
                    "ports", "objects"})
    word(metadata.at(name).size());
  for (auto w : widths)
    word(w);
  na = ni = 0;
  for (const auto &op : ops) {
    word(op.code);
    word(op.out);
    word(na);
    word(op.args.size());
    word(ni);
    word(op.imm.size());
    na += op.args.size();
    ni += op.imm.size();
  }
  for (const auto &op : ops)
    for (auto a : op.args)
      word(a);
  for (const auto &op : ops)
    for (auto a : op.imm)
      word(a, 8);
  for (auto name : {"registers", "memories", "writes", "reads"})
    for (const auto &r : metadata.at(name))
      for (const auto &a : r)
        word(natural(a));
  for (const auto &r : metadata.at("assertions")) {
    for (size_t i = 0; i < 3; ++i)
      word(natural(r[i]));
    text(r[3]);
  }
  for (const auto &r : metadata.at("ports")) {
    word(natural(r[0]));
    word(natural(r[1]));
    text(r[2]);
  }
  for (const auto &o : metadata.at("objects")) {
    for (size_t i = 0; i < 4; ++i)
      word(natural(o[i]));
    word(o[4].size());
    for (const auto &a : o[4])
      word(natural(a));
    text(o[5]);
  }
  out.close();
  if (!out)
    throw std::runtime_error("failed writing " + path);
}
} // namespace rds
