// Proves zero upper payload bits through ordinary FIFO cycles and narrows storage and selection datapaths.
#include "model.hpp"
#include <algorithm>
namespace rds {
void narrow_fifo_payloads(Model&m){
 auto&objects=m.metadata["objects"];std::vector<Big> possible(m.widths.size()),fifo(objects.size());std::vector<const Op*>defs(m.widths.size());
 for(const auto&o:m.ops)defs[o.out]=&o;
 for(Id i=0;i<m.widths.size();++i)if(!defs[i])possible[i]=mask(m.widths[i]);
 auto ordinary=[&](size_t i){return objects[i][0]==1&&objects[i][3]==0;};
 for(size_t i=0;i<objects.size();++i)if(!ordinary(i))fifo[i]=mask(objects[i][1].get<unsigned>());
 // FIFO storage starts zero. All non-FIFO state and unsupported operations are
 // conservatively unknown. This ascending may-one fixed point is inductive
 // across every enqueue, including writes coincident with reset.
 bool changed=true;unsigned iterations=0;
 while(changed&&iterations<1024){changed=false;++iterations;
  for(const auto&o:m.ops){Big value=mask(m.widths[o.out]);auto a=[&](size_t n)->const Big&{return possible[o.args[n]];};
   switch(o.code){
   case 0:value=number(o.imm);break;
   case 1:case 18:value=a(0);break;
   case 3:value=a(0);for(size_t i=1;i<o.args.size();++i)value&=a(i);break;
   case 4:case 5:value=0;for(size_t i=0;i<o.args.size();++i)value|=a(i);break;
   case 15:value=a(1)|a(2);break;
   case 16:value=0;for(size_t i=1;i<o.args.size();++i)value|=a(i);break;
   case 17:value=a(0)>>o.imm[0];break;
   case 19:value=a(0);if((a(0)>>(m.widths[o.args[0]]-1))!=0)value|=mask(m.widths[o.out])^mask(m.widths[o.args[0]]);break;
   case 20:{value=0;unsigned offset=0;for(size_t i=0;i<o.args.size();++i){value|=a(i)<<offset;offset+=m.widths[o.args[i]];}break;}
   case 9:case 10:{auto c=defs[o.args[1]];if(c&&c->code==0){Big shift=number(c->imm);if(shift>=m.widths[o.out])value=0;else if(o.code==9)value=a(0)<<shift.convert_to<unsigned>();else value=a(0)>>shift.convert_to<unsigned>();}break;}
   case 29:if(o.imm[1]==3&&ordinary(o.imm[0]))value=fifo[o.imm[0]];break;
   default:break;
   }
   possible[o.out]=value&mask(m.widths[o.out]);
  }
  for(size_t i=0;i<objects.size();++i)if(ordinary(i)){Big next=fifo[i]|possible[objects[i][4][2].get<Id>()];if(next!=fifo[i]){fifo[i]=next;changed=true;}}
 }
 Json summary={{"converged",!changed},{"iterations",iterations},{"objects",Json::array()},{"saved_storage_bytes",0}};
 if(changed){m.metadata["fifo_width_summary"]=summary;return;}
 std::vector<unsigned> narrowed(objects.size());uint64_t saved=0;
 for(size_t i=0;i<objects.size();++i)if(ordinary(i)){unsigned old=objects[i][1],width=fifo[i]==0?1:boost::multiprecision::msb(fifo[i])+1;
  if(width<old){narrowed[i]=width;summary["objects"].push_back({{"name",objects[i][5]},{"old_width",old},{"width",width}});saved+=uint64_t((old+63)/64-(width+63)/64)*8*(objects[i][2].get<unsigned>()+1);objects[i][1]=width;}}
 std::vector<Op> rewritten;rewritten.reserve(m.ops.size()+objects.size());unsigned selections=0;uint64_t removed_bits=0;
 for(auto o:m.ops){
  if(o.code==29&&o.imm[1]==3&&narrowed[o.imm[0]]){Id old=o.out;o.out=m.widths.size();m.widths.push_back(narrowed[o.imm[0]]);rewritten.push_back(o);rewritten.push_back({18,old,{o.out},{}});}
  else if(o.code==15||o.code==16){
   unsigned width=possible[o.out]==0?1:boost::multiprecision::msb(possible[o.out])+1;
   if(width>=m.widths[o.out]){rewritten.push_back(o);continue;}
   // Zero extension commutes with selection. The fixed point proves every
   // arm's upper bits zero, including empty FIFO previews and reset writes.
   // Keep the original selector and immediate fields, hence the same invalid
   // selector behavior; preserve wide observers with one final extension.
   ++selections;removed_bits+=m.widths[o.out]-width;
   for(size_t a=1;a<o.args.size();++a){Id low=m.widths.size();m.widths.push_back(width);rewritten.push_back({17,low,{o.args[a]},{0}});o.args[a]=low;}
   Id old=o.out;o.out=m.widths.size();m.widths.push_back(width);rewritten.push_back(o);rewritten.push_back({18,old,{o.out},{}});
  }else rewritten.push_back(o);
 }
 for(size_t i=0;i<objects.size();++i)if(narrowed[i]){Id low=m.widths.size();m.widths.push_back(narrowed[i]);rewritten.push_back({17,low,{objects[i][4][2].get<Id>()},{0}});objects[i][4][2]=low;}
 m.ops=std::move(rewritten);summary["saved_storage_bytes"]=saved;summary["narrowed_selections"]=selections;summary["selection_bits_removed"]=removed_bits;m.metadata["fifo_width_summary"]=summary;m.validate();optimize_body(m);
}
} // namespace rds
