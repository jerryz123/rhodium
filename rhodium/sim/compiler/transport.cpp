// Exchanges payload slots along proved queue matchings while retaining every observed control field.
#include "model.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace rds { namespace {
using B = uint32_t;
struct BoolGraph {
  struct Node {unsigned var;B low,high;};
  struct Atom {unsigned kind;Id value;unsigned bit;};
  const Model&m;std::vector<const Op*>defs;
  std::vector<Node> nodes{{UINT32_MAX,0,0},{UINT32_MAX,1,1}};
  std::vector<Atom> atoms;
  std::map<std::tuple<unsigned,B,B>,B> unique;
  std::map<std::tuple<unsigned,Id,unsigned>,B> variables;
  std::map<std::pair<B,B>,B> conjunction;
  std::map<B,B> negatives;
  std::map<std::pair<Id,unsigned>,B> bits;
  struct MatcherChoice {Id owner;unsigned query,rows;std::vector<Id>args;Id value=none;};
  std::vector<MatcherChoice> matchers;
  std::map<std::tuple<Id,unsigned,std::vector<Id>>,Id> matcher_ids;
  std::map<std::pair<Id,unsigned>,B> grants;
  Id empty_owner=none;
  explicit BoolGraph(const Model&model):m(model),defs(m.widths.size()) {for(const auto&o:m.ops)defs[o.out]=&o;}
  B node(unsigned v,B l,B h){if(l==h)return l;auto key=std::make_tuple(v,l,h);auto i=unique.find(key);if(i!=unique.end())return i->second;
    if(nodes.size()>=65536)throw std::runtime_error("Boolean proof budget");
    B id=nodes.size();nodes.push_back({v,l,h});unique[key]=id;return id;}
  B atom(unsigned kind,Id v,unsigned b){auto key=std::make_tuple(kind,v,b);auto i=variables.find(key);if(i!=variables.end())return i->second;
    unsigned var=atoms.size();atoms.push_back({kind,v,b});return variables[key]=node(var,0,1);}
  B neg(B a){if(a<2)return a^1;auto i=negatives.find(a);if(i!=negatives.end())return i->second;auto n=nodes[a];return negatives[a]=node(n.var,neg(n.low),neg(n.high));}
  B land(B a,B b){if(!a||!b)return 0;if(a==1)return b;if(b==1||a==b)return a;if(a>b)std::swap(a,b);auto key=std::make_pair(a,b);auto i=conjunction.find(key);if(i!=conjunction.end())return i->second;
    auto x=nodes[a],y=nodes[b];unsigned v=std::min(x.var,y.var);B lo=land(x.var==v?x.low:a,y.var==v?y.low:b),hi=land(x.var==v?x.high:a,y.var==v?y.high:b);return conjunction[key]=node(v,lo,hi);}
  B lor(B a,B b){return neg(land(neg(a),neg(b)));}
  B ite(B s,B f,B t){return lor(land(neg(s),f),land(s,t));}
  template<class F> bool temporary(F work){
    size_t node_mark=nodes.size(),atom_mark=atoms.size(),matcher_mark=matchers.size();
    auto restore=[&](){
      auto trim=[](auto&table,auto invalid){for(auto i=table.begin();i!=table.end();)if(invalid(*i))i=table.erase(i);else ++i;};
      trim(unique,[&](const auto&e){return e.second>=node_mark;});
      trim(variables,[&](const auto&e){return e.second>=node_mark;});
      trim(bits,[&](const auto&e){return e.second>=node_mark;});
      trim(grants,[&](const auto&e){return e.second>=node_mark;});
      trim(negatives,[&](const auto&e){return e.first>=node_mark||e.second>=node_mark;});
      trim(conjunction,[&](const auto&e){return e.first.first>=node_mark||e.first.second>=node_mark||e.second>=node_mark;});
      trim(matcher_ids,[&](const auto&e){return e.second>=matcher_mark;});
      nodes.resize(node_mark);atoms.resize(atom_mark);matchers.resize(matcher_mark);
    };
    try{bool result=work();restore();return result;}catch(...){restore();throw;}
  }
  bool implies(B a,B b){return temporary([&](){return prove_implication(a,b);});}
  bool prove_implication(B a,B b){
    B remaining=land(a,neg(b));
    // Refine only a counterexample's selected grants. Eager expansion of all
    // columns recreates an exponential matching circuit inside the proof.
    for(unsigned attempt=0;remaining&&attempt<256;++attempt){
      std::vector<bool>witness(atoms.size());B cursor=remaining;
      while(cursor>1){auto n=nodes[cursor];bool high=n.low==0;witness[n.var]=high;cursor=high?n.high:n.low;}
      auto evaluate=[&](B x){while(x>1){auto n=nodes[x];x=n.var<witness.size()&&witness[n.var]?n.high:n.low;}return x!=0;};
      bool refined=false;
      for(Id id=0;id<matchers.size();++id){unsigned choice=0,width=1;while((1u<<width)<=matchers[id].rows)++width;
        for(unsigned k=0;k<width;++k){auto v=variables.find(std::make_tuple(3u,id,k));if(v!=variables.end()&&evaluate(v->second))choice|=1u<<k;}
        if(!choice||choice>matchers[id].rows)continue;
        B fact=lor(neg(grant(id,choice-1,96)),requirement(id,choice-1,96));
        if(!evaluate(fact)){remaining=land(remaining,fact);refined=true;break;}
      }
      if(!refined)return false;
    }
    if(remaining)throw std::runtime_error("matcher refinement budget");
    return true;
  }
  B full(Id owner){return owner==empty_owner?0:atom(1,owner,0);}
  Id matcher(Id owner,unsigned query,std::vector<Id>args){
    for(Id&a:args)while(defs[a]&&defs[a]->code==1)a=defs[a]->args[0];
    auto key=std::make_tuple(owner,query,args);auto found=matcher_ids.find(key);if(found!=matcher_ids.end())return found->second;
    Id id=matchers.size();matchers.push_back({owner,query,m.metadata["objects"][owner][1].get<unsigned>(),std::move(args),none});matcher_ids[key]=id;return id;
  }
  B grant(Id id,unsigned row,unsigned fuel){
    if(!fuel)throw std::runtime_error("matcher proof depth budget");
    auto key=std::make_pair(id,row);auto cached=grants.find(key);if(cached!=grants.end())return cached->second;
    auto d=matchers[id];
    // Zero or one row is chosen. Request/taken implications are lazy facts;
    // allowing empty grants remains a conservative ownership approximation.
    unsigned width=1;while((1u<<width)<=d.rows)++width;B selected=1;
    for(unsigned k=0;k<width;++k){B x=atom(3,id,k);selected=land(selected,((row+1)>>k)&1?x:neg(x));}
    return grants[key]=selected;
  }
  B requirement(Id id,unsigned row,unsigned fuel){
    if(!fuel)throw std::runtime_error("matcher requirement depth budget");
    auto d=matchers[id];const auto&object=m.metadata["objects"][d.owner];unsigned columns=object[2];bool packed=object[3].get<unsigned>()&32;
    bool step=d.query>=columns;unsigned col=step?d.query-columns:d.query;
    B request=packed?bit(d.args[step?1:col],row,fuel-1):bit(d.args[step?1+row:col*d.rows+row],0,fuel-1);
    B taken=step?bit(d.args[0],row,fuel-1):0;
    if(!step)for(unsigned prior=0;prior<col;++prior){std::vector<Id>prefix(d.args.begin(),d.args.begin()+(packed?prior+1:(prior+1)*d.rows));taken=lor(taken,grant(matcher(d.owner,prior,std::move(prefix)),row,fuel-1));}
    return land(request,neg(taken));
  }
  std::vector<B> choices(const Op&o,unsigned fuel=96){
    if(!fuel)throw std::runtime_error("Boolean selector depth budget");
    std::vector<B> c(o.args.size()-1);Id select=o.args[0];unsigned width=m.widths[select];if(width>64)throw std::runtime_error("wide transport selector");
    if(o.code==15){B taken=0;for(unsigned a=2;a<o.args.size();++a){B eq=1;uint64_t key=o.imm.at(a-2);for(unsigned k=0;k<width;++k){B x=bit(select,k,fuel-1);eq=land(eq,(key>>k)&1?x:neg(x));}c[a-1]=land(eq,neg(taken));taken=lor(taken,eq);}c[0]=neg(taken);
    }else{for(unsigned a=0;a<c.size();++a){B g=bit(select,a,fuel-1);for(unsigned k=0;k<width;++k)if(k!=a)g=land(g,neg(bit(select,k,fuel-1)));c[a]=g;}}
    return c;
  }
  B bit(Id v,unsigned b=0,unsigned fuel=96){if(!fuel)throw std::runtime_error("Boolean depth budget");if(v==none||b>=m.widths[v])throw std::runtime_error("invalid Boolean projection");auto key=std::make_pair(v,b);auto cached=bits.find(key);if(cached!=bits.end())return cached->second;
    const auto*o=defs[v];B result;auto arg=[&](unsigned a,unsigned k){return bit(o->args[a],k,fuel-1);};
    if(!o)result=atom(0,v,b);
    else {switch(o->code){
      case 0:result=(o->imm[b/64]>>(b%64))&1;break;
      case 1:result=arg(0,b);break;
      case 2:result=neg(arg(0,b));break;
      case 3:{result=arg(0,b);if(result)result=land(result,arg(1,b));break;}
      case 4:{result=arg(0,b);if(result!=1)result=lor(result,arg(1,b));break;}
      case 5:result=ite(arg(0,b),arg(1,b),neg(arg(1,b)));break;
      case 26:result=land(lor(arg(0,b),arg(1,b)),neg(arg(2,b)));break;
      case 17:result=arg(0,b+o->imm[0]);break;
      case 18:case 19:result=b<m.widths[o->args[0]]?arg(0,b):o->code==18?0:arg(0,m.widths[o->args[0]]-1);break;
      case 20:{unsigned pos=0;result=0;for(Id a:o->args){if(b-pos<m.widths[a]){result=bit(a,b-pos,fuel-1);break;}pos+=m.widths[a];}break;}
      case 12:{unsigned w=m.widths[o->args[0]];if(w>64){result=atom(0,v,b);break;}result=1;for(unsigned k=0;k<w;++k){B a=arg(0,k),c=arg(1,k);result=land(result,neg(ite(a,c,neg(c))));}break;}
      case 15:case 16:{auto c=choices(*o,fuel-1);result=0;for(unsigned a=1;a<o->args.size();++a)if(c[a-1])result=lor(result,land(c[a-1],arg(a,b)));break;}
      case 29:{const auto&object=m.metadata["objects"][o->imm[0]];auto q=o->imm[1];if(object[0]==1&&object[2]==1&&object[3]==0&&q<3)result=q==1?neg(full(o->imm[0])):full(o->imm[0]);else if(q==3&&object[0]==1)result=atom(2,o->imm[0],b);else if(object[0]==10){Id id=matcher(o->imm[0],q,o->args);matchers[id].value=v;result=grant(id,b,fuel-1);}else result=atom(0,v,b);break;}
      default:result=atom(0,v,b);break;
    }}return bits[key]=result;
  }
};
bool forwarding(const Op&o){return o.code==1||o.code==15||o.code==16;}
void fuse_transport_controls(Model&m,const std::vector<std::pair<std::vector<Id>,Id>>&regions){
  std::set<Id>removed;Json details=Json::array();std::vector<Op>added;
  auto emit=[&](unsigned code,unsigned width,std::vector<Id>args,std::vector<uint64_t>imm=std::vector<uint64_t>{}){Id id=m.widths.size();m.widths.push_back(width);added.push_back({code,id,std::move(args),std::move(imm)});return id;};
  for(const auto&[group,handles]:regions){
    unsigned count=group.size(),header=m.metadata["objects"][group[0]][1],handle_width=m.widths[handles];
    unsigned total=handle_width+count*(header+1);if(total>64)continue;
    Json handle_register;Json kept=Json::array();
    for(const auto&r:m.metadata["registers"])if(r[0]==handles)handle_register=r;else kept.push_back(r);
    if(handle_register.is_null())throw std::runtime_error("missing packed transport register");
    Id state=m.widths.size();m.widths.push_back(total);Id reset=handle_register[2];uint64_t initial=0;bool found=false;
    for(const auto&o:m.ops)if(o.out==handle_register[3]){if(o.code!=0)throw std::runtime_error("nonliteral transport initializer");initial=o.imm.at(0);found=true;}
    if(!found)throw std::runtime_error("missing transport initializer");
    added.push_back({17,handles,{state},{0}});
    std::map<Id,Id>valids,headers;std::vector<Id>pushes,pops,next_header;
    for(unsigned pos=0;pos<count;++pos){Id owner=group[pos];const auto&o=m.metadata["objects"][owner];removed.insert(owner);
      Id valid=emit(17,1,{state},{handle_width+pos}),data=emit(17,header,{state},{handle_width+count+pos*header});valids[owner]=valid;headers[owner]=data;
      Id empty=emit(2,1,{valid}),enqueue=emit(3,1,{empty,o[4][1].get<Id>()});
      pushes.push_back(o[4][1]);pops.push_back(o[4][3]);next_header.push_back(emit(15,header,{enqueue,data,o[4][2].get<Id>()},{1}));
    }
    for(auto&o:m.ops)if(o.code==29&&valids.count(o.imm[0])){Id owner=o.imm[0];unsigned query=o.imm[1];
      o={query==1?2u:1u,o.out,{query==3?headers.at(owner):valids.at(owner)},{}};}
    Id live=emit(17,count,{state},{handle_width}),push=emit(20,count,pushes),pop=emit(20,count,pops);
    Id packed_valid=emit(4,count,{emit(3,count,{live,emit(2,count,{pop})}),emit(3,count,{emit(2,count,{live}),push})});
    Id zero=emit(0,count,{}, {0});
    Id reset_valid=emit(15,count,{reset,packed_valid,zero},{1});
    Id reset_handles=emit(15,handle_width,{reset,handle_register[1].get<Id>(),handle_register[3].get<Id>()},{1});
    // Header previews survive reset, including accepted reset-cycle writes.
    // Reset is therefore applied to ownership/validity fields before packing.
    Id next=emit(20,total,{reset_handles,reset_valid,emit(20,count*header,next_header)});
    kept.push_back({state,next,none,emit(0,total,{}, {initial})});m.metadata["registers"]=std::move(kept);
    details.push_back({{"objects",group},{"state_bits",total},{"handle_bits",handle_width},{"valid_bits",count},{"header_bits",count*header}});
  }
  m.ops.insert(m.ops.end(),added.begin(),added.end());
  Json objects=Json::array();std::vector<Id>remap(m.metadata["objects"].size(),none);
  for(Id owner=0;owner<remap.size();++owner)if(!removed.count(owner)){remap[owner]=objects.size();objects.push_back(m.metadata["objects"][owner]);}
  m.metadata["objects"]=std::move(objects);for(auto&o:m.ops)if(o.code==29)o.imm[0]=remap.at(o.imm[0]);
  auto ops=std::move(m.ops);std::vector<Id>producer(m.widths.size(),none);for(Id i=0;i<ops.size();++i)producer[ops[i].out]=i;
  std::vector<unsigned char>seen(ops.size());m.ops.clear();std::function<void(Id)>visit=[&](Id i){if(i==none||seen[i]==2)return;if(seen[i]==1)throw std::runtime_error("fused transport state cycle");seen[i]=1;for(Id a:ops[i].args)visit(producer[a]);seen[i]=2;m.ops.push_back(std::move(ops[i]));};for(Id i=0;i<ops.size();++i)visit(i);
  m.metadata["transport_state_summary"]={{"regions",details}};
}
}
void exchange_payload_handles(Model&m,bool pack_handles,bool fuse_state,bool isolate_readers,bool bitmap_slots){
  if(bitmap_slots&&fuse_state)throw std::runtime_error("bitmap payload slots and transport-state fusion are independent layouts");
  const Model original=m;const auto&objects=original.metadata["objects"];
  std::vector<const Op*>defs(original.widths.size());for(const auto&o:original.ops)defs[o.out]=&o;
  auto canonical=[&](Id v){while(v!=none&&defs[v]&&defs[v]->code==1)v=defs[v]->args[0];return v;};
  auto eligible=[&](Id i){const auto&o=objects[i];return o[0]==1&&o[2]==1&&o[3]==0&&o[1].get<unsigned>()>64;};
  std::vector<std::set<Id>> links(objects.size());
  std::function<void(Id,Id,std::set<Id>&)> connect=[&](Id dst,Id v,std::set<Id>&seen){if(!seen.insert(v).second||!defs[v])return;const auto&o=*defs[v];
    if(o.code==29&&o.imm[1]==3&&eligible(o.imm[0])&&objects[o.imm[0]][1]==objects[dst][1]&&canonical(objects[o.imm[0]][4][0])==canonical(objects[dst][4][0])){links[dst].insert(o.imm[0]);links[o.imm[0]].insert(dst);}
    if(forwarding(o))for(unsigned a=o.code==1?0:1;a<o.args.size();++a)connect(dst,o.args[a],seen);
  };
  for(Id i=0;i<objects.size();++i)if(eligible(i)){std::set<Id>seen;connect(i,objects[i][4][2],seen);}
  std::vector<std::vector<Id>>groups;std::set<Id>assigned;
  for(Id i=0;i<objects.size();++i)if(!links[i].empty()&&!assigned.count(i)){std::vector<Id>g{i};assigned.insert(i);for(size_t j=0;j<g.size();++j)for(Id n:links[g[j]])if(assigned.insert(n).second)g.push_back(n);if(g.size()>1)groups.push_back(g);}
  // Keep packed lanes in occurrence order rather than graph-discovery order.
  // This preserves contiguous status/header views across sibling queues.
  if(pack_handles||bitmap_slots)for(auto&group:groups)std::sort(group.begin(),group.end());
  Json accepted=Json::array(),rejected=Json::array(),cuts=Json::array();std::vector<Op>added;std::map<Id,Op>replaced;
  std::vector<std::pair<std::vector<Id>,Id>>fused_regions;
  auto emit=[&](unsigned code,unsigned w,std::vector<Id>a,std::vector<uint64_t>im=std::vector<uint64_t>{}){Id id=m.widths.size();m.widths.push_back(w);added.push_back({code,id,std::move(a),std::move(im)});return id;};
  for(size_t group_index=0;group_index<groups.size();++group_index){const auto group=groups[group_index];
    Model checkpoint=m;size_t added_before=added.size();auto replaced_before=replaced;
    std::string stage="source discovery";Id proof_value=none;Json retained_reads=Json::array();
    try{
      if(group.size()>128)throw std::runtime_error("transport region exceeds 128 queues");
      std::set<Id>members(group.begin(),group.end());unsigned width=objects[group[0]][1];BoolGraph b(original);
      using Sources=std::map<Id,B>;std::map<Id,Sources>memo;
      std::function<const Sources&(BoolGraph&,std::map<Id,Sources>&,Id)>source_graph;
      source_graph=[&](BoolGraph&graph,std::map<Id,Sources>&cache,Id v)->const Sources&{auto i=cache.find(v);if(i!=cache.end())return i->second;Sources s;const auto*o=defs[v];
        if(o&&o->code==29&&o->imm[1]==3&&members.count(o->imm[0]))s[o->imm[0]]=1;
        else if(o&&o->code==1)s=source_graph(graph,cache,o->args[0]);
        else if(o&&(o->code==15||o->code==16)){bool present=false;for(unsigned a=1;a<o->args.size();++a)present|=!source_graph(graph,cache,o->args[a]).empty();
          if(present){proof_value=v;auto c=graph.choices(*o);for(unsigned a=1;a<o->args.size();++a)for(auto [owner,g]:source_graph(graph,cache,o->args[a]))s[owner]=graph.lor(s[owner],graph.land(g,c[a-1]));}}
        return cache.emplace(v,std::move(s)).first->second;
      };
      auto sources=[&](Id v)->const Sources&{return source_graph(b,memo,v);};
      std::set<Id>tracked;for(const auto&o:original.ops)if(original.widths[o.out]==width&&!sources(o.out).empty())tracked.insert(o.out);
      for(Id v:tracked)if(replaced.count(v))throw std::runtime_error("overlapping transport region");
      stage="accepted transfers";
      struct Edge{Id src,dst;B guard;};std::vector<Edge>edges;std::map<Id,B>enqueue;std::set<Id>sending;
      for(Id dst:group){proof_value=objects[dst][4][1];const auto&incoming_sources=sources(objects[dst][4][2]);
        // A fresh ingress enable is a boundary signal, not an ownership proof
        // over the complete producer datapath. Its actual value is still emitted.
        B valid=incoming_sources.empty()?b.atom(0,proof_value,0):b.bit(proof_value);
        B en=b.land(b.neg(b.full(dst)),valid);enqueue[dst]=en;
        for(auto [src,g]:sources(objects[dst][4][2])){B transfer=b.land(en,g);if(!transfer||b.implies(transfer,0))continue;
          if(src==dst||!b.implies(transfer,b.land(b.full(src),b.bit(objects[src][4][3]))))throw std::runtime_error("accepted transfer is not a source dequeue");
          edges.push_back({src,dst,transfer});sending.insert(src);
        }
      }
      if(edges.empty())throw std::runtime_error("no live internal transfer");
      stage="source exclusivity";
      for(size_t i=0;i<edges.size();++i)for(size_t j=0;j<i;++j)if(edges[i].src==edges[j].src&&!b.implies(b.land(edges[i].guard,edges[j].guard),0))throw std::runtime_error("accepted transfer duplicates a source");
      std::set<Id>hydrate;
      std::vector<bool>retained(width);
      struct Observation{Id value;unsigned low,width;};std::vector<Observation>observations;
      unsigned fallback_proofs=0;
      std::set<Id>early_readers;
      stage="payload observations";
      // Endpoint demand must not compete with the whole network's ownership
      // proof for nodes or inherit its variable order. Rebuild only the local
      // source path and consumer demand, with the same conservative semantics.
      BoolGraph observation(original);std::map<Id,Sources>observation_sources;
      // A projection can precede its terminal validity mux. Follow demand
      // through total operations; strict checks and unknown consumers stay live.
      std::vector<std::vector<std::pair<const Op*,unsigned>>>users(original.widths.size());
      for(const auto&o:original.ops)for(unsigned a=0;a<o.args.size();++a)users[o.args[a]].push_back({&o,a});
      std::set<Id>roots;
      std::map<Id,std::vector<Id>>conditional_roots;
      auto conditional=[&](Id value,Id enable){if(value!=none){if(enable==none)roots.insert(value);else conditional_roots[value].push_back(enable);}};
      for(const auto&p:original.metadata["ports"])if(p[0]==1)roots.insert(p[1].get<Id>());
      for(const auto&r:original.metadata["registers"])for(const auto&v:r)roots.insert(v.get<Id>());
      for(const auto&r:original.metadata["reads"])for(unsigned i=1;i<5;++i)roots.insert(r[i].get<Id>());
      for(const auto&r:original.metadata["writes"]){roots.insert(r[3].get<Id>());for(unsigned i:{1u,2u,4u})conditional(r[i],r[3]);}
      for(const auto&r:original.metadata["assertions"])for(unsigned i=0;i<3;++i)roots.insert(r[i].get<Id>());
      auto fifo_payload=[&](const Json&o,unsigned a){return a==2&&o[0]==1&&(o[3].get<unsigned>()&~2u)==0;};
      for(const auto&o:objects)for(unsigned a=0;a<o[4].size();++a){Id v=o[4][a];if(fifo_payload(o,a))conditional(v,o[4][1]);else roots.insert(v);}
      std::map<Id,B>demands;
      std::function<B(Id,unsigned)>demand;
      auto nonzero=[&](Id v,bool invert=false){B result=0;for(unsigned k=0;k<original.widths[v]&&result!=1;++k){B bit=observation.bit(v,k);result=observation.lor(result,invert?observation.neg(bit):bit);}return result;};
      auto operand_demand=[&](const Op&o,unsigned arg,unsigned fuel)->B{
        if(o.code==23){
          if(arg==1)return 1; // Enables and enabled indices govern strict checks.
          if(arg==0)return demand(o.out,fuel);
          B enabled=nonzero(o.args[1]);
          return arg==2||!enabled?enabled:observation.land(enabled,demand(o.out,fuel));
        }
        if((o.code<=20&&!(o.code==16&&!arg))||o.code==25||o.code==26||o.code==27||o.code==28||o.code==30||o.code==31||
           (o.code==21&&arg==0)||(o.code==22&&arg!=1)){
          B gate=1;
          if((o.code==15||o.code==16)&&arg)gate=observation.choices(o)[arg-1];
          if((o.code==3||o.code==4)&&original.widths[o.out]<=64)gate=nonzero(o.args[arg^1],o.code==4);
          return gate?observation.land(gate,demand(o.out,fuel)):0;
        }
        return B(1);
      };
      demand=[&](Id v,unsigned fuel)->B{
        if(roots.count(v)||!fuel)return 1;
        auto found=demands.find(v);if(found!=demands.end())return found->second;
        B result=0;
        auto boundary=conditional_roots.find(v);if(boundary!=conditional_roots.end())for(Id enable:boundary->second)result=observation.lor(result,observation.bit(enable));
        for(auto [consumer,arg]:users[v]){B use=operand_demand(*consumer,arg,fuel-1);
          result=observation.lor(result,use);if(result==1)break;
        }
        return demands[v]=result;
      };
      auto observe=[&](Id v,unsigned low,unsigned size,auto guard,bool boundary=false){if(!tracked.count(v))return;proof_value=v;
        bool terminal=false;std::string reason;Id observing=none;
        try{terminal=true;for(const auto&source:sources(v)){
          Id src=source.first;
          observing=src;
          // Prove terminal data unobservable with its source empty. Cofactoring
          // before traversal lets validity gates prune unrelated consumer cones.
          observation.empty_owner=sending.count(src)?none:src;
          bool irrelevant=observation.temporary([&](){demands.clear();observation_sources.clear();
            // Constant bit results survive node rollback. They are valid only
            // under the previous empty-owner assumption, so discard them too.
            observation.bits.clear();B needed=guard();
            const auto&local=source_graph(observation,observation_sources,v);auto found=local.find(src);
            if(found==local.end())return true;
            B use=observation.land(needed,found->second);
            if(observation.implies(use,0))return true;
            if(!boundary||!sending.count(src))return false;
            // A routing source can hand off to an unpooled sink instead of an
            // internal destination. Read the body only on that final departure,
            // never during an internal move or a stalled intermediate cycle.
            B internal=0;
            for(const auto&e:edges)if(e.src==src){
              const auto&choices=source_graph(observation,observation_sources,objects[e.dst][4][2]);auto choice=choices.find(src);
              if(choice!=choices.end()){B en=observation.land(observation.neg(observation.full(e.dst)),observation.bit(objects[e.dst][4][1]));
                internal=observation.lor(internal,observation.land(en,choice->second));}
            }
            B departure=observation.land(observation.full(src),observation.bit(objects[src][4][3]));
            return observation.implies(use,observation.land(departure,observation.neg(internal)));
          });
          if(!irrelevant){terminal=false;reason=sending.count(src)?"intermediate reader":"invalid preview";break;}
        }}catch(const std::runtime_error&e){terminal=false;++fallback_proofs;reason=e.what();}
        if(!terminal){std::fill(retained.begin()+low,retained.begin()+low+size,true);retained_reads.push_back({{"value",v},{"low",low},{"width",size},{"reason",reason},{"owner",observing}});
          if(size>=64&&observing!=none&&!sending.count(observing))early_readers.insert(observing);}
        observations.push_back({v,low,size});
      };
      for(const auto&o:original.ops)for(unsigned a=0;a<o.args.size();++a)if(tracked.count(o.args[a])){
        if(forwarding(o)&&original.widths[o.out]==width&&(o.code==1||a>0))continue;
        observe(o.args[a],o.code==17?unsigned(o.imm[0]):0,o.code==17?original.widths[o.out]:width,[&](){return operand_demand(o,a,96);});
      }
      auto always=[](){return B(1);};
      for(const auto&p:original.metadata["ports"])if(p[0]==1)observe(p[1],0,width,always);
      for(const auto&r:original.metadata["registers"])for(const auto&v:r)observe(v,0,width,always);
      for(const auto&r:original.metadata["reads"])for(unsigned i=1;i<5;++i)observe(r[i],0,width,always);
      for(const auto&r:original.metadata["writes"])for(unsigned i=1;i<5;++i)observe(r[i],0,width,[&](){return i==3||r[3]==none?B(1):observation.bit(r[3]);});
      for(const auto&r:original.metadata["assertions"])for(unsigned i=0;i<3;++i)observe(r[i],0,width,always);
      for(Id owner=0;owner<objects.size();++owner)for(unsigned a=0;a<objects[owner][4].size();++a){Id v=objects[owner][4][a];if(members.count(owner)&&a==2)continue;
        observe(v,0,width,[&](){if(!fifo_payload(objects[owner],a))return B(1);B enabled=observation.bit(objects[owner][4][1]);
          return objects[owner][2]==1&&objects[owner][3]==0?observation.land(observation.neg(observation.full(owner)),enabled):enabled;},fifo_payload(objects[owner],a));}
      // Grant/transfer controls can read packet bits even without a standalone
      // projection. Keep those bits directly available as well.
      for(const auto&atom:b.atoms)if(atom.kind==2&&members.count(atom.value))retained.at(atom.bit)=true;
      if(isolate_readers&&!early_readers.empty()){
        std::vector<Id>remaining;for(Id owner:group)if(!early_readers.count(owner))remaining.push_back(owner);
        if(remaining.size()>=2){
          // Keep early-reading sinks as original queues. A fresh proof on the
          // smaller region must establish all new terminal enqueue boundaries.
          cuts.push_back({{"objects",group},{"excluded",early_readers},{"remaining",remaining},{"retained_reads",retained_reads}});
          groups.push_back(std::move(remaining));continue;
        }
      }
      if(std::none_of(retained.begin(),retained.end(),[](bool x){return x;}))retained.back()=true;
      unsigned header=std::count(retained.begin(),retained.end(),true),body=width-header;
      if(body<64)throw std::runtime_error("fewer than 64 opaque payload bits");
      for(const auto&o:observations)if(std::find(retained.begin()+o.low,retained.begin()+o.low+o.width,false)!=retained.begin()+o.low+o.width)hydrate.insert(o.value);
      struct Field{unsigned low,width,offset;bool retained;};std::vector<Field>fields;
      std::vector<unsigned>inline_offsets(width,UINT32_MAX);unsigned offsets[2]{};
      Json inline_ranges=Json::array(),pooled_ranges=Json::array();
      for(unsigned low=0;low<width;){unsigned end=low+1;while(end<width&&retained[end]==retained[low])++end;bool keep=retained[low];
        fields.push_back({low,end-low,offsets[keep],keep});
        if(keep)for(unsigned k=low;k<end;++k)inline_offsets[k]=offsets[keep]+k-low;
        (keep?inline_ranges:pooled_ranges).push_back({low,end-low});offsets[keep]+=end-low;low=end;
      }
      // Handle swaps preserve a permutation because an ordinary one-entry queue
      // cannot enqueue and dequeue together. Late failures roll back the region.
      unsigned hw=1;while((1u<<hw)<group.size())++hw;Id memory=m.metadata["memories"].size();m.metadata["memories"].push_back({body,group.size()});
      stage="emission";
      bool packed=pack_handles&&!bitmap_slots;unsigned lanes=64/hw;std::vector<Id>handle_states;
      if(packed)for(unsigned first=0;first<group.size();first+=lanes){handle_states.push_back(m.widths.size());m.widths.push_back(std::min(lanes,unsigned(group.size())-first)*hw);}
      std::map<Id,Id>handles,headers;
      for(unsigned pos=0;pos<group.size();++pos){Id owner=group[pos];
        if(bitmap_slots){Id packet=emit(29,header+hw,{}, {owner,3});headers[owner]=emit(17,header,{packet},{0});handles[owner]=emit(17,hw,{packet},{header});
          m.metadata["objects"][owner][1]=header+hw;continue;}
        if(packed)handles[owner]=emit(17,hw,{handle_states[pos/lanes]},{(pos%lanes)*hw});
        else{handles[owner]=m.widths.size();m.widths.push_back(hw);}
        headers[owner]=emit(29,header,{}, {owner,3});m.metadata["objects"][owner][1]=header;}
      std::map<B,Id>bools;std::map<unsigned,Id>atoms;
      // The decision graph proves ownership; it need not become executable muxes.
      // Reuse already-proved scalar controls before synthesizing missing events.
      for(const auto&entry:b.bits)if(entry.first.second==0&&entry.second>1&&original.widths[entry.first.first]==1)
        bools.emplace(entry.second,entry.first.first);
      std::function<Id(B)>condition=[&](B g)->Id{auto it=bools.find(g);if(it!=bools.end())return it->second;Id value;if(g<2)value=emit(0,1,{}, {g});else{auto n=b.nodes[g];Id a;
          auto found=atoms.find(n.var);if(found!=atoms.end())a=found->second;else{auto atom=b.atoms[n.var];if(atom.kind==1)a=emit(29,1,{}, {atom.value,2});else if(atom.kind==2){
            // Do not add an untracked payload read to another candidate region.
            // Existing scalar controls above remain valid through its rewrite.
            if(!members.count(atom.value))throw std::runtime_error("cross-region payload control needs an existing scalar binding");
            if(inline_offsets.at(atom.bit)==UINT32_MAX)throw std::runtime_error("body bit escaped control proof");
            a=emit(17,1,{headers.at(atom.value)},{inline_offsets.at(atom.bit)});
          }else if(atom.kind==3){const auto&d=b.matchers.at(atom.value);Id mask=d.value==none?emit(29,d.rows,d.args,{d.owner,d.query}):d.value;
            a=emit(0,1,{}, {0});for(unsigned row=0;row<d.rows;++row)if(((row+1)>>atom.bit)&1)a=emit(4,1,{a,emit(17,1,{mask},{row})});
          }else a=original.widths[atom.value]==1?atom.value:emit(17,1,{atom.value},{atom.bit});atoms[n.var]=a;}
          value=emit(15,1,{a,condition(n.low),condition(n.high)},{1});}bools[g]=value;return value;};
      std::map<Id,Id>header_values,fresh_values;
      std::function<Id(Id,bool)>project=[&](Id v,bool route)->Id{auto&cache=route?header_values:fresh_values;auto f=cache.find(v);if(f!=cache.end())return f->second;const auto*o=defs[v];Id x;unsigned w=route?header:body;
        if(o&&o->code==29&&o->imm[1]==3&&members.count(o->imm[0]))x=route?headers.at(o->imm[0]):emit(0,body,{},std::vector<uint64_t>((body+63)/64));
        else if(o&&o->code==1)x=project(o->args[0],route);
        else if(o&&(o->code==15||o->code==16)){std::vector<Id>args{o->args[0]};for(unsigned a=1;a<o->args.size();++a)args.push_back(project(o->args[a],route));x=emit(o->code,w,std::move(args),o->imm);}
        else{std::vector<Id>parts;for(const auto&field:fields)if(field.retained==route)parts.push_back(emit(17,field.width,{v},{field.low}));x=parts.size()==1?parts[0]:emit(20,w,std::move(parts));}
        return cache[v]=x;};
      unsigned allocator_count=0,departure_count=0;
      if(bitmap_slots){
        unsigned capacity=group.size();Id free_state=m.widths.size();m.widths.push_back(capacity);Id available=free_state;
        Id zero=emit(0,1,{}, {0}),zero_index=emit(0,hw,{}, {0});
        std::vector<uint64_t>one_words((capacity+63)/64);one_words[0]=1;
        Id one=emit(0,capacity,{},one_words),retired=emit(0,capacity,{},std::vector<uint64_t>((capacity+63)/64));
        Id yes=emit(0,1,{}, {1});std::map<Id,Id>forwarded,fresh_paths;
        std::function<Id(Id)>fresh_path=[&](Id v)->Id{auto found=fresh_paths.find(v);if(found!=fresh_paths.end())return found->second;
          const auto*o=defs[v];Id result=yes;
          if(o&&o->code==29&&o->imm[1]==3&&members.count(o->imm[0]))result=zero;
          else if(o&&o->code==1)result=fresh_path(o->args[0]);
          else if(o&&(o->code==15||o->code==16)){std::vector<Id>args{o->args[0]};for(unsigned a=1;a<o->args.size();++a)args.push_back(fresh_path(o->args[a]));
            bool same=std::all_of(args.begin()+1,args.end(),[&](Id a){return a==args[1];});
            // Header forwarding retains the original strict selector check.
            result=same?args[1]:emit(o->code,1,std::move(args),o->imm);}
          return fresh_paths[v]=result;
        };
        std::function<Id(Id)>index_of=[&](Id v)->Id{auto found=forwarded.find(v);if(found!=forwarded.end())return found->second;
          const auto*o=defs[v];Id result=zero_index;
          if(o&&o->code==29&&o->imm[1]==3&&members.count(o->imm[0]))result=handles.at(o->imm[0]);
          else if(o&&o->code==1)result=index_of(o->args[0]);
          else if(o&&(o->code==15||o->code==16)){std::vector<Id>args{o->args[0]};for(unsigned a=1;a<o->args.size();++a)args.push_back(index_of(o->args[a]));result=emit(o->code,hw,std::move(args),o->imm);}
          return forwarded[v]=result;
        };
        for(Id owner:group){Id data=objects[owner][4][2],index=index_of(data),fresh=fresh_path(data);
          if(fresh!=zero){++allocator_count;
            Id push=emit(3,1,{emit(2,1,{emit(29,1,{}, {owner,2})}),objects[owner][4][1].get<Id>()});
            Id enable=emit(3,1,{push,fresh});unsigned bank_width=std::min(capacity,64u);Id low=emit(17,bank_width,{available},{0}),bank=zero;
            if(capacity>64){bank=emit(12,1,{low,emit(0,64,{}, {0})});Id high=emit(18,64,{emit(17,capacity-64,{available},{64})});low=emit(15,64,{bank,low,high},{1});}
            Id grant=emit(3,bank_width,{low,emit(7,bank_width,{emit(0,bank_width,{}, {0}),low})});
            // Empty pools only occur with no accepted fresh enqueue. Supply a
            // legal selector even then, preserving eager debug strict checks.
            Id safe=emit(4,bank_width,{grant,emit(18,bank_width,{emit(12,1,{grant,emit(0,bank_width,{}, {0})})})});
            std::vector<Id>choices{safe};for(unsigned k=0;k<bank_width;++k)choices.push_back(emit(0,hw,{}, {k}));
            Id allocated=emit(16,hw,std::move(choices));
            if(capacity>64)allocated=emit(4,hw,{allocated,emit(9,hw,{emit(18,hw,{bank}),emit(0,3,{}, {6})})});
            Id claim=emit(3,capacity,{emit(9,capacity,{one,allocated}),emit(19,capacity,{enable})});
            available=emit(3,capacity,{available,emit(2,capacity,{claim})});
            index=emit(15,hw,{enable,index,allocated},{1});
            m.metadata["writes"].push_back({memory,allocated,project(data,false),enable,none,0});
          }
          m.metadata["objects"][owner][4][2]=emit(20,header+hw,{project(data,true),index});
          Id departure=none;auto saved_bools=bools;auto saved_atoms=atoms;
          b.temporary([&](){B outgoing=0;for(const auto&e:edges)if(e.src==owner)outgoing=b.lor(outgoing,e.guard);
            bool internal_only=false;
            try{internal_only=b.implies(b.land(b.full(owner),b.bit(objects[owner][4][3])),outgoing);}catch(const std::runtime_error&){/* Emit the exact departure condition when the proof is inconclusive. */}
            if(!internal_only){Id pop=emit(3,1,{emit(29,1,{}, {owner,2}),objects[owner][4][3].get<Id>()});
              departure=emit(3,1,{pop,emit(2,1,{condition(outgoing)})});}
            return true;
          });
          bools=std::move(saved_bools);atoms=std::move(saved_atoms);
          if(departure!=none){++departure_count;
            retired=emit(4,capacity,{retired,emit(3,capacity,{emit(9,capacity,{one,handles.at(owner)}),emit(19,capacity,{departure})})});
          }
        }
        // Allocate only from old free slots; departing payloads remain intact
        // through all current readers and publish their free bits at advance.
        std::vector<uint64_t>initial((capacity+63)/64,UINT64_MAX);if(capacity%64)initial.back()=(UINT64_C(1)<<(capacity%64))-1;
        m.metadata["registers"].push_back({free_state,emit(4,capacity,{available,retired}),objects[group[0]][4][0],emit(0,capacity,{},initial)});
      }else{
      std::vector<Id>next_handles;
      for(unsigned pos=0;pos<group.size();++pos){Id owner=group[pos],next=handles.at(owner),data=objects[owner][4][2];B incoming=0;
        for(const auto&e:edges){
          if(e.src==owner)next=emit(15,hw,{condition(e.guard),next,handles.at(e.dst)},{1});
          if(e.dst==owner){next=emit(15,hw,{condition(e.guard),next,handles.at(e.src)},{1});incoming=b.lor(incoming,e.guard);}}
        if(packed)next_handles.push_back(next);
        else m.metadata["registers"].push_back({handles.at(owner),next,objects[owner][4][0],emit(0,hw,{}, {pos})});
        m.metadata["objects"][owner][4][2]=project(data,true);
        B fresh=b.land(enqueue.at(owner),b.neg(incoming));if(fresh)m.metadata["writes"].push_back({memory,handles.at(owner),project(data,false),condition(fresh),none,0});
      }
      if(packed)for(unsigned first=0;first<group.size();first+=lanes){unsigned count=std::min(lanes,unsigned(group.size())-first);uint64_t initial=0;
        for(unsigned k=0;k<count;++k)initial|=uint64_t(first+k)<<(k*hw);
        std::vector<Id>parts(next_handles.begin()+first,next_handles.begin()+first+count);
        Id next=emit(20,count*hw,std::move(parts));
        if(handle_states.size()>1){
          // No ownership exchange can touch an idle queue. Guard the complete
          // packed update using existing FIFO events, before rebuilding lanes.
          Id active=emit(0,1,{}, {0});
          for(unsigned k=0;k<count;++k){Id owner=group[first+k],full=emit(29,1,{}, {owner,2});
            Id push=emit(3,1,{emit(2,1,{full}),objects[owner][4][1].get<Id>()});
            Id pop=emit(3,1,{full,objects[owner][4][3].get<Id>()});
            active=emit(4,1,{active,emit(4,1,{push,pop})});
          }
          next=emit(15,count*hw,{active,handle_states[first/lanes],next},{1});
        }
        m.metadata["registers"].push_back({handle_states[first/lanes],next,objects[group[0]][4][0],emit(0,count*hw,{}, {initial})});
      }
      }
      for(Id v:tracked){
        if(!hydrate.count(v)){replaced[v]={0,v,{},std::vector<uint64_t>((width+63)/64)};continue;}
        // Hydrate only at proved terminal observers. Nonmember/zero arms keep
        // their original body expression; selected pooled rows are read once.
        B present=0;Id handle=emit(0,hw,{}, {0});for(auto [owner,g]:sources(v)){present=b.lor(present,g);handle=emit(15,hw,{condition(g),handle,handles.at(owner)},{1});}
        Id data=emit(24,body,{handle},{memory});data=emit(15,body,{condition(present),project(v,false),data},{1});Id control=project(v,true);
        std::vector<Id>parts;for(const auto&field:fields)parts.push_back(emit(17,field.width,{field.retained?control:data},{field.offset}));
        replaced[v]={20,v,std::move(parts),{}};
      }
      for(const auto&o:original.ops)if(o.code==17&&tracked.count(o.args[0])){unsigned low=o.imm[0],end=low+original.widths[o.out];
        if(std::find(retained.begin()+low,retained.begin()+end,false)==retained.begin()+end)replaced[o.out]={17,o.out,{project(o.args[0],true)},{inline_offsets[low]}};
      }
      // Preserve even unused strict selector checks through the narrow header path.
      for(Id v:tracked)if(defs[v]&&defs[v]->code==16)(void)project(v,true);
      accepted.push_back({{"objects",group},{"slots",group.size()},{"body_bits",body},{"routing_bits",header},{"inline_bits",header},{"inline_ranges",inline_ranges},{"pooled_ranges",pooled_ranges},{"retained_reads",retained_reads},{"observation_proof_fallbacks",fallback_proofs},{"handle_bits",hw},{"handle_state_words",bitmap_slots?0:packed?handle_states.size():group.size()},{"free_bitmap_words",bitmap_slots?(group.size()+63)/64:0},{"allocators",allocator_count},{"departures",departure_count},{"transfer_edges",edges.size()},{"hydration_roots",hydrate.size()},{"proof_nodes",b.nodes.size()},{"matcher_choices",b.matchers.size()}});
      if(fuse_state&&packed&&handle_states.size()==1)fused_regions.emplace_back(group,handle_states[0]);
    }catch(const std::runtime_error&e){m=std::move(checkpoint);added.resize(added_before);replaced=std::move(replaced_before);rejected.push_back({{"objects",group},{"reason",e.what()},{"stage",stage},{"value",proof_value},{"retained_reads",retained_reads}});}
  }
  std::vector<Op>all;for(auto o:original.ops){auto i=replaced.find(o.out);if(i!=replaced.end())o=i->second;all.push_back(std::move(o));}all.insert(all.end(),added.begin(),added.end());
  std::vector<Id>producer(m.widths.size(),none);for(Id i=0;i<all.size();++i)producer[all[i].out]=i;
  std::vector<unsigned char>seen(all.size());m.ops.clear();std::function<void(Id)>visit=[&](Id i){if(i==none||seen[i]==2)return;if(seen[i]==1)throw std::runtime_error("transport rewrite cycle");seen[i]=1;for(Id a:all[i].args)visit(producer[a]);seen[i]=2;m.ops.push_back(std::move(all[i]));};for(Id i=0;i<all.size();++i)visit(i);
  m.metadata["payload_exchange_summary"]={{"regions",accepted},{"rejected",rejected},{"reader_boundaries",cuts},{"ownership",bitmap_slots?"live handles in queue words; free bitmap changes only on region entry/departure":"permutation of one slot per hardware queue; swap on accepted matching edges"}};
  if(fuse_state)fuse_transport_controls(m,fused_regions);
  optimize_body(m);
}
}
