// Plans flow regions, shares native queries, and separates independent matcher priorities.
#include "model.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

namespace rds {
void fold_stationary_matchers(Model &m) {
  // A priority starts at zero and changes only after an accepted nonzero grant.
  // Prove that update impossible; query operands remain completely independent.
  std::vector<const Op*> defs(m.widths.size());
  for(const auto&o:m.ops)defs[o.out]=&o;
  std::map<std::pair<Id,unsigned>,int> memo;
  std::function<int(Id,unsigned)> bit=[&](Id v,unsigned b)->int {
    auto key=std::make_pair(v,b);auto found=memo.find(key);
    if(found!=memo.end())return found->second;
    int r=-1;const auto*p=defs[v];
    if(p){const auto&a=p->args;
      if(p->code==0)r=(p->imm[b/64]>>(b%64))&1;
      else if(p->code==1)r=bit(a[0],b);
      else if(p->code==17)r=bit(a[0],b+p->imm[0]);
      else if(p->code==18||p->code==19)r=b<m.widths[a[0]]?bit(a[0],b):p->code==18?0:bit(a[0],m.widths[a[0]]-1);
      else if(p->code==20){unsigned offset=b;for(Id x:a){if(offset<m.widths[x]){r=bit(x,offset);break;}offset-=m.widths[x];}}
      else if(p->code==2){int x=bit(a[0],b);if(x>=0)r=!x;}
      else if(p->code>=3&&p->code<=5){int x=bit(a[0],b),y=bit(a[1],b);
        if(p->code==3){if(x==0||y==0)r=0;else if(x==1&&y==1)r=1;}
        else if(p->code==4){if(x==1||y==1)r=1;else if(x==0&&y==0)r=0;}
        else if(x>=0&&y>=0)r=x^y;
      }
    }
    return memo[key]=r;
  };
  std::vector<uint64_t> stationary(m.metadata["objects"].size());Json details=Json::array();
  for(Id id=0;id<stationary.size();++id){const auto&o=m.metadata["objects"][id];
    if(o[0]!=10||!(o[3].get<unsigned>()&32))continue;
    unsigned rows=o[1],cols=o[2];
    for(unsigned c=0;c<cols;++c){bool no_requests=true;
      for(unsigned r=0;r<rows;++r)no_requests&=bit(o[4][1+c],r)==0;
      bool no_accept=bit(o[4][1+cols],c)==0;
      if(rows==1||no_requests||no_accept){stationary[id]|=UINT64_C(1)<<c;
        details.push_back({{"object",id},{"occurrence",o[5]},{"column",c},{"no_requests",no_requests},{"no_accept",no_accept},{"single_row",rows==1}});}
    }
  }
  std::vector<Op> ops;unsigned rewritten=0,fixed_steps=0;
  auto emit=[&](unsigned code,unsigned width,std::vector<Id> args,std::vector<uint64_t> imm=std::vector<uint64_t>{}){
    Id out=m.widths.size();m.widths.push_back(width);ops.push_back({code,out,std::move(args),std::move(imm)});return out;};
  for(const auto&o:m.ops){
    if(o.code!=29||!stationary[o.imm[0]]){ops.push_back(o);continue;}
    const auto&owner=m.metadata["objects"][o.imm[0]];unsigned rows=owner[1],cols=owner[2],q=o.imm[1];
    unsigned last=q>=cols?q-cols:q;uint64_t relevant=q>=cols?UINT64_C(1)<<last:last==63?UINT64_MAX:(UINT64_C(1)<<(last+1))-1;
    if(!(stationary[o.imm[0]]&relevant)){ops.push_back(o);continue;}
    Id zero=emit(0,rows,{}, {0}),taken=q>=cols?o.args[0]:zero,grant=none;
    for(unsigned col=q>=cols?last:0;col<=last;++col){Id requests=q>=cols?o.args[1]:o.args[col];
      if(stationary[o.imm[0]]&(UINT64_C(1)<<col)){
        Id eligible=emit(3,rows,{requests,emit(2,rows,{taken})});
        grant=emit(3,rows,{eligible,emit(7,rows,{zero,eligible})});++fixed_steps;
      }else grant=emit(29,rows,{taken,requests},{o.imm[0],cols+col});
      if(col<last)taken=emit(4,rows,{taken,grant});
    }
    ops.push_back({1,o.out,{grant},{}});++rewritten;
  }
  m.ops=std::move(ops);if(rewritten)optimize_body(m);
  m.metadata["stationary_matcher_summary"]={{"columns",details},{"queries_rewritten",rewritten},{"fixed_steps",fixed_steps},
    {"proof","zero initialized priority with no possible accepted grant; preserve actual prefix/step query operands"}};
}
void lift_indexed_datapaths(Model &m) {
  // Move selection in front of equal-width views of one packed array. Select
  // an element index, then load just that element; preserve strict one-hot
  // diagnostics and the permissive zero result inside one fused view operation.
  std::vector<const Op*> defs(m.widths.size());
  for(const auto &o:m.ops)defs[o.out]=&o;
  struct View{Id base;uint64_t low;};
  auto view=[&](Id v){View result{v,0};
    for(unsigned fuel=0;fuel<64;++fuel){const auto *p=defs[result.base];if(!p)break;
      if(p->code==1)result.base=p->args[0];
      else if(p->code==17){result.low+=p->imm[0];result.base=p->args[0];}
      else break;}
    return result;
  };
  std::vector<Op> ops;Json details=Json::array();
  auto emit=[&](unsigned code,unsigned width,std::vector<Id> args,std::vector<uint64_t> imm=std::vector<uint64_t>{}){
    Id v=m.widths.size();m.widths.push_back(width);ops.push_back({code,v,std::move(args),std::move(imm)});return v;};
  for(const auto &o:m.ops){
    if((o.code!=15&&o.code!=16)||o.args.size()<3||m.widths[o.out]<16||(o.code==16&&m.widths[o.args[0]]>64)){ops.push_back(o);continue;}
    unsigned width=m.widths[o.out];Id base=none;std::vector<uint64_t> lanes;bool legal=true;
    for(size_t i=1;i<o.args.size();++i){auto v=view(o.args[i]);
      if(base==none)base=v.base;
      if(v.base!=base||v.low%width||v.low+width>m.widths[base]){legal=false;break;}
      lanes.push_back(v.low/width);}
    if(!legal||m.widths[base]%width||m.widths[base]/width<2){ops.push_back(o);continue;}
    uint32_t count=m.widths[base]/width,iw=1;
    while((UINT64_C(1)<<iw)<count)++iw;
    if(o.code==16){std::vector<uint64_t> offsets;for(uint64_t lane:lanes)offsets.push_back(lane*width);
      ops.push_back({33,o.out,{o.args[0],base},std::move(offsets)});
    }else{
      std::vector<Id> choices{o.args[0]};for(uint64_t lane:lanes)choices.push_back(emit(0,iw,{}, {lane}));
      Id index=emit(15,iw,std::move(choices),o.imm),payload=emit(21,width,{base,index},{count,width});
      ops.push_back({1,o.out,{payload},{}});
    }
    details.push_back({{"output",o.out},{"source",base},{"element_width",width},{"elements",count},
      {"choice_elements",lanes},{"onehot",o.code==16}});
  }
  m.ops=std::move(ops);
  m.metadata["indexed_datapath_summary"]={{"selections",details.size()},{"datapaths",details},
    {"provenance","output and source refer to the pre-cleanup graph"}};
  optimize_body(m);
}
void split_matcher_columns(Model &m) {
  // A matcher has independent per-output priorities. Split their update
  // ownership, while explicitly preserving greedy exclusions between queries.
  if(std::none_of(m.metadata["objects"].begin(),m.metadata["objects"].end(),[](const Json&o){return o[0]==10&&o[2].get<unsigned>()>1;}))return;
  for(const auto&o:m.ops)if(o.code==RDS_KERNEL_OPCODE)
    throw std::runtime_error("split matcher columns before lifting contract programs");
  pack_matchers(m);
  Json old=m.metadata["objects"],objects=Json::array(),mapping=Json::array();
  std::vector<std::vector<Id>> owners(old.size());unsigned split=0;
  for(Id id=0;id<old.size();++id){const auto&o=old[id];unsigned count=o[0]==10?o[2].get<unsigned>():1;
    if(count>1)++split;
    for(unsigned c=0;c<count;++c){owners[id].push_back(objects.size());auto next=o;
      if(count>1){next[2]=1;next[3]=96;next[5]=o[5].get<std::string>()+"/column"+std::to_string(c);}
      objects.push_back(next);
    }
    if(count>1)mapping.push_back({{"source",o[5]},{"columns",owners[id]}});
  }
  if(!split)return;
  std::vector<Op> ops;std::map<std::tuple<Id,Id,Id>,Id> queries;
  auto emit=[&](unsigned code,unsigned width,std::vector<Id>args,std::vector<uint64_t>imm=std::vector<uint64_t>{}){
    Id out=m.widths.size();m.widths.push_back(width);ops.push_back({code,out,std::move(args),std::move(imm)});return out;};
  auto step=[&](Id owner,unsigned rows,Id taken,Id request){auto key=std::make_tuple(owner,taken,request);auto found=queries.find(key);if(found!=queries.end())return found->second;
    Id grant=emit(29,rows,{taken,request},{owner,1});queries[key]=grant;return grant;};
  for(auto o:m.ops){if(o.code!=29){ops.push_back(std::move(o));continue;}Id id=o.imm[0];const auto&owner=old[id];unsigned cols=owners[id].size();
    if(cols==1){o.imm[0]=owners[id][0];ops.push_back(std::move(o));continue;}
    unsigned rows=owner[1],q=o.imm[1];Id grant;
    if(q>=cols)grant=step(owners[id][q-cols],rows,o.args[0],o.args[1]);
    else{Id taken=emit(0,rows,{}, {0});grant=taken;for(unsigned c=0;c<=q;++c){grant=step(owners[id][c],rows,taken,o.args[c]);if(c<q)taken=emit(4,rows,{taken,grant});}}
    ops.push_back({1,o.out,{grant},{}});
  }
  for(Id id=0;id<old.size();++id)if(owners[id].size()>1){const auto&o=old[id];unsigned rows=o[1],cols=o[2];std::vector<Id> in=o[4];Id taken=emit(0,rows,{}, {0});
    for(unsigned c=0;c<cols;++c){Id grant=(o[3].get<unsigned>()&64)?in[1+c]:step(owners[id][c],rows,taken,in[1+c]);
      objects[owners[id][c]][4]=std::vector<Id>{in[0],grant,emit(17,1,{in.back()},{c})};
      if(!(o[3].get<unsigned>()&64)&&c+1<cols)taken=emit(4,rows,{taken,grant});
    }
  }
  m.ops=std::move(ops);m.metadata["objects"]=std::move(objects);
  m.metadata["matcher_column_split"]={{"matchers",split},{"mapping",mapping},{"state_policy","one priority owner per original output; no state copies"}};
  optimize_body(m);m.validate();
}
void reuse_matcher_grants(Model &m) {
  // Preserve query dependencies. A supplied update grant must come from the
  // same request column and exactly the OR of all earlier update grants.
  std::vector<const Op*> defs(m.widths.size());
  for(const auto &op:m.ops)defs[op.out]=&op;
  unsigned groups=0,columns=0;
  for(Id id=0;id<m.metadata["objects"].size();++id){
    auto &o=m.metadata["objects"][id];if(o[0]!=10||o[3]!=32)continue;
    unsigned rows=o[1],depth=o[2];std::vector<Id> inputs=o[4],queries(depth,none);
    bool eligible=true;
    for(const auto &op:m.ops)if(op.code==29&&op.imm[0]==id){auto q=op.imm[1];
      if(q>=2*depth){eligible=false;break;}
      unsigned col=q<depth?q:q-depth;
      bool same=q<depth?op.args.size()==col+1&&std::equal(op.args.begin(),op.args.end(),inputs.begin()+1)
                       :op.args.size()==2&&op.args[1]==inputs[col+1];
      if(!same||queries[col]!=none){eligible=false;break;}
      queries[col]=op.out;}
    for(Id q:queries)eligible&=q!=none;
    if(!eligible)continue;
    for(unsigned col=0;col<depth&&eligible;++col){
      std::set<Id> terms,visited;
      std::function<void(Id)> prefix=[&](Id v){
        if(!visited.insert(v).second)return;
        const auto *op=defs[v];
        if(op&&op->code==0&&std::all_of(op->imm.begin(),op->imm.end(),[](uint64_t x){return x==0;}))return;
        if(op&&m.widths[v]==rows&&(op->code==1||op->code==4)){for(Id a:op->args)prefix(a);}
        else terms.insert(v);
      };
      // A prefix-form query already names all original earlier request
      // columns. Only explicit-prefix queries need the OR-of-grants proof.
      if(defs[queries[col]]->imm[1]>=depth){
        prefix(defs[queries[col]]->args[0]);
        eligible=terms==std::set<Id>(queries.begin(),queries.begin()+col);
      }
    }
    if(!eligible)continue;
    for(unsigned col=0;col<depth;++col)inputs[1+col]=queries[col];
    o[4]=inputs;o[3]=96;++groups;columns+=depth;
  }
  m.metadata["matcher_grant_reuse_summary"]={{"objects",groups},{"columns",columns}};
}
void simplify_matcher_masks(Model &m) {
  // A matcher grant bit implies its corresponding request bit, independently
  // of priority state or previous-column exclusions. Prove downstream mask
  // redundancy through exact bit projections and Boolean conjunctions.
  std::vector<Id> producer(m.widths.size(),none);
  for(Id i=0;i<m.ops.size();++i)producer[m.ops[i].out]=i;
  struct Bit {Id value;uint32_t bit;};
  auto normalize=[&](Bit b){
    for(unsigned fuel=0;fuel<32;++fuel){Id p=producer[b.value];if(p==none)break;const auto&o=m.ops[p];
      if(o.code==1)b.value=o.args[0];
      else if(o.code==17){b.value=o.args[0];b.bit+=o.imm[0];}
      else if(o.code==20){uint32_t position=0;bool found=false;
        for(Id a:o.args){if(b.bit-position<m.widths[a]){b={a,b.bit-position};found=true;break;}position+=m.widths[a];}
        if(!found)break;
      }else if(o.code==18||o.code==19){if(b.bit<m.widths[o.args[0]])b.value=o.args[0];
        else if(o.code==19)b={o.args[0],m.widths[o.args[0]]-1};else break;
      }else break;
    }return b;
  };
  unsigned visits=0;bool used_matcher=false;
  std::function<bool(Bit,Bit,unsigned)> implies=[&](Bit a,Bit b,unsigned fuel){
    if(!fuel||++visits>512)return false;
    a=normalize(a);b=normalize(b);
    if(a.value==b.value&&a.bit==b.bit)return true;
    Id pa=producer[a.value],pb=producer[b.value];
    const Op *x=pa==none?nullptr:&m.ops[pa],*y=pb==none?nullptr:&m.ops[pb];
    if(x&&x->code==0 && !((x->imm[a.bit/64]>>(a.bit%64))&1))return true;
    if(y&&y->code==0 && ((y->imm[b.bit/64]>>(b.bit%64))&1))return true;
    if(y&&y->code==3)return implies(a,{y->args[0],b.bit},fuel-1)&&implies(a,{y->args[1],b.bit},fuel-1);
    if(x&&x->code==3)return implies({x->args[0],a.bit},b,fuel-1)||implies({x->args[1],a.bit},b,fuel-1);
    if(x&&x->code==4)return implies({x->args[0],a.bit},b,fuel-1)&&implies({x->args[1],a.bit},b,fuel-1);
    if(x&&x->code==15){
      for(size_t i=1;i<x->args.size();++i)if(!implies({x->args[i],a.bit},b,fuel-1))return false;
      return true;
    }
    if(!x||x->code!=29)return false;
    const auto &object=m.metadata["objects"][x->imm[0]];
    if(object[0]!=10)return false;
    uint32_t rows=object[1],columns=object[2],query=x->imm[1];
    bool packed=object[3].get<uint32_t>()&32;
    Bit request=packed?Bit{x->args[query<columns?query:1],a.bit}:
      Bit{x->args[query<columns?query*rows+a.bit:1+a.bit],0};
    bool proven=implies(request,b,fuel-1);used_matcher|=proven;return proven;
  };
  uint64_t rewritten=0,bits=0;Json roots=Json::array();
  for(auto &o:m.ops){if(o.code!=3||m.widths[o.out]>64)continue;
    for(unsigned side=0;side<2;++side){visits=0;used_matcher=false;bool valid=true;
      for(uint32_t bit=0;bit<m.widths[o.out]&&valid;++bit)valid=implies({o.args[side],bit},{o.args[1-side],bit},24);
      if(valid&&used_matcher){roots.push_back(o.out);++rewritten;bits+=m.widths[o.out];o={1,o.out,{o.args[side]}, {}};break;}
    }
  }
  m.metadata["matcher_mask_summary"]={{"rewritten",rewritten},{"bits",bits},{"original_outputs",roots}};
  optimize_body(m);recover_words(m);optimize_body(m);
}
void plan_flow_regions(Model &m, bool reorder) {
  // Contracts locate boundaries; only actual DFG edges establish dependencies,
  // sharing and live-outs. Overlapping wrapper annotations confer no ownership.
  const size_t n=m.ops.size();
  std::vector<Id> producer(m.widths.size(),none);
  std::vector<std::vector<Id>> users(m.widths.size());
  std::vector<bool> observed(m.widths.size());
  for(Id i=0;i<n;++i){producer[m.ops[i].out]=i;for(Id a:m.ops[i].args)users[a].push_back(i);}
  auto observe=[&](const Json &j){Id v=j.get<Id>();if(v!=none)observed.at(v)=true;};
  for(auto&p:m.metadata["ports"])observe(p[1]);
  for(auto&r:m.metadata["registers"])for(auto&a:r)observe(a);
  for(auto name:{"writes","reads"})for(auto&r:m.metadata[name])for(size_t i=1;i<5;++i)observe(r[i]);
  for(auto&r:m.metadata["assertions"])for(size_t i=0;i<3;++i)observe(r[i]);
  for(auto&o:m.metadata["objects"])for(auto&a:o[4])observe(a);
  auto total=[](uint32_t code){return code!=0&&code!=16&&code!=33&&(code<21||code>24)&&code!=29;};
  struct Region {Id contract;std::vector<Id> members,inputs,outputs;uint64_t words=0,escaping=0;};
  std::vector<Region> candidates;
  Json details=Json::array();
  std::set<std::vector<Id>> seen;
  const std::set<std::string> kinds={"map","map-valid","filter","filter-valid","gate","demux","grant-crossbar","atomic-fork","fork-valid"};
  const auto contracts=m.metadata.value("contracts",Json::array());
  for(Id ci=0;ci<contracts.size();++ci){
    const auto &c=contracts[ci];if(!kinds.count(c[1].get<std::string>()))continue;
    std::set<Id> boundary,roots;
    for(const auto &b:c[2]){Id v=b[1];if(v==none)continue;std::string label=b[0];
      if(label.rfind("out",0)==0&&label.size()>=5&&label.substr(label.size()-5)==".bits")roots.insert(v);
      else boundary.insert(v);
    }
    std::set<Id> members;bool exhausted=false;
    std::function<void(Id)> visit=[&](Id v){
      Id p=producer[v];if(boundary.count(v)||p==none||!total(m.ops[p].code)||members.count(p))return;
      if(members.size()==1024){exhausted=true;return;}members.insert(p);
      for(Id a:m.ops[p].args)visit(a);
    };
    for(Id v:roots)visit(v);
    if(exhausted||members.empty())continue;
    Region r{ci,{members.begin(),members.end()},{},{}};
    if(!seen.insert(r.members).second)continue;
    std::set<Id> inputs,outputs;
    for(Id p:r.members){const auto &op=m.ops[p];r.words+=(uint64_t(m.widths[op.out])+63)/64;
      for(Id a:op.args)if(producer[a]==none||!members.count(producer[a]))inputs.insert(a);
      bool escapes=observed[op.out];for(Id u:users[op.out])escapes|=!members.count(u);
      if(escapes){outputs.insert(op.out);r.escaping+=(uint64_t(m.widths[op.out])+63)/64;}
    }
    r.inputs={inputs.begin(),inputs.end()};r.outputs={outputs.begin(),outputs.end()};
    std::vector<Id> values;for(Id p:r.members)values.push_back(m.ops[p].out);
    details.push_back({{"contract",ci},{"kind",c[1]},{"values",values},{"inputs",r.inputs},{"outputs",r.outputs},
      {"words",r.words},{"escaping_words",r.escaping},{"internal_words",r.words-r.escaping}});
    candidates.push_back(std::move(r));
  }
  // Prefer bodies with enough internal work to amortize their boundary. This
  // is a static ordering experiment, never permission to skip or cache work.
  std::stable_sort(candidates.begin(),candidates.end(),[](const Region&a,const Region&b){return a.words-a.escaping>b.words-b.escaping;});
  std::vector<Id> group(n,none);Id groups=0;size_t assigned=0;
  for(const auto&r:candidates){
    size_t available=0;for(Id p:r.members)available+=group[p]==none;
    if(available<16||r.words-r.escaping<8)continue;
    for(Id p:r.members)if(group[p]==none){group[p]=groups;++assigned;}
    ++groups;
  }
  if(reorder&&groups){
    // Stable ready sets preserve every data dependency. Partial operators are
    // also chained in original order so competing diagnostics do not swap.
    std::vector<unsigned> remaining(n);std::vector<std::vector<Id>> edges(n);
    Id prior=none;
    for(Id i=0;i<n;++i){for(Id a:m.ops[i].args)if(producer[a]!=none){++remaining[i];edges[producer[a]].push_back(i);}
      if(!total(m.ops[i].code)&&m.ops[i].code!=0&&m.ops[i].code!=29){if(prior!=none){++remaining[i];edges[prior].push_back(i);}prior=i;}}
    std::set<Id> ready;std::map<Id,std::set<Id>> local;
    auto insert=[&](Id i){ready.insert(i);if(group[i]!=none)local[group[i]].insert(i);};
    for(Id i=0;i<n;++i)if(!remaining[i])insert(i);
    std::vector<Op> ordered;Id active=none;
    while(!ready.empty()){
      Id i=*ready.begin();auto it=local.find(active);
      if(active!=none&&it!=local.end()&&!it->second.empty())i=*it->second.begin();
      active=group[i];ready.erase(i);if(active!=none)local[active].erase(i);
      ordered.push_back(m.ops[i]);for(Id u:edges[i])if(!--remaining[u])insert(u);
    }
    if(ordered.size()!=n)throw std::runtime_error("flow ordering introduced a cycle");
    m.ops=std::move(ordered);
  }
  m.metadata["flow_region_summary"]={{"version",1},{"reordered",reorder},{"groups",groups},{"assigned_operations",assigned},{"candidates",details}};
}
void share_tlb_queries(Model &m) {
  // Matching/address formation depend only on the virtual address and enable.
  // Permissions add current access/privilege controls without coupling them
  // into hit/address dependencies or repeating associative entry searches.
  std::vector<Op> added;
  for (auto &op : m.ops) {
    if(op.code!=29 || m.metadata["objects"][op.imm[0]][0]!=11 ||
       (op.imm[1]!=1 && op.imm[1]!=2))continue;
    Id lookup=none;uint64_t query=op.imm[1]==1?0:5;
    auto find=[&](const std::vector<Op>&ops){
      for(const auto &candidate:ops)
        if(candidate.code==29 && candidate.imm==std::vector<uint64_t>{op.imm[0],query} &&
           candidate.args==std::vector<Id>{op.args[0],op.args[1]})return candidate.out;
      return none;
    };
    lookup=find(m.ops);if(lookup==none)lookup=find(added);
    if(lookup==none){lookup=m.widths.size();m.widths.push_back(128);
      added.push_back({29,lookup,{op.args[0],op.args[1]},{op.imm[0],query}});}
    std::vector<Id> args{lookup};args.insert(args.end(),op.args.begin()+2,op.args.end());
    op.args=std::move(args);op.imm[1]+=2;
  }
  m.ops.insert(m.ops.end(),added.begin(),added.end());
  std::vector<Id> producer(m.widths.size(),none);
  for(Id i=0;i<m.ops.size();++i)producer[m.ops[i].out]=i;
  std::vector<bool> done(m.ops.size());std::vector<Op> ordered;
  std::function<void(Id)> visit=[&](Id i){if(i==none||done[i])return;done[i]=true;
    for(Id a:m.ops[i].args)visit(producer[a]);
    ordered.push_back(m.ops[i]);};
  for(Id i=0;i<m.ops.size();++i)visit(i);
  m.ops=std::move(ordered);
}
void pack_matchers(Model &m, bool recover_rows) {
  std::vector<Op> added;
  auto pack = [&](const std::vector<Id> &args) {
    Id out = m.widths.size();
    m.widths.push_back(args.size());
    added.push_back({20, out, args, {}});
    return out;
  };
  std::vector<bool> converted(m.metadata["objects"].size());
  size_t rows = 0, bits = 0;
  for (size_t id = 0; id < converted.size(); ++id) {
    auto &o = m.metadata["objects"][id];
    if (o[0] != 10 || o[3].get<uint32_t>() & 32)
      continue;
    uint32_t n = o[1], d = o[2];
    if (!n || n > 64 || !d || d > 64)
      throw std::runtime_error("packed matcher dimensions must be in 1..64");
    std::vector<Id> input = o[4];
    if (input.size() != 1 + size_t(n) * d + d)
      throw std::runtime_error("invalid matcher input count");
    for (auto v : input)
      if (v == none || m.widths.at(v) != 1)
        throw std::runtime_error(
            "matcher controls must be defined one-bit values");
    // Seed row regrouping before changing the native consumer. The existing
    // regroup pass rejects scalar redirects that would introduce feedback.
    if (recover_rows)
      for (uint32_t row = 0; row < n; ++row)
        pack(std::vector<Id>(input.begin() + 1 + row * d,
                             input.begin() + 1 + (row + 1) * d));
    std::vector<Id> packed{input[0]};
    for (uint32_t col = 0; col < d; ++col) {
      std::vector<Id> column;
      for (uint32_t row = 0; row < n; ++row)
        column.push_back(input[1 + row * d + col]);
      packed.push_back(pack(column));
    }
    packed.push_back(pack(std::vector<Id>(input.end() - d, input.end())));
    o[4] = packed;
    o[3] = o[3].get<uint32_t>() | 32;
    converted[id] = true;
    rows += n;
    bits += size_t(n) * d;
  }
  if (!rows)
    return;
  for (auto &op : m.ops) {
    if (op.code != 29 || !converted.at(op.imm[0]))
      continue;
    const auto &o = m.metadata["objects"][op.imm[0]];
    uint32_t n = o[1], d = o[2];
    std::vector<Id> args;
    if (op.imm[1] >= d) {
      args = {op.args[0],
              pack(std::vector<Id>(op.args.begin() + 1, op.args.end()))};
    } else {
      for (uint32_t col = 0; col <= op.imm[1]; ++col)
        args.push_back(pack(std::vector<Id>(op.args.begin() + col * n,
                                            op.args.begin() + (col + 1) * n)));
    }
    op.args = std::move(args);
  }
  m.ops.insert(m.ops.end(), added.begin(), added.end());
  std::vector<std::optional<Op>> defs(m.widths.size());
  for (const auto &op : m.ops)
    defs[op.out] = op;
  std::vector<uint8_t> state(defs.size());
  std::vector<Op> ordered;
  // Column packs retain precisely the original query dependencies, including
  // legal dependencies on earlier grants; they add no whole-matrix query.
  std::function<void(Id)> visit = [&](Id id) {
    if (!defs[id] || state[id] == 2)
      return;
    if (state[id] == 1)
      throw std::logic_error("packed matcher introduced a dependency cycle");
    state[id] = 1;
    for (Id a : defs[id]->args)
      visit(a);
    state[id] = 2;
    ordered.push_back(*defs[id]);
  };
  for (const auto &op : m.ops)
    visit(op.out);
  m.ops = std::move(ordered);
  if (recover_rows)
    regroup_bits(m);
  optimize_body(m);
  m.metadata["packed_matcher_summary"] = {{"rows", rows},
                                          {"request_bits", bits}};
}
} // namespace rds
