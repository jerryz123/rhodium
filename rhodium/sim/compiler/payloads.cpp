// Replaces bounded FIFO payload forwarding islands with handles into snapshot SRAM.
#include "model.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <vector>
namespace rds {
void pool_payloads(Model &m,unsigned limit) {
  if(limit<2||limit>31)throw std::runtime_error("payload pool group limit must be 2..31");
  const auto original=m.ops;const auto objects=m.metadata["objects"];
  std::vector<const Op*> defs(m.widths.size());for(const auto&o:original)defs[o.out]=&o;
  auto eligible=[&](Id i){const auto&o=objects[i];return o[0]==1&&o[2]==1&&o[3]==0&&o[1].get<unsigned>()>=128;};
  std::vector<std::set<Id>> edges(objects.size());
  std::function<void(Id,Id,std::set<Id>&)> sources=[&](Id owner,Id v,std::set<Id>&seen){
    if(!seen.insert(v).second||!defs[v])return;
    const auto&o=*defs[v];
    if(o.code==29&&o.imm[1]==3&&o.args.empty()){Id source=o.imm[0];
      if(eligible(source)&&objects[source][1]==objects[owner][1]){edges[owner].insert(source);edges[source].insert(owner);}}
    if(o.code==1)sources(owner,o.args[0],seen);
    if(o.code==15||o.code==16)for(size_t i=1;i<o.args.size();++i)sources(owner,o.args[i],seen);
  };
  for(Id i=0;i<objects.size();++i)if(eligible(i)){std::set<Id> seen;sources(i,objects[i][4][2],seen);}
  std::vector<bool> assigned(objects.size());std::vector<std::vector<Id>> groups;
  for(Id seed=0;seed<objects.size();++seed)if(eligible(seed)&&!assigned[seed]&&!edges[seed].empty()){
    std::vector<Id> group,todo{seed};std::set<Id> seen;
    for(size_t cursor=0;cursor<todo.size()&&group.size()<limit;++cursor){Id v=todo[cursor];
      if(assigned[v]||!seen.insert(v).second)continue;
      assigned[v]=true;group.push_back(v);
      todo.insert(todo.end(),edges[v].begin(),edges[v].end());}
    if(group.size()>1)groups.push_back(std::move(group));
  }
  std::vector<Op> prefix,extra;std::map<Id,std::pair<Id,Id>> reads;
  auto emit=[&](std::vector<Op>&ops,unsigned code,unsigned width,std::vector<Id> args,std::vector<uint64_t> im=std::vector<uint64_t>{}){
    Id id=m.widths.size();m.widths.push_back(width);ops.push_back({code,id,std::move(args),std::move(im)});return id;};
  Json details=Json::array();
  for(const auto &group:groups){std::set<Id> members(group.begin(),group.end());
    std::map<Id,bool> representable;
    std::function<bool(Id)> can=[&](Id v){if(representable.count(v))return representable[v];bool yes=false;
      if(defs[v]){const auto&o=*defs[v];
        if(o.code==0)yes=std::all_of(o.imm.begin(),o.imm.end(),[](uint64_t x){return x==0;});
        if(o.code==29)yes=o.imm[1]==3&&o.args.empty()&&members.count(o.imm[0]);
        if(o.code==1)yes=can(o.args[0]);
        if(o.code==15||o.code==16){yes=true;for(size_t i=1;i<o.args.size();++i)yes&=can(o.args[i]);}}
      return representable[v]=yes;};
    unsigned fresh=0;for(Id id:group)fresh+=!can(objects[id][4][2]);
    if(!fresh)continue;
    unsigned capacity=group.size()+fresh+1,hw=1,width=objects[group[0]][1];
    while((1u<<hw)<capacity)++hw;
    Id memory=m.metadata["memories"].size();m.metadata["memories"].push_back({width,capacity});
    Id zero=emit(prefix,0,hw,{}, {0}),one=emit(prefix,0,capacity,{}, {1});
    std::map<Id,Id> handles;
    for(Id id:group){handles[id]=emit(prefix,29,hw,{}, {id,3});m.metadata["objects"][id][1]=hw;}
    Id live=one;
    for(Id id:group)live=emit(prefix,4,capacity,{live,emit(prefix,9,capacity,{one,handles[id]})});
    Id free=emit(prefix,2,capacity,{live});
    // All current handles, including invalid previews, retain their slots.
    // Reserve one fresh slot for each boundary writer before publication.
    std::map<Id,Id> lowered;
    std::function<Id(Id)> lower=[&](Id v)->Id{if(lowered.count(v))return lowered[v];const auto&o=*defs[v];Id result=none;
      if(o.code==0)result=zero;
      if(o.code==29)result=handles.at(o.imm[0]);
      if(o.code==1)result=lower(o.args[0]);
      if(o.code==15||o.code==16){std::vector<Id> args{o.args[0]};for(size_t i=1;i<o.args.size();++i)args.push_back(lower(o.args[i]));result=emit(extra,o.code,hw,std::move(args),o.imm);}
      return lowered[v]=result;};
    for(Id id:group){Id data=objects[id][4][2],handle;
      if(can(data))handle=lower(data);
      else{
        Id negative=emit(prefix,6,capacity,{emit(prefix,2,capacity,{free}),one});
        Id grant=emit(prefix,3,capacity,{free,negative});std::vector<Id> choices{grant};
        for(unsigned i=0;i<capacity;++i)choices.push_back(emit(prefix,0,hw,{}, {i}));
        handle=emit(prefix,16,hw,std::move(choices));free=emit(prefix,5,capacity,{free,grant});
        Id ready=emit(prefix,29,1,{}, {id,1});
        Id enable=emit(extra,3,1,{ready,objects[id][4][1]});
        m.metadata["writes"].push_back({memory,handle,data,enable,none,0});
      }
      m.metadata["objects"][id][4][2]=handle;
      reads[id]={memory,handles[id]};
    }
    details.push_back({{"objects",group},{"payload_width",width},{"slots",capacity},{"handle_width",hw},{"boundary_writers",fresh}});
  }
  std::vector<Op> ops=std::move(prefix);
  for(auto o:original){if(o.code==29&&o.imm[1]==3&&o.args.empty()&&reads.count(o.imm[0])){
      auto [memory,handle]=reads.at(o.imm[0]);o={24,o.out,{handle},{memory}};}
    ops.push_back(std::move(o));}
  ops.insert(ops.end(),extra.begin(),extra.end());
  std::vector<Id> producer(m.widths.size(),none);for(Id i=0;i<ops.size();++i)producer[ops[i].out]=i;
  std::vector<unsigned char> state(ops.size());std::vector<Op> ordered;
  std::function<void(Id)> visit=[&](Id i){if(i==none||state[i]==2)return;if(state[i]==1)throw std::runtime_error("payload pool introduced a combinational cycle");state[i]=1;
    for(Id a:ops[i].args)visit(producer[a]);
    state[i]=2;ordered.push_back(std::move(ops[i]));};
  for(Id i=0;i<ops.size();++i)visit(i);
  m.ops=std::move(ordered);
  m.metadata["payload_pool_summary"]={{"groups",details},{"group_limit",limit},{"allocation","bounded current-handle census; slot zero reserved"}};
  optimize_body(m);
}
}
