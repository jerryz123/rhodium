// Lowers demanded typed leaves and preserves structural provenance without adding execution roots.
#include "model.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <tuple>
#include <stdexcept>
namespace rds {
namespace {
uint32_t nat(const Json &j) {
  if (!j.is_number_integer() || j < 0 || j > UINT32_MAX)
    throw std::runtime_error("invalid semantic integer");
  return j.get<uint32_t>();
}
void require(bool p, const char *message) { if (!p) throw std::runtime_error(message); }
std::vector<uint32_t> type_widths(const Json &types) {
  require(types.is_array(), "invalid semantic types");
  std::vector<uint32_t> tw;
  for (const auto &t: types) {
    require(t.is_array() && t.size()>=2 && t[0].is_string(), "invalid semantic type");
    uint64_t w=0;
    if(t[0]=="bits") { require(t.size()==2,"invalid bits type");w=nat(t[1]); }
    else if(t[0]=="vector") {
      require(t.size()==3 && nat(t[2])<tw.size(),"invalid vector element type");
      w=uint64_t(nat(t[1]))*tw[nat(t[2])];
    } else if(t[0]=="record") {
      require(t.size()==2 && t[1].is_array(),"invalid record type");std::set<std::string> names;
      for(const auto &f:t[1]) {
        require(f.is_array() && f.size()==2 && f[0].is_string() && nat(f[1])<tw.size(),"invalid record field");
        require(names.insert(f[0].get<std::string>()).second,"duplicate record field");w+=tw[nat(f[1])];
      }
    } else throw std::runtime_error("unknown semantic type");
    require(w>0 && w<=UINT32_MAX,"invalid semantic type width");tw.push_back(w);
  }
  return tw;
}
}
Json lower_semantic(Json j, bool optimize) {
  if (j.at("format") != "rhodium-simulation-ir-v2") return j;
  const auto raw = j.at("operations");
  const auto widths = j.at("values").get<std::vector<uint32_t>>();
  const auto types = j.at("types");
  const auto vtypes = j.at("value_types").get<std::vector<uint32_t>>();
  require(vtypes.size()==widths.size(), "semantic value/type count mismatch");
  const auto tw = type_widths(types);
  for(size_t i=0;i<widths.size();++i)
    require(vtypes[i]<tw.size() && widths[i]==tw[vtypes[i]],"semantic type width mismatch");
  std::vector<const Json *> defs(widths.size(),nullptr);
  for(const auto &o:raw) {
    require(o.is_array() && o.size()==4 && o[2].is_array() && o[3].is_array(),"invalid semantic operation");
    auto c=nat(o[0]),v=nat(o[1]);
    require(c<=36 && v<widths.size() && !defs[v],"invalid semantic definition");defs[v]=&o;
    for(const auto &a:o[2])require(nat(a)<widths.size(),"invalid semantic operand");
    if(c>=32) {
      const auto &t=types[vtypes[v]];
      if(c==32 || c==34) {
        require(t[0]==(c==32?"record":"vector"),"construction result type mismatch");
        size_t n=c==32?t[1].size():nat(t[1]);require(o[2].size()==n && o[3].empty(),"construction arity mismatch");
        for(size_t i=0;i<n;++i)require(vtypes[nat(o[2][i])]==(c==32?nat(t[1][i][1]):nat(t[2])),"construction operand type mismatch");
      } else {
        require(o[2].size()==1,"projection/reinterpret arity mismatch");
        auto a=nat(o[2][0]);const auto &at=types[vtypes[a]];
        if(c==36)require(widths[v]==widths[a] && o[3].empty(),"reinterpret width mismatch");
        else {
          require(o[3].size()==1 && at[0]==(c==33?"record":"vector"),"projection source type mismatch");
          auto ix=nat(o[3][0]);require(ix<(c==33?at[1].size():nat(at[1])),"projection index out of range");
          require(vtypes[v]==(c==33?nat(at[1][ix][1]):nat(at[2])),"projection result type mismatch");
        }
      }
    }
  }
  const auto groups = j.value("conditional_groups", Json::array());
  require(groups.is_array(), "invalid source conditional groups");
  std::set<Id> interesting;
  for (Id v=0;v<widths.size();++v)
    if (types[vtypes[v]][0]!="bits") interesting.insert(v);
  for (const auto &g:groups) {
    require(g.is_array() && (g.size()==4 || g.size()==5) &&
                g[3].is_array() && (g.size()==4 || g[4].is_string()),
            "invalid source conditional group");
    auto occurrence=nat(g[0]), result=nat(g[1]), selector=nat(g[2]);
    require(j.contains("occurrences") && occurrence<j["occurrences"].size() &&
                result<widths.size() && selector<widths.size(),
            "invalid source conditional mapping");
    const auto *op=defs[result];
    require(op && (*op)[0]==15 && (*op)[2].size()>=2 &&
                (*op)[2][0]==selector && (*op)[3]==g[3],
            "source conditional disagrees with definition");
    interesting.insert(result);interesting.insert(selector);
  }
  Model out;out.metadata=j;out.metadata["format"]="rhodium-simulation-ir-v1";
  auto names=j.at("opcodes");
  require(names.size()==37 && names[32]=="record_create" && names[33]=="record_get" && names[34]=="vector_create" && names[35]=="vector_get" && names[36]=="reinterpret","invalid semantic opcode contract");
  while(names.size()>32)names.erase(names.size()-1);
  out.metadata["opcodes"]=names;
  out.metadata.erase("types");out.metadata.erase("value_types");out.metadata.erase("conditional_groups");
  using Key=std::tuple<Id,uint32_t,uint32_t>;
  std::map<Key,Id> cache;std::set<Key> active;
  std::map<std::tuple<uint32_t,uint32_t,std::vector<Id>,std::vector<uint64_t>>,Id> cse;
  std::vector<Id> sources(widths.size(),none);
  auto emit=[&](uint32_t code,uint32_t w,std::vector<Id> a,std::vector<uint64_t> im=std::vector<uint64_t>{}) {
    auto k=std::make_tuple(code,w,a,im);auto it=cse.find(k);
    if((optimize || code==0) && it!=cse.end())return it->second;
    Id id=out.widths.size();out.widths.push_back(w);out.ops.push_back({code,id,a,im});cse[k]=id;return id;
  };
  // Preserve only declared sources. An undefined operand is never silently accepted.
  auto source=[&](Id v) {
    require(v<widths.size() && !defs[v],"semantic source has a definition");
    if(sources[v]==none){sources[v]=out.widths.size();out.widths.push_back(widths[v]);}
  };
  for(const auto &p:j["ports"])if(p[0]!=1)source(nat(p[1]));
  for(const auto &r:j["registers"])source(nat(r[0]));
  for(const auto &r:j["reads"])source(nat(r[3]));
  uint64_t projections=0, selections=0;
  std::function<Id(Id,uint32_t,uint32_t)> get;
  get=[&](Id v,uint32_t low,uint32_t w)->Id {
    require(v<widths.size() && w && uint64_t(low)+w<=widths[v],"invalid demanded range");
    Key key{v,low,w};auto it=cache.find(key);if(it!=cache.end())return it->second;
    require(active.insert(key).second,"semantic combinational leaf cycle");
    auto whole=[&](Id a){return get(a,0,widths[a]);};
    Id result=none;
    if(sources[v]!=none)result=low==0&&w==widths[v]?sources[v]:emit(17,w,{sources[v]},{low});
    else {
      require(defs[v]!=nullptr,"undefined semantic value");const auto &o=*defs[v];auto code=nat(o[0]);
      auto a=o[2].get<std::vector<Id>>();auto im=o[3].get<std::vector<uint64_t>>();
      if(code==1 || code==36) {require(a.size()==1,"invalid alias");result=get(a[0],low,w);++projections;}
      else if(code==17) {require(a.size()==1&&im.size()==1,"invalid slice");result=get(a[0],low+im[0],w);++projections;}
      else if(code==33 || code==35) {
        const auto &t=types[vtypes[a[0]]];uint64_t offset=0;
        if(code==33)for(size_t i=im[0]+1;i<t[1].size();++i)offset+=tw[nat(t[1][i][1])];
        else offset=im[0]*tw[nat(t[2])];
        result=get(a[0],low+offset,w);++projections;
      } else if(code==20 || code==32 || code==34) {
        if(code==32)std::reverse(a.begin(),a.end());
        uint64_t offset=0;std::vector<Id> pieces;
        for(Id x:a){auto begin=std::max<uint64_t>(low,offset),end=std::min<uint64_t>(uint64_t(low)+w,offset+widths[x]);
          if(begin<end)pieces.push_back(get(x,begin-offset,end-begin));
          offset+=widths[x];}
        require(offset==widths[v]&&!pieces.empty(),"invalid aggregate packing");
        result=pieces.size()==1?pieces[0]:emit(20,w,pieces);++projections;
      } else if(code==15 || code==16) {
        require(a.size()>=2,"invalid selection");std::vector<Id> b{whole(a[0])};
        // Flat selectors are one dependency leaf; share their full result.
        if(types[vtypes[v]][0]=="bits" && (low || w!=widths[v]))result=emit(17,w,{whole(v)},{low});
        else {for(size_t i=1;i<a.size();++i)b.push_back(get(a[i],low,w));result=emit(code,w,b,im);++selections;}
      } else if(code==23 && (low || w!=widths[v])) {
        require(im.size()==4&&a.size()==4,"invalid vector write");
        uint32_t ew=im[1],ports=im[2],iw=im[3];std::vector<Id> pieces;
        for(uint32_t e=low/ew;e<=(low+w-1)/ew;++e){uint32_t begin=std::max(low,e*ew),end=std::min(low+w,(e+1)*ew),pw=end-begin;
          Id selected=get(a[0],begin,pw);
          for(uint32_t p=0;p<ports;++p){Id en=get(a[1],p,1),index=get(a[2],p*iw,iw),k=emit(0,iw,{},words(e,iw));
            Id guard=emit(3,1,{en,emit(12,1,{index,k})});
            selected=emit(15,pw,{guard,selected,get(a[3],p*ew+begin-e*ew,pw)},{1});}
          pieces.push_back(selected);}
        result=pieces.size()==1?pieces[0]:emit(20,w,pieces);
      } else if(low || w!=widths[v])result=emit(17,w,{whole(v)},{low});
      else {for(auto &x:a)x=whole(x);result=emit(code,w,a,im);}
    }
    active.erase(key);cache[key]=result;return result;
  };
  auto mapped=[&](const Json &v)->Id{auto id=nat(v);return id==none?none:get(id,0,widths.at(id));};
  for(auto &p:out.metadata["ports"])p[1]=mapped(p[1]);
  for(auto &r:out.metadata["registers"])for(auto &a:r)a=mapped(a);
  for(auto name:{"writes","reads"})for(auto &r:out.metadata[name])for(size_t i=1;i<5;++i)r[i]=mapped(r[i]);
  for(auto &r:out.metadata["assertions"])for(size_t i=0;i<3;++i)r[i]=mapped(r[i]);
  for(auto &o:out.metadata["objects"])for(auto &a:o[4])a=mapped(a);
  if(out.metadata.contains("contracts")){
    require(out.metadata["contracts"].is_array(),"invalid contracts section");
    for(auto &c:out.metadata["contracts"]){
      require(c.is_array()&&c.size()==4&&c[1].is_string()&&c[2].is_array()&&c[3].is_array(),"invalid semantic contract");
      for(auto &b:c[2]){
        require(b.is_array()&&b.size()==2&&b[0].is_string(),"invalid contract binding");
        b[1]=mapped(b[1]);
      }
    }
  }
  // Keep the original partial-operation validation roots in debug lowering.
  for(const auto &o:raw)if(o[0]==16 || (o[0]>=21&&o[0]<=24)){Id v=nat(o[1]);get(v,0,widths[v]);}
  Json origins=Json::array();
  for(const auto &o:j["origins"]){Id v=nat(o[0]);auto begin=cache.lower_bound(Key{v,0,0});
    for(auto it=begin;it!=cache.end()&&std::get<0>(it->first)==v;++it){auto n=o;n[0]=it->second;n[5]=std::get<1>(it->first);n[6]=std::get<2>(it->first);origins.push_back(n);}}
  out.metadata["origins"]=origins;
  // Observe only materialized demanded ranges. Looking up another whole value
  // here would accidentally retain dead fields or close legal aggregate cycles.
  Json ranges=Json::array(), conditionals=Json::array();
  for (const auto &[key, value]:cache) {
    auto [source_id,low,width]=key;
    if (interesting.count(source_id))
      ranges.push_back({source_id,low,width,value});
  }
  for (const auto &g:groups) {
    Id source_result=nat(g[1]), source_selector=nat(g[2]);
    auto selector=cache.find(Key{source_selector,0,widths[source_selector]});
    if (selector==cache.end()) continue;
    const auto &op=*defs[source_result];
    for (auto it=cache.lower_bound(Key{source_result,0,0});
         it!=cache.end() && std::get<0>(it->first)==source_result;++it) {
      auto [unused,low,width]=it->first;(void)unused;
      Json arms=Json::array();bool complete=true;
      for (size_t a=1;a<op[2].size();++a) {
        auto arm=cache.find(Key{nat(op[2][a]),low,width});
        if (arm==cache.end()) { complete=false;break; }
        arms.push_back(arm->second);
      }
      if (complete)
        conditionals.push_back({g[0],source_result,low,width,it->second,
                               selector->second,arms,g[3],g.size()==5?g[4]:Json("")});
    }
  }
  out.metadata["semantic_structure"]={{"version",1},{"types",types},
      {"source_types",vtypes},{"ranges",ranges},{"conditionals",conditionals}};
  out.metadata["semantic_summary"]={{"input_operations",raw.size()},{"input_types",types.size()},
    {"lowered_operations",out.ops.size()},{"projection_visits",projections},{"selection_visits",selections},
    {"conditional_groups",j.value("conditional_groups",Json::array()).size()},{"cse",optimize}};
  return out.json();
}
void validate_semantic_structure(const Model &m) {
  if (!m.metadata.contains("semantic_structure")) return;
  const auto &s=m.metadata["semantic_structure"];
  require(s.is_object() && s.at("version")==1 &&
              s.at("source_types").is_array() && s.at("ranges").is_array() &&
              s.at("conditionals").is_array(), "invalid semantic structure");
  const auto tw=type_widths(s.at("types"));
  std::vector<uint32_t> sw;
  for (const auto &t:s["source_types"]) {
    auto id=nat(t);require(id<tw.size(),"invalid source type mapping");sw.push_back(tw[id]);
  }
  auto current=[&](const Json &j) {
    auto id=nat(j);require(id<m.widths.size(),"invalid mapped semantic value");return id;
  };
  auto range=[&](const Json &source,const Json &low,const Json &width) {
    auto id=nat(source), lo=nat(low), w=nat(width);
    require(id<sw.size() && w && uint64_t(lo)+w<=sw[id],"invalid mapped semantic range");
  };
  std::set<std::tuple<Id,uint32_t,uint32_t>> unique;
  std::set<std::tuple<Id,uint32_t,uint32_t,Id>> ranges;
  for (const auto &r:s["ranges"]) {
    require(r.is_array() && r.size()==4,"invalid semantic range row");
    range(r[0],r[1],r[2]);Id value=current(r[3]);
    require(m.widths[value]==nat(r[2]),"mapped semantic range width mismatch");
    require(unique.emplace(nat(r[0]),nat(r[1]),nat(r[2])).second,"duplicate semantic range");
    ranges.emplace(nat(r[0]),nat(r[1]),nat(r[2]),value);
  }
  for (const auto &g:s["conditionals"]) {
    require(g.is_array() && g.size()==9 && g[6].is_array() && g[7].is_array() &&
                g[8].is_string(),"invalid mapped conditional row");
    require(m.metadata.contains("occurrences") && nat(g[0])<m.metadata["occurrences"].size(),
            "invalid mapped conditional occurrence");
    range(g[1],g[2],g[3]);Id result=current(g[4]), selector=current(g[5]);
    require(m.widths[result]==nat(g[3]) &&
                ranges.count({nat(g[1]),nat(g[2]),nat(g[3]),result}),
            "conditional result lacks demanded range");
    require(!g[6].empty() &&
                g[7].size()==(g[6].size()-1)*((uint64_t(m.widths[selector])+63)/64),
            "invalid conditional arm/key count");
    for (const auto &a:g[6])
      require(m.widths[current(a)]==m.widths[result],"conditional arm width mismatch");
    for (const auto &k:g[7])
      require(k.is_number_unsigned() || (k.is_number_integer() && k.get<int64_t>()>=0),
              "invalid conditional key");
  }
}
void remap_semantic_structure(Model &m, const std::function<Id(Id)> &mapped) {
  if (!m.metadata.contains("semantic_structure")) return;
  auto &s=m.metadata["semantic_structure"];
  Json ranges=Json::array(), groups=Json::array();
  for (auto row:s["ranges"]) {
    Id value=mapped(nat(row[3]));
    if (value!=none) { row[3]=value;ranges.push_back(std::move(row)); }
  }
  for (auto row:s["conditionals"]) {
    Id result=mapped(nat(row[4])), selector=mapped(nat(row[5]));
    if (result==none || selector==none) continue;
    bool complete=true;
    for (auto &a:row[6]) {
      Id value=mapped(nat(a));if (value==none) { complete=false;break; }a=value;
    }
    if (complete) { row[4]=result;row[5]=selector;groups.push_back(std::move(row)); }
  }
  s["ranges"]=std::move(ranges);s["conditionals"]=std::move(groups);
}
} // namespace rds
