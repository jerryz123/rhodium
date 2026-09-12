// Shares unread payload storage across ordered queues and fixed valid pipelines while retaining cycle controls.
#include "model.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace rds {
void share_payload_lifetimes(Model &m,bool direct_storage,bool packed_counters,bool split_control,bool phase_index) {
  const auto original=m.ops;
  const auto objects=m.metadata["objects"];
  std::vector<const Op*> defs(m.widths.size());
  for(const auto &o:original)defs[o.out]=&o;
  auto canonical=[&](Id v){while(v!=none&&defs[v]&&defs[v]->code==1)v=defs[v]->args[0];return v;};
  auto query=[&](Id v,Id owner,unsigned kind){v=canonical(v);const auto *o=v==none?nullptr:defs[v];
    return o&&o->code==29&&o->args.empty()&&o->imm[0]==owner&&o->imm[1]==kind;};
  auto fixed_pipe=[&](Id i){const auto&o=objects[i];return o[0]==2&&o[3]==8&&o[2].get<unsigned>()<=64;};
  auto eligible=[&](Id i){const auto&o=objects[i];return (o[0]==1&&o[2]==1&&o[3]==0)||fixed_pipe(i);};
  std::vector<Id> next(objects.size(),none),previous(objects.size(),none);
  for(Id b=0;b<objects.size();++b)if(eligible(b)){
    Id payload=canonical(objects[b][4][2]);const auto *p=defs[payload];
    if(!p||p->code!=29||p->imm[1]!=3||!p->args.empty())continue;
    Id a=p->imm[0];
    if(a==b||!eligible(a)||fixed_pipe(a)!=fixed_pipe(b)||objects[a][1]!=objects[b][1]||canonical(objects[a][4][0])!=canonical(objects[b][4][0]))continue;
    if(!query(objects[b][4][1],a,2)||(!fixed_pipe(a)&&!query(objects[a][4][3],b,1)))continue;
    // Queues need reciprocal handshakes; valid-only pipelines always advance.
    if(next[a]!=none)continue;
    next[a]=b;previous[b]=a;
  }
  std::vector<std::vector<Id>> chains;
  for(Id first=0;first<objects.size();++first)if(previous[first]==none&&(next[first]!=none||fixed_pipe(first))){
    std::vector<Id> chain;for(Id i=first;i!=none&&chain.size()<=64;i=next[i])chain.push_back(i);
    if(chain.size()>64)continue;
    unsigned capacity=0;for(Id i:chain)capacity+=objects[i][2].get<unsigned>();
    if(capacity<2||capacity>64)continue;
    std::set<Id> members(chain.begin(),chain.end());
    bool safe=true;Id last=chain.back();
    auto payload_owner=[&](Id v){v=canonical(v);const auto *o=v==none?nullptr:defs[v];
      return o&&o->code==29&&o->imm[1]==3&&members.count(o->imm[0])?Id(o->imm[0]):none;};
    // Only a terminal valid-guarded mux may observe data outside forwarding.
    for(const auto&o:original)if(o.code!=1)for(size_t a=0;a<o.args.size();++a){
      Id owner=payload_owner(o.args[a]);if(owner==none)continue;
      bool guarded=o.code==15&&o.args.size()==3&&o.imm.size()==1&&o.imm[0]<=1&&
        owner==last&&query(o.args[0],last,2)&&a==(o.imm[0]==1?2u:1u);
      if(!guarded)safe=false;
    }
    for(Id i=0;i<objects.size();++i)for(size_t a=0;a<objects[i][4].size();++a){
      Id owner=payload_owner(objects[i][4][a]);if(owner!=none&&!(a==2&&next[owner]==i))safe=false;
    }
    auto observe=[&](Id v){if(payload_owner(v)!=none)safe=false;};
    for(const auto&p:m.metadata["ports"])observe(p[1]);
    for(const auto&r:m.metadata["registers"])for(const auto&v:r)observe(v);
    for(auto name:{"reads","writes"})for(const auto&r:m.metadata[name])for(size_t i=1;i<5;++i)observe(r[i]);
    for(const auto&r:m.metadata["assertions"])for(size_t i=0;i<3;++i)observe(r[i]);
    if(safe)chains.push_back(std::move(chain));
  }
  std::vector<Op> added;std::map<Id,Op> replacements;std::set<Id> removed;
  Json details=Json::array();
  auto emit=[&](unsigned code,unsigned width,std::vector<Id>args,std::vector<uint64_t>imm=std::vector<uint64_t>{}){
    Id out=m.widths.size();m.widths.push_back(width);added.push_back({code,out,std::move(args),std::move(imm)});return out;};
  for(const auto&chain:chains){
    unsigned n=0,width=objects[chain[0]][1];for(Id i:chain)n+=objects[i][2].get<unsigned>();
    Id first=chain.front(),last=chain.back();bool fixed=fixed_pipe(first);
    unsigned iw=1;while((1u<<iw)<n)++iw;
    bool phase=direct_storage&&fixed&&phase_index;
    bool packed_phase=phase&&packed_counters&&n+iw<=64;
    bool separate=direct_storage&&(split_control||phase)&&!packed_phase;
    bool counters=direct_storage&&!separate&&!phase&&packed_counters&&(n&(n-1))==0;
    unsigned control_width=direct_storage?n+(phase?iw:2*iw)+unsigned(counters):n;
    Id bank=m.widths.size();m.widths.push_back(separate?n:control_width);
    Id state=direct_storage&&!separate?emit(17,n,{bank},{0}):bank;
    Id zero=emit(0,n,{}, {0}),one=emit(0,n,{}, {1});
    Id push=objects[first][4][1],pop=fixed?emit(0,1,{}, {1}):objects[last][4][3].get<Id>(),reset=objects[first][4][0];
    Id low=emit(17,1,{state},{0}),high=emit(17,1,{state},{n-1});
    Id accept=emit(3,1,{push,emit(2,1,{low})}),depart=emit(3,1,{pop,high});
    Id incoming=emit(4,n,{emit(9,n,{state,one}),emit(18,n,{push})});
    Id enqueue=emit(3,n,{incoming,emit(2,n,{state})});
    Id ready=emit(4,n,{emit(2,n,{emit(10,n,{state,one})}),zero});
    // The top dequeue bit comes from egress ready, not the shifted zero.
    Id lower=emit(0,n,{}, {n==64?UINT64_MAX>>1:(UINT64_C(1)<<(n-1))-1});
    Id top=emit(9,n,{emit(18,n,{pop}),emit(0,n,{}, {n-1})});
    ready=emit(4,n,{emit(3,n,{ready,lower}),top});
    Id dequeue=emit(3,n,{state,ready});
    Id next_state=emit(3,n,{emit(4,n,{state,enqueue}),emit(2,n,{dequeue})});
    if(fixed){accept=push;depart=high;next_state=incoming;}
    // One live token occupies one row, from external enqueue through dequeue.
    // Ordered forwarding needs head/tail positions rather than per-hop handles.
    Id pool=none,head=none;
    if(direct_storage){
      unsigned tail_bit=n+iw+unsigned(counters);
      Id tail;
      if(separate){head=m.widths.size();m.widths.push_back(iw);tail=head;if(!phase){tail=m.widths.size();m.widths.push_back(iw);}}
      else {head=emit(17,iw,{bank},{n});tail=phase?head:emit(17,iw,{bank},{tail_bit});}
      Id increment=emit(0,iw,{}, {1}),last_index=emit(0,iw,{}, {n-1}),index_zero=emit(0,iw,{}, {0});
      auto advance=[&](Id position,Id enable){
        if(separate&&(n&(n-1))==0)return emit(6,iw,{position,emit(18,iw,{enable})});
        Id sum=emit(6,iw,{position,increment});
        if((n&(n-1))!=0)sum=emit(15,iw,{emit(12,1,{position,last_index}),sum,index_zero},{1});
        return emit(15,iw,{enable,position,sum},{1});};
      Id packed;
      if(separate){
        // Fixed latency makes the read and incoming write use one phase row.
        // Read old data before publication; invalid stages never observe holes.
        m.metadata["registers"].push_back({head,advance(head,phase?emit(0,1,{}, {1}):depart),reset,index_zero});
        if(!phase)m.metadata["registers"].push_back({tail,advance(tail,accept),reset,index_zero});
        packed=next_state;
      }else if(packed_phase){
        packed=emit(20,control_width,{next_state,advance(head,emit(0,1,{}, {1}))});
      }else if(counters){
        // A zero padding bit absorbs head overflow before it reaches tail.
        // Clear it after adding both enables; tail overflow truncates normally.
        Id gap=emit(0,1,{}, {0});
        Id positions=emit(20,control_width,{zero,head,gap,tail});
        Id delta=emit(20,control_width,{zero,emit(18,iw,{depart}),gap,emit(18,iw,{accept})});
        Id sum=emit(6,control_width,{positions,delta});
        packed=emit(20,control_width,{next_state,emit(17,iw,{sum},{n}),gap,emit(17,iw,{sum},{tail_bit})});
      }else packed=emit(20,control_width,{next_state,advance(head,depart),advance(tail,accept)});
      m.metadata["registers"].push_back({bank,packed,reset,emit(0,separate?n:control_width,{},std::vector<uint64_t>(((separate?n:control_width)+63)/64))});
      pool=m.metadata["memories"].size();m.metadata["memories"].push_back({width,n});
      m.metadata["writes"].push_back({pool,tail,objects[first][4][2],accept,none,0});
    }else{
      m.metadata["registers"].push_back({state,next_state,reset,zero});
      pool=m.metadata["objects"].size();
      m.metadata["objects"].push_back({1,width,n,fixed?2:0,{reset,accept,objects[first][4][2],depart},
        objects[first][5].get<std::string>()+"/payload-lifetime"});
    }
    std::map<Id,unsigned> position;unsigned offset=0;
    for(Id i:chain){position[i]=offset;offset+=objects[i][2].get<unsigned>();removed.insert(i);}
    for(const auto&o:original)if(o.code==29&&position.count(o.imm[0])){
      Id owner=o.imm[0];unsigned q=o.imm[1];
      if(q==3){replacements[o.out]=owner!=last?Op{0,o.out,{},std::vector<uint64_t>((width+63)/64)}:
        direct_storage?Op{24,o.out,{head},{pool}}:Op{29,o.out,{}, {pool,3}};}
      else if(fixed&&q==1){
        // Even VALID_ONLY's ready query exposes its explicit query argument.
        // Preserve that preview independently of the unconditional transition.
        unsigned depth=objects[owner][2];Id stages=emit(17,depth,{state},{position[owner]});
        Id full=emit(12,1,{stages,emit(0,depth,{}, {depth==64?UINT64_MAX:(UINT64_C(1)<<depth)-1})});
        replacements[o.out]={4,o.out,{o.args[0],emit(2,1,{full})},{}};
      }else {Id bit=emit(17,1,{state},{position[owner]+objects[owner][2].get<unsigned>()-1});replacements[o.out]={q==1?2u:1u,o.out,{bit},{}};}
    }
    details.push_back({{"objects",chain},{"payload_width",width},{"capacity",n},{"fixed_valid_pipeline",fixed},{"phase_index",phase},{"packed_phase",packed_phase},{"intermediate_payload_reads",0},{"direct_sram",direct_storage},{"packed_counters",counters},{"split_control",separate},{"control_bits",control_width}});
  }
  if(chains.empty()){m.metadata["payload_lifetime_summary"]={{"chains",details}};return;}
  Json kept=Json::array();std::vector<Id> remap(m.metadata["objects"].size(),none);
  for(Id i=0;i<remap.size();++i)if(!removed.count(i)){remap[i]=kept.size();kept.push_back(m.metadata["objects"][i]);}
  m.metadata["objects"]=std::move(kept);
  std::vector<Op> all=std::move(added);
  for(auto o:original){if(replacements.count(o.out))o=replacements.at(o.out);all.push_back(std::move(o));}
  for(auto&o:all)if(o.code==29)o.imm[0]=remap.at(o.imm[0]);
  std::vector<Id> producer(m.widths.size(),none);for(Id i=0;i<all.size();++i)producer[all[i].out]=i;
  std::vector<unsigned char> seen(all.size());m.ops.clear();
  std::function<void(Id)> visit=[&](Id i){if(i==none||seen[i]==2)return;if(seen[i]==1)throw std::runtime_error("payload lifetime cycle");seen[i]=1;
    for(Id a:all[i].args)visit(producer[a]);
    seen[i]=2;m.ops.push_back(std::move(all[i]));};
  for(Id i=0;i<all.size();++i)visit(i);
  m.metadata["payload_lifetime_summary"]={{"chains",details},{"policy","ordered token lifetime; retain stage controls; no intermediate payload reads"}};
  optimize_body(m);
}
}
