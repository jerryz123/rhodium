// Lifts closed contract output and transition cones into bounded local programs.
#include "kernel.hpp"
#include <algorithm>
#include <map>
#include <optional>
#include <set>
namespace rds {
void share_handshake_rows(Model &m) {
  using Terms=std::vector<std::pair<Id,Id>>;
  struct Row {Id output;unsigned bit;};
  std::map<Terms,std::vector<Row>> groups;
  std::map<Id,bool> zeros;
  for(const auto&o:m.ops)if(o.code==0)zeros[o.out]=std::all_of(o.imm.begin(),o.imm.end(),[](uint64_t w){return !w;});
  for(const auto&o:m.ops){
    if(o.code!=RDS_KERNEL_OPCODE||m.widths[o.out]!=1)continue;
    rds_kernel_format k;if(!rds_kernel_parse(o.imm.data(),o.imm.size(),&k))continue;
    auto def=[&](Id v)->const rds_kernel_instruction*{return v<k.inputs?nullptr:&k.ops[v-k.inputs];};
    auto zero=[&](Id v){
      if(v<k.inputs)return zeros.count(o.args[v])&&zeros.at(o.args[v]);
      const auto&p=*def(v);return p.code==0&&std::all_of(o.imm.begin()+p.imm,o.imm.begin()+p.imm+p.nimm,[](uint64_t w){return !w;});
    };
    const auto&last=k.ops[k.count-1];std::vector<Id> leaves;
    if(last.code==2){
      const auto*eq=def(k.args[last.args]);if(!eq||eq->code!=12)continue;
      Id a=k.args[eq->args],b=k.args[eq->args+1];
      const auto*pack=zero(a)?def(b):zero(b)?def(a):nullptr;if(!pack||pack->code!=20)continue;
      for(unsigned j=0;j<pack->nargs;++j)leaves.push_back(k.args[pack->args+j]);
    }else{
      // A scalar OR tree is the same positive reduction as nonzero(pack).
      // Follow only Boolean OR/copy nodes, preserving arbitrary grant masks.
      std::set<Id> visited;
      std::function<void(Id)> collect=[&](Id v){
        if(!visited.insert(v).second||zero(v))return;
        const auto*p=def(v);
        if(p&&k.widths[v]==1&&(p->code==4||p->code==1)){
          for(unsigned j=0;j<p->nargs;++j)collect(k.args[p->args+j]);
        }else leaves.push_back(v);
      };
      collect(k.inputs+k.count-1);
    }
    Terms terms;unsigned row=UINT32_MAX;bool good=true;
    for(Id v:leaves){
      if(zero(v))continue;
      const auto*both=def(v);if(!both||both->code!=3||k.widths[v]!=1){good=false;break;}
      bool found=false;
      for(unsigned side=0;side<2;++side){
        Id ready=k.args[both->args+side],bit=k.args[both->args+(side^1)];const auto*slice=def(bit);
        if(ready>=k.inputs||k.widths[ready]!=1||!slice||slice->code!=17||k.widths[bit]!=1)continue;
        Id grant=k.args[slice->args];unsigned r=o.imm[slice->imm];
        if(grant>=k.inputs||k.widths[grant]>64||(row!=UINT32_MAX&&row!=r))continue;
        row=r;terms.emplace_back(o.args[ready],o.args[grant]);found=true;break;
      }
      if(!found){good=false;break;}
    }
    if(!good||terms.size()<2)continue;
    std::sort(terms.begin(),terms.end());terms.erase(std::unique(terms.begin(),terms.end()),terms.end());
    groups[terms].push_back({o.out,row});
  }
  std::vector<Op> added;std::map<Id,Op> replacements;Json details=Json::array();
  auto emit=[&](unsigned code,unsigned width,std::vector<Id>args){Id out=m.widths.size();m.widths.push_back(width);added.push_back({code,out,std::move(args),{}});return out;};
  for(const auto&[terms,rows]:groups){
    if(rows.size()<2)continue;
    unsigned width=0;for(auto[ready,grant]:terms){(void)ready;width=std::max(width,m.widths[grant]);}
    Id mask=none;
    for(auto[ready,grant]:terms){
      if(m.widths[grant]<width)grant=emit(18,width,{grant});
      Id accepted=emit(3,width,{grant,emit(19,width,{ready})});
      mask=mask==none?accepted:emit(4,width,{mask,accepted});
    }
    Json outputs=Json::array();
    for(auto row:rows){replacements[row.output]={17,row.output,{mask},{row.bit}};outputs.push_back({row.output,row.bit});}
    details.push_back({{"terms",terms},{"width",width},{"outputs",outputs}});
  }
  m.metadata["handshake_row_summary"]={{"groups",details},{"outputs",replacements.size()},{"policy","share exact grant/ready pairs; retain every output bit"},{"provenance","term/output IDs refer to the pass input"}};
  if(replacements.empty())return;
  std::vector<Op> all;for(auto o:m.ops){if(replacements.count(o.out))o=replacements.at(o.out);all.push_back(std::move(o));}
  all.insert(all.end(),added.begin(),added.end());
  std::vector<Id> producer(m.widths.size(),none);for(Id i=0;i<all.size();++i)producer[all[i].out]=i;
  std::vector<unsigned char> seen(all.size());m.ops.clear();
  std::function<void(Id)> visit=[&](Id i){if(i==none||seen[i]==2)return;if(seen[i]==1)throw std::runtime_error("handshake sharing cycle");seen[i]=1;for(Id a:all[i].args)visit(producer[a]);seen[i]=2;m.ops.push_back(std::move(all[i]));};
  for(Id i=0;i<all.size();++i)visit(i);
  optimize_body(m);
}
void guard_contract_programs(Model &m) {
  // Candidate selection is heuristic; constant folding proves the fast path for
  // arbitrary remaining captures. No reachable-state or invalid-data assumption.
  std::vector<const Op*> producer(m.widths.size());
  for(const auto &o:m.ops)producer[o.out]=&o;
  auto matcher_derived=[&](Id root){
    std::vector<Id> todo{root};std::set<Id> seen;unsigned fuel=64;
    while(!todo.empty()&&fuel--){Id v=todo.back();todo.pop_back();
      if(!seen.insert(v).second)continue;
      const auto *p=producer[v];if(!p)continue;
      if(p->code==29){if(m.metadata["objects"][p->imm[0]][0]==10)return true;continue;}
      if(p->code==1||p->code==3||p->code==4||p->code==17||p->code==18||p->code==19||p->code==20)
        todo.insert(todo.end(),p->args.begin(),p->args.end());
    }
    return false;
  };
  Json details=Json::array();unsigned attempted=0,proofs=0;
  for(auto &o:m.ops){if(o.code!=RDS_KERNEL_OPCODE||o.imm[0]!=2||o.imm[2]<16)continue;
    Model original=kernel_model(m,o);std::vector<Id> candidates;
    for(Id i=0;i<o.args.size();++i){Id a=o.args[i];
      if(producer[a]&&producer[a]->code==0)continue;
      if(m.widths[a]==1||(m.widths[a]<=64&&matcher_derived(a)))candidates.push_back(i);}
    if(candidates.empty()||candidates.size()>64)continue;
    ++attempted;
    auto constant_when_zero=[&](const std::vector<Id>&inputs)->std::optional<std::vector<uint64_t>>{
      ++proofs;Model child=original;std::vector<Op> prefix;
      child.metadata["ports"]=Json::array();
      for(Id i=0;i<o.args.size();++i){
        if(std::find(inputs.begin(),inputs.end(),i)!=inputs.end())
          prefix.push_back({0,i,{},words(0,child.widths[i])});
        else child.metadata["ports"].push_back({0,i,"input"+std::to_string(i)});}
      child.metadata["ports"].push_back({1,child.ops.back().out,"result"});
      prefix.insert(prefix.end(),child.ops.begin(),child.ops.end());child.ops=std::move(prefix);
      optimize_body(child);Id result=child.metadata["ports"].back()[1];
      for(const auto &p:child.ops)if(p.out==result&&p.code==0)return p.imm;
      return std::nullopt;
    };
    auto literal=constant_when_zero(candidates);if(!literal)continue;
    // Drop unnecessary comparisons while preserving the proof. The remaining
    // inputs need not share a name, owner, or an assumed protocol invariant.
    for(size_t i=candidates.size();i-->0;){auto trial=candidates;trial.erase(trial.begin()+i);
      if(trial.empty())continue;
      if(auto value=constant_when_zero(trial)){candidates=std::move(trial);literal=std::move(value);}}
    if(candidates.size()>8||candidates.size()*4+4>=original.ops.size())continue;
    Model child=original;Id result=child.ops.back().out,guard=none;
    auto append=[&](unsigned code,unsigned width,std::vector<Id> args,std::vector<uint64_t> imm=std::vector<uint64_t>{}){
      Id v=child.widths.size();child.widths.push_back(width);child.ops.push_back({code,v,std::move(args),std::move(imm)});return v;};
    for(Id i:candidates){Id zero=append(0,child.widths[i],{},words(0,child.widths[i]));
      Id equal=append(12,1,{i,zero});guard=guard==none?equal:append(3,1,{guard,equal});}
    Id idle=append(0,m.widths[o.out],{},*literal);
    append(15,m.widths[o.out],{guard,result,idle},{1});
    std::vector<uint64_t> im{2,o.args.size(),child.ops.size()};
    im.insert(im.end(),child.widths.begin(),child.widths.end());
    for(const auto &p:child.ops){im.insert(im.end(),{p.code,p.args.size(),p.imm.size()});
      im.insert(im.end(),p.args.begin(),p.args.end());im.insert(im.end(),p.imm.begin(),p.imm.end());}
    rds_kernel_format format;if(!rds_kernel_parse(im.data(),im.size(),&format))continue;
    Op guarded=o;guarded.imm=std::move(im);kernel_model(m,guarded).validate();
    details.push_back({{"output",o.out},{"zero_inputs",candidates},{"idle_words",*literal},
      {"original_operations",original.ops.size()},{"guard_operations",child.ops.size()-original.ops.size()}});
    o.imm=std::move(guarded.imm);
  }
  m.metadata["contract_guard_summary"]={{"attempted",attempted},{"proofs",proofs},{"programs",details.size()},
    {"guards",details},{"provenance","output and zero_inputs describe this pass snapshot and local argument positions"}};
}
void optimize_contract_programs(Model &m) {
  uint64_t before=0,after=0,changed=0;
  for(auto &o:m.ops){if(o.code!=RDS_KERNEL_OPCODE)continue;
    auto child=kernel_model(m,o);before+=child.ops.size();
    child.metadata["ports"].push_back({1,child.ops.back().out,"result"});
    recover_words(child);regroup_bits(child);optimize_body(child);child.validate();
    // Re-encode by explicit input identity after local CSE and renumbering.
    std::map<Id,Id> local;Id result=none;
    std::vector<uint32_t> widths(o.args.size());
    for(const auto &p:child.metadata["ports"]){if(p[0]==1){result=p[1];continue;}
      const auto name=p[2].get<std::string>();unsigned input=std::stoul(name.substr(5));
      local[p[1].get<Id>()]=input;widths.at(input)=child.widths[p[1].get<Id>()];}
    std::vector<Op> ops;
    for(auto op:child.ops){Id out=local.size();for(auto &a:op.args)a=local.at(a);
      widths.push_back(child.widths[op.out]);local[op.out]=out;op.out=out;ops.push_back(std::move(op));}
    // A completely folded result or an input alias need not be the final node.
    Id output=local.at(result);
    if(ops.empty()||ops.back().out!=output){Id out=widths.size();widths.push_back(m.widths[o.out]);ops.push_back({1,out,{output},{}});}
    std::vector<uint64_t> im{o.imm[0],o.args.size(),ops.size()};im.insert(im.end(),widths.begin(),widths.end());
    for(const auto &op:ops){im.insert(im.end(),{op.code,op.args.size(),op.imm.size()});
      im.insert(im.end(),op.args.begin(),op.args.end());im.insert(im.end(),op.imm.begin(),op.imm.end());}
    rds_kernel_format format;
    if(!rds_kernel_parse(im.data(),im.size(),&format)){after+=o.imm[2];continue;}
    if(im!=o.imm){++changed;o.imm=std::move(im);}
    after+=o.imm[2];
  }
  m.metadata["contract_program_optimization"]={{"before",before},{"after",after},{"changed",changed},
    {"provenance","contract_kernel_summary local IDs describe the pre-optimization program"}};
}
void bundle_contract_controls(Model &m) {
  // Pack related observable controls only when every consumer can wait for the
  // last producer. Actual edges, including captured controls, decide legality.
  const size_t n=m.ops.size(),nv=m.widths.size();
  std::vector<Id> producer(nv,none),available(nv,none);
  std::vector<std::vector<Id>> users(nv);
  std::vector<bool> observed(nv),removed(n),bundled(nv);
  std::vector<std::vector<Op>> after(n);
  for(Id i=0;i<n;++i){producer[m.ops[i].out]=available[m.ops[i].out]=i;
    for(Id a:m.ops[i].args)users[a].push_back(i);}
  auto observe=[&](Id v){if(v!=none)observed.at(v)=true;};
  for(auto&p:m.metadata["ports"])observe(p[1]);
  for(auto&r:m.metadata["registers"])for(auto&v:r)observe(v);
  for(auto name:{"writes","reads"})for(auto&r:m.metadata[name])for(unsigned i=1;i<5;++i)observe(r[i]);
  for(auto&r:m.metadata["assertions"])for(unsigned i=0;i<3;++i)observe(r[i]);
  for(auto&o:m.metadata["objects"])for(auto&v:o[4])observe(v);
  auto total=[](uint32_t c){return c<32 && c!=16 && !(c>=21&&c<=25) && c!=29;};
  Json groups=Json::array();unsigned attempted=0,dependency_rejections=0;
  auto try_group=[&](const std::vector<Id>&roots,const std::set<Id>&boundary,
                     const Json &contract,bool commit){
    if(roots.size()<2)return false;
    if(!commit)++attempted;
    std::set<Id> outputs(roots.begin(),roots.end()),members;
    std::vector<Id> todo=roots;
    Id anchor=0;
    for(Id v:roots){Id p=producer[v];
      if(p==none||removed[p]||bundled[v]||!total(m.ops[p].code)||m.ops[p].code==0)return false;
      anchor=std::max(anchor,p);}
    while(!todo.empty() && members.size()<RDS_KERNEL_OPS){Id v=todo.back();todo.pop_back();Id p=producer[v];
      if(p==none||removed[p]||!total(m.ops[p].code)||
         (!outputs.count(v)&&(boundary.count(v)||observed[v]))||!members.insert(p).second)continue;
      todo.insert(todo.end(),m.ops[p].args.begin(),m.ops[p].args.end());}
    bool changed=true;
    while(changed){changed=false;
      for(auto it=members.begin();it!=members.end();){Id p=*it;
        bool escape=!outputs.count(m.ops[p].out)&&std::any_of(users[m.ops[p].out].begin(),users[m.ops[p].out].end(),[&](Id u){return !members.count(u);});
        if(escape){it=members.erase(it);changed=true;}else ++it;}}
    std::set<Id> reachable;todo=roots;
    while(!todo.empty()){Id p=producer[todo.back()];todo.pop_back();
      if(p==none||!members.count(p)||!reachable.insert(p).second)continue;
      todo.insert(todo.end(),m.ops[p].args.begin(),m.ops[p].args.end());}
    members=std::move(reachable);
    for(Id v:roots)if(!members.count(producer[v]))return false;
    if(members.size()<4 || members.size()+1>RDS_KERNEL_OPS)return false;
    std::set<Id> inputs;
    for(Id p:members){
      for(Id u:users[m.ops[p].out])if(!members.count(u)&&u<=anchor){if(!commit)++dependency_rejections;return false;}
      for(Id a:m.ops[p].args)if(!members.count(producer[a]))inputs.insert(a);}
    for(Id a:inputs)if(available[a]!=none&&available[a]>=anchor){if(!commit)++dependency_rejections;return false;}
    if(inputs.size()>RDS_KERNEL_INPUTS)return false;
    uint32_t width=0;for(Id v:roots)width+=m.widths[v];
    if(width>64)return false;
    Op program{RDS_KERNEL_OPCODE,Id(m.widths.size()),{inputs.begin(),inputs.end()},{2,inputs.size(),members.size()+1}};
    std::map<Id,Id> local;uint64_t storage=1,arguments=roots.size();
    for(Id a:inputs){local[a]=local.size();program.imm.push_back(m.widths[a]);storage+=(m.widths[a]+63)/64;}
    for(Id p:members){Id v=m.ops[p].out;local[v]=local.size();program.imm.push_back(m.widths[v]);storage+=(m.widths[v]+63)/64;arguments+=m.ops[p].args.size();}
    program.imm.push_back(width);
    if(storage>RDS_KERNEL_WORDS||arguments>RDS_KERNEL_ARGS)return false;
    if(!commit)return true;
    for(Id p:members){const auto&o=m.ops[p];program.imm.insert(program.imm.end(),{o.code,o.args.size(),o.imm.size()});
      for(Id a:o.args)program.imm.push_back(local.at(a));
      program.imm.insert(program.imm.end(),o.imm.begin(),o.imm.end());}
    program.imm.insert(program.imm.end(),{20,roots.size(),0});
    for(Id v:roots)program.imm.push_back(local.at(v));
    m.widths.push_back(width);kernel_model(m,program).validate();
    Json origins=Json::array();
    for(const auto &origin:m.metadata["origins"]){Id v=origin[0];
      if(v<nv&&members.count(producer[v])){auto copy=origin;copy[0]=program.out;origins.push_back(std::move(copy));}}
    for(auto &origin:origins)m.metadata["origins"].push_back(std::move(origin));
    after[anchor].push_back(std::move(program));
    uint64_t low=0;for(Id v:roots){after[anchor].push_back({17,v,{Id(m.widths.size()-1)},{low}});
      low+=m.widths[v];bundled[v]=true;available[v]=anchor;}
    for(Id p:members)removed[p]=true;
    groups.push_back({{"occurrence",contract[0]},{"kind",contract[1]},{"original_outputs",roots},
      {"inner_operations",members.size()+1},{"packed_bits",width},{"captures",inputs.size()}});
    return true;
  };
  for(const auto &c:m.metadata.value("contracts",Json::array())){
    std::set<Id> boundary,distinct;std::vector<Id> roots;
    for(const auto &b:c[2]){Id v=b[1];if(v==none)continue;boundary.insert(v);std::string label=b[0];
      bool output=label.rfind("out",0)==0&&label.size()>=6&&label.substr(label.size()-6)==".valid";
      bool ready=label.rfind("in",0)==0&&label.size()>=6&&label.substr(label.size()-6)==".ready";
      if((output||ready)&&m.widths[v]<=8&&!bundled[v]&&producer[v]!=none&&distinct.insert(v).second)roots.push_back(v);}
    std::sort(roots.begin(),roots.end(),[&](Id a,Id b){return producer[a]<producer[b];});
    for(size_t first=0;first+1<roots.size();){size_t accepted=0;
      for(size_t end=std::min(roots.size(),first+16);end>first+1;--end){
        std::vector<Id> group(roots.begin()+first,roots.begin()+end);
        if(try_group(group,boundary,c,false)){try_group(group,boundary,c,true);accepted=end;break;}}
      first=accepted?accepted:first+1;}
  }
  std::vector<Op> ops;
  for(Id i=0;i<n;++i){if(!removed[i])ops.push_back(std::move(m.ops[i]));
    for(auto &o:after[i])ops.push_back(std::move(o));}
  m.ops=std::move(ops);
  m.metadata["contract_control_summary"]={{"version",1},{"groups",groups},{"attempted",attempted},
    {"dependency_rejections",dependency_rejections},{"provenance_ids","original_outputs refer to the input snapshot"}};
  compact_graph(m);
}
void lift_contracts(Model &m) {
  const size_t n=m.ops.size();
  std::vector<Id> producer(m.widths.size(),none);
  std::vector<std::vector<Id>> users(m.widths.size());
  std::vector<std::vector<size_t>> origin_indices(m.widths.size());
  for(size_t i=0;i<m.metadata["origins"].size();++i)
    origin_indices[m.metadata["origins"][i][0].get<Id>()].push_back(i);
  std::vector<bool> observed(m.widths.size()),removed(n);
  for(Id i=0;i<n;++i){producer[m.ops[i].out]=i;for(Id a:m.ops[i].args)users[a].push_back(i);}
  auto observe=[&](Id v){if(v!=none)observed.at(v)=true;};
  struct Candidate { Id root; std::string kind,label; std::set<Id> boundary; };
  std::vector<Candidate> candidates;
  for(auto &p:m.metadata["ports"])observe(p[1]);
  for(auto &r:m.metadata["registers"]){for(auto &v:r)observe(v);candidates.push_back({r[1],"state-transition","next",{r[0]}});}
  for(auto name:{"writes","reads"})for(auto &r:m.metadata[name])for(unsigned i=1;i<5;++i)observe(r[i]);
  for(auto &r:m.metadata["assertions"])for(unsigned i=0;i<3;++i)observe(r[i]);
  for(auto &o:m.metadata["objects"])for(auto &v:o[4])observe(v);
  // Endpoint direction supplies roots, not a semantic replacement by name.
  // Captured sideband dependencies are discovered from the actual graph.
  auto contracts=m.metadata.value("contracts",Json::array());
  std::vector<Candidate> endpoints;
  for(const auto &c:contracts){std::set<Id> boundary;
    for(const auto &b:c[2])if(b[1]!=none)boundary.insert(b[1].get<Id>());
    for(const auto &b:c[2]){std::string label=b[0];Id v=b[1];if(v==none)continue;
      bool output=label.rfind("out",0)==0 && (label.find(".valid")!=std::string::npos || label.find(".bits")!=std::string::npos);
      bool ready=label.rfind("in",0)==0 && label.find(".ready")!=std::string::npos;
      if(output||ready){auto cut=boundary;cut.erase(v);endpoints.push_back({v,c[1],label,std::move(cut)});}
    }
  }
  candidates.insert(candidates.begin(),endpoints.begin(),endpoints.end());
  // Also lift exact update expressions of native composites. Their snapshot
  // queries and unique-owner publication remain explicit scheduling boundaries.
  for(const auto &o:m.metadata["objects"])for(size_t i=1;i<o[4].size();++i)
    if(o[4][i]!=none)candidates.push_back({o[4][i],"object-transition",std::to_string(i),{}});
  Json details=Json::array(),coverage=Json::object();std::set<Id> attempted;
  uint64_t inner=0,private_words=0;
  // Keep decoders in the surrounding backend, which owns their shared lookup
  // tables and decision trees. A generic local search would undo that lowering.
  auto total=[](uint32_t c){return c<32 && c!=16 && !(c>=21&&c<=25) && c!=29;};
  for(const auto &c:candidates){
    auto &counts=coverage[c.kind];if(counts.is_null())counts={{"roots",0},{"programs",0}};
    counts["roots"]=counts["roots"].get<unsigned>()+1;
    Id root=producer[c.root];if(root==none||removed[root]||!total(m.ops[root].code)||m.ops[root].code==0||!attempted.insert(c.root).second)continue;
    std::set<Id> members;std::vector<Id> todo{c.root};
    while(!todo.empty() && members.size()<RDS_KERNEL_OPS){Id v=todo.back();todo.pop_back();Id p=producer[v];
      if(p==none||removed[p]||!total(m.ops[p].code)||c.boundary.count(v)||(v!=c.root&&observed[v])||!members.insert(p).second)continue;
      todo.insert(todo.end(),m.ops[p].args.begin(),m.ops[p].args.end());
    }
    // Cut every escaping intermediate, then trim anything disconnected by that
    // cut. One root preserves the original topological insertion point, so no
    // false aggregate feedback or duplicated cone can be introduced.
    bool changed=true;
    while(changed){changed=false;
      for(auto it=members.begin();it!=members.end();){Id p=*it;bool escapes=p!=root&&std::any_of(users[m.ops[p].out].begin(),users[m.ops[p].out].end(),[&](Id u){return !members.count(u);});
        if(escapes){it=members.erase(it);changed=true;}else ++it;
      }
    }
    std::set<Id> reachable;todo={c.root};
    while(!todo.empty()){Id p=producer[todo.back()];todo.pop_back();if(p==none||!members.count(p)||!reachable.insert(p).second)continue;
      todo.insert(todo.end(),m.ops[p].args.begin(),m.ops[p].args.end());}
    members=std::move(reachable);if(members.size()<4)continue;
    std::set<Id> inputs;
    for(Id p:members)for(Id a:m.ops[p].args)if(!members.count(producer[a]))inputs.insert(a);
    if(inputs.size()>RDS_KERNEL_INPUTS)continue;
    Op program{RDS_KERNEL_OPCODE,c.root,{inputs.begin(),inputs.end()},{2,inputs.size(),members.size()}};
    std::map<Id,Id> local;uint64_t storage=0,arguments=0,eliminated=0;
    for(Id a:inputs){local[a]=local.size();program.imm.push_back(m.widths[a]);storage+=(m.widths[a]+63)/64;}
    for(Id p:members){Id v=m.ops[p].out;local[v]=local.size();program.imm.push_back(m.widths[v]);storage+=(m.widths[v]+63)/64;arguments+=m.ops[p].args.size();if(p!=root)eliminated+=(m.widths[v]+63)/64;}
    if(storage>RDS_KERNEL_WORDS||arguments>RDS_KERNEL_ARGS)continue;
    for(Id p:members){const auto &o=m.ops[p];program.imm.insert(program.imm.end(),{o.code,o.args.size(),o.imm.size()});
      for(Id a:o.args)program.imm.push_back(local.at(a));
      program.imm.insert(program.imm.end(),o.imm.begin(),o.imm.end());}
    kernel_model(m,program).validate();
    Json origins=Json::array();for(Id p:members)for(size_t i:origin_indices[m.ops[p].out])origins.push_back(m.metadata["origins"][i]);
    std::vector<Id> local_values(inputs.begin(),inputs.end());for(Id p:members)local_values.push_back(m.ops[p].out);
    details.push_back({{"kind",c.kind},{"endpoint",c.label},{"original_root",c.root},{"output_value",c.root},{"input_values",program.args},{"local_values",local_values},
      {"inner_operations",members.size()},{"private_words",eliminated},{"local_words",storage},{"origins",origins}});
    for(Id p:members)if(p!=root)removed[p]=true;
    m.ops[root]=std::move(program);inner+=members.size();private_words+=eliminated;
    counts["programs"]=counts["programs"].get<unsigned>()+1;
  }
  std::vector<Op> ops;for(Id i=0;i<n;++i)if(!removed[i])ops.push_back(std::move(m.ops[i]));m.ops=std::move(ops);
  m.metadata["contract_kernel_summary"]={{"version",1},{"programs",details.size()},{"inner_operations",inner},
    {"private_words",private_words},{"outer_operations_before",n},{"outer_operations_after",m.ops.size()},
    {"coverage",coverage},{"kernels",details},{"provenance_ids","local_values, input_values and original_root refer to the input snapshot; output_value is remapped"}};
  compact_graph(m);
  m.metadata["contract_kernel_summary"]["outer_operations_after"]=m.ops.size();
}
}
