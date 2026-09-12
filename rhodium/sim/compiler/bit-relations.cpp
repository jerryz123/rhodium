// Proves Boolean equivalences through packed views and complete decoder columns without expanding the runtime graph.
#include "model.hpp"
#include <algorithm>
#include <map>
#include <tuple>
namespace rds {
namespace {
using Symbol=uint32_t;
struct Relations {
  const Model &m;
  std::vector<const Op*> defs;
  std::map<std::pair<Id,unsigned>,Symbol> bits;
  std::map<std::tuple<unsigned,Symbol,Symbol>,Symbol> nodes;
  std::map<Id,std::map<std::vector<uint64_t>,unsigned>> columns;
  uint64_t visits=0,column_rows=0;
  unsigned merged_columns=0;
  explicit Relations(const Model &model):m(model),defs(m.widths.size()) {for(const auto&o:m.ops)defs[o.out]=&o;}
  Symbol node(unsigned kind,Symbol a,Symbol b) {
    auto key=std::make_tuple(kind,a,b);auto [it,added]=nodes.emplace(key,0);
    if(added)it->second=2*nodes.size();
    return it->second;
  }
  Symbol leaf(Id v,unsigned b) {return node(0,v,b);}
  Symbol land(Symbol a,Symbol b) {
    if(!a||!b||(a^b)==1)return 0;
    if(a==1||a==b)return b;
    if(b==1)return a;
    if(a>b)std::swap(a,b);
    return node(3,a,b);
  }
  Symbol lor(Symbol a,Symbol b) {return land(a^1,b^1)^1;}
  Symbol lxor(Symbol a,Symbol b) {
    unsigned invert=(a^b)&1;a&=~1u;b&=~1u;
    if(a==b)return invert;
    if(!a)return b^invert;
    if(!b)return a^invert;
    if(a>b)std::swap(a,b);
    return node(5,a,b)^invert;
  }
  Symbol decoder(const Op &o,unsigned b) {
    unsigned count=o.imm[0],ow=(m.widths[o.out]+63)/64,iw=(m.widths[o.args[0]]+63)/64;
    if(column_rows+count+1>8000000)return leaf(o.out,b);
    column_rows+=count+1;
    std::vector<uint64_t> signature((uint64_t(count)+64)/64);
    auto add=[&](unsigned row,size_t offset){signature[row/64]|=((o.imm[offset+b/64]>>(b%64))&1)<<(row%64);};
    add(0,1);for(unsigned row=0;row<count;++row)add(row+1,1+ow+size_t(row)*(2*iw+ow)+2*iw);
    bool zero=true,one=true;for(unsigned row=0;row<=count;++row){bool bit=(signature[row/64]>>(row%64))&1;zero&=!bit;one&=bit;}
    if(zero||one)return one?1:0;
    auto [it,added]=columns[o.out].emplace(std::move(signature),b);merged_columns+=!added;
    return leaf(o.out,it->second);
  }
  Symbol get(Id v,unsigned b,unsigned fuel=128) {
    auto key=std::make_pair(v,b);auto found=bits.find(key);if(found!=bits.end())return found->second;
    if(!fuel||visits++>=4000000)return leaf(v,b);
    const Op *o=defs[v];Symbol result=leaf(v,b);
    auto child=[&](Id a,unsigned bit){return get(a,bit,fuel-1);};
    if(o)switch(o->code){
      case 0:result=(o->imm[b/64]>>(b%64))&1;break;
      case 1:result=child(o->args[0],b);break;
      case 17:result=child(o->args[0],b+o->imm[0]);break;
      case 18:case 19:{unsigned width=m.widths[o->args[0]];result=b<width?child(o->args[0],b):o->code==18?0:child(o->args[0],width-1);break;}
      case 20:{unsigned low=0;for(Id a:o->args){unsigned width=m.widths[a];if(b-low<width){result=child(a,b-low);break;}low+=width;}break;}
      case 2:result=child(o->args[0],b)^1;break;
      case 3:result=land(child(o->args[0],b),child(o->args[1],b));break;
      case 4:result=lor(child(o->args[0],b),child(o->args[1],b));break;
      case 5:result=lxor(child(o->args[0],b),child(o->args[1],b));break;
      case 25:result=decoder(*o,b);break;
      case 12:if(m.widths[o->args[0]]<=256){
        result=1;
        for(unsigned bit=0;bit<m.widths[o->args[0]]&&result;++bit)
          result=land(result,lxor(child(o->args[0],bit),child(o->args[1],bit))^1);
      }break;
      case 15:if(o->args.size()==3&&m.widths[o->args[0]]==1&&o->imm.size()==1&&o->imm[0]<=1){
        Symbol sel=child(o->args[0],0),a=child(o->args[1],b),z=child(o->args[2],b);
        if(o->imm[0]==0)sel^=1;
        result=a==z?a:lor(land(sel,z),land(sel^1,a));
      }break;
      default:break;
    }
    bits.emplace(key,result);return result;
  }
};
}
void canonicalize_bit_relations(Model &m) {
  Relations proof(m);std::map<Symbol,Id> available;unsigned aliases=0,constants=0;
  // Reuse only earlier scalar values. Word operations remain word operations;
  // symbolic bit nodes are compiler proof data, never emitted intermediates.
  for(const auto&p:m.metadata["ports"])if(p[0]==0&&m.widths[p[1].get<Id>()]==1){Id v=p[1];available.emplace(proof.get(v,0),v);}
  for(const auto&r:m.metadata["registers"])if(m.widths[r[0].get<Id>()]==1){Id v=r[0];available.emplace(proof.get(v,0),v);}
  std::vector<Op> result;result.reserve(m.ops.size());
  for(auto o:m.ops){if(m.widths[o.out]==1){Symbol symbol=proof.get(o.out,0);
    if(symbol<=1&&o.code!=0){o={0,o.out,{}, {symbol}};++constants;}
    else if(auto it=available.find(symbol);it!=available.end()&&o.code!=0){o={1,o.out,{it->second},{}};++aliases;}
    available.emplace(symbol,o.out);
  }result.push_back(std::move(o));}
  m.ops=std::move(result);
  m.metadata["bit_relation_summary"]={{"aliases",aliases},{"constants",constants},{"equal_decoder_columns",proof.merged_columns},{"proof_nodes",proof.nodes.size()},{"bit_visits",proof.visits},{"decoder_row_visits",proof.column_rows},{"runtime_nodes_added",0}};
  optimize_body(m);
}
}
