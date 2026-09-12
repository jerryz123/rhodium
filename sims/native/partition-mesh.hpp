// Composes buffered XY routers in standalone IR and independently models their transfers.
#pragma once

static unsigned neighbor(unsigned node,unsigned port,unsigned side){
 unsigned x=node%side,y=node/side;
 if(port==1&&x+1<side)return node+1;
 if(port==2&&x)return node-1;
 if(port==3&&y)return node-side;
 if(port==4&&y+1<side)return node+side;
 return none;
}
static unsigned mesh_route(unsigned node,unsigned dest,unsigned side){
 return dest%side>node%side?1:dest%side<node%side?2:dest/side<node/side?3:dest/side>node/side?4:0;
}
#ifndef RDS_MESH_SOFTWARE_ONLY
static void order_fixture(Model&m){
 std::vector<Id> defs(m.widths.size(),none);for(Id i=0;i<m.ops.size();++i){require(defs[m.ops[i].out]==none,"duplicate fixture definition");defs[m.ops[i].out]=i;}
 std::vector<unsigned char> seen(m.ops.size());std::vector<Op> ordered;
 std::function<void(Id)> visit=[&](Id i){if(i==none||seen[i]==2)return;require(!seen[i],"combinational mesh cycle");seen[i]=1;for(Id a:m.ops[i].args)visit(defs[a]);seen[i]=2;ordered.push_back(m.ops[i]);};
 for(Id i=0;i<m.ops.size();++i)visit(i);m.ops=std::move(ordered);
}
static Model mesh_fixture(unsigned side){
 require(side>=2&&side<=16&&!(side&(side-1)),"mesh side must be 2, 4, 8 or 16");
 Builder b;unsigned count=side*side;Id reset=b.value(1),traffic=b.value(2),zero=b.lit(0),one=b.lit(1),no=b.lit(0,1);
 b.m.metadata["ports"]={{0,reset,"reset"},{0,traffic,"traffic"}};
 b.m.metadata["mesh"]={{"side",side},{"router","five-port XY, ordinary depth-two input FIFOs, per-output round-robin"},{"payload_bits",240}};
 struct Node{std::array<Id,5> valid{},data{},ready{},out{},fire{};Id seq,held,tick,cfg,received,hash;};
 std::vector<Node> nodes(count);
 // One construction rule, distinct physical state. Deterministic unicast makes
 // request columns disjoint, so greedy taken masks are identically redundant.
 for(unsigned n=0;n<count;++n){auto&x=nodes[n];x.seq=b.value(64);x.held=b.value(1);x.tick=b.value(64);x.cfg=b.value(2);x.received=b.value(64);x.hash=b.value(64);
  for(unsigned p=0;p<5;++p){x.valid[p]=b.query(n*10+p,2);x.data[p]=b.query(n*10+p,3,240);x.ready[p]=b.query(n*10+p,1);x.out[p]=b.value(240);x.fire[p]=b.value(1);}
 }
 constexpr unsigned opposite[5]={0,2,1,4,3};
 for(unsigned n=0;n<count;++n){auto&x=nodes[n];std::string path="mesh/node"+std::to_string(n);std::array<Id,5> routes{},grants{},outready{},pop{};
  auto reg=[&](Id q,Id d,Id initial){b.m.metadata["registers"].push_back({q,d,reset,initial});};
  auto drive=[&](Id q,Id d){b.m.ops.push_back({1,q,{d},{}});};
  reg(x.tick,b.bin(6,x.tick,one),zero);reg(x.cfg,traffic,traffic);
  Id phase=b.bin(6,x.tick,b.lit(n)),pattern=b.mux(b.op(12,1,{x.cfg,b.lit(2,2)}),b.lit(1,1),b.land(b.op(12,1,{x.cfg,b.lit(1,2)}),b.op(12,1,{b.bin(3,phase,b.lit(31)),zero})));
  Id sourcefire=b.land(x.held,x.ready[0]);reg(x.seq,b.mux(sourcefire,b.bin(6,x.seq,one),x.seq),zero);reg(x.held,b.op(4,1,{b.land(x.held,b.op(2,1,{x.ready[0]})),pattern}),no);
  Id destination=b.bin(3,b.bin(6,x.seq,b.lit(n+1)),b.lit(count-1));destination=b.mux(b.op(12,1,{destination,b.lit(n)}),b.lit((n+1)&(count-1)),destination);
  Id packet=b.op(18,240,{b.op(20,96,{x.seq,b.lit(n,16),b.op(17,16,{destination},{0})})});
  for(unsigned p=0;p<5;++p){Id dest=b.op(17,16,{x.data[p]},{80}),dx=b.bin(3,dest,b.lit(side-1,16)),dy=b.bin(10,dest,b.lit(__builtin_ctz(side),16));
   routes[p]=b.mux(b.op(13,1,{b.lit(n%side,16),dx}),b.lit(1,3),b.mux(b.op(13,1,{dx,b.lit(n%side,16)}),b.lit(2,3),b.mux(b.op(13,1,{dy,b.lit(n/side,16)}),b.lit(3,3),b.mux(b.op(13,1,{b.lit(n/side,16),dy}),b.lit(4,3),b.lit(0,3)))));
  }
  std::array<Id,5> request{};
  for(unsigned p=0;p<5;++p){unsigned peer=neighbor(n,p,side);outready[p]=p?peer==none?no:nodes[peer].ready[opposite[p]]:b.op(2,1,{b.op(12,1,{b.bin(3,phase,b.lit(7)),zero})});
   std::vector<Id> bits;for(unsigned i=0;i<5;++i)bits.push_back(b.land(x.valid[i],b.op(12,1,{routes[i],b.lit(p,3)})));request[p]=b.op(20,5,bits);grants[p]=b.query(n*10+5+p,0,5,{request[p]});
   Id any=b.op(2,1,{b.op(12,1,{grants[p],b.lit(0,5)})});std::vector<Id> select{b.mux(any,grants[p],b.lit(1,5))};select.insert(select.end(),x.data.begin(),x.data.end());drive(x.out[p],b.op(16,240,select));drive(x.fire[p],b.land(any,outready[p]));
  }
  for(unsigned i=0;i<5;++i){pop[i]=no;for(unsigned p=0;p<5;++p)pop[i]=b.op(4,1,{pop[i],b.land(b.op(17,1,{grants[p]},{i}),outready[p])});unsigned peer=neighbor(n,i,side);
   Id incoming=i?peer==none?no:nodes[peer].fire[opposite[i]]:x.held;Id payload=i?peer==none?b.op(18,240,{zero}):nodes[peer].out[opposite[i]]:packet;
   b.m.metadata["objects"].push_back({1,240,2,0,{reset,incoming,payload,pop[i]},path+"/input"+std::to_string(i)});
  }
  for(unsigned p=0;p<5;++p)b.m.metadata["objects"].push_back({10,5,1,32,{reset,request[p],x.fire[p]},path+"/arbiter"+std::to_string(p)});
  reg(x.received,b.bin(6,x.received,b.op(18,64,{x.fire[0]})),zero);
  Id seq=b.op(17,64,{x.out[0]},{0}),src=b.op(18,64,{b.op(17,16,{x.out[0]},{64})});
  reg(x.hash,b.mux(x.fire[0],b.bin(5,b.bin(5,b.bin(8,x.hash,b.lit(UINT64_C(0x9e3779b185ebca87))),seq),src),x.hash),zero);
  b.out(x.seq,path+"/sent");b.out(x.received,path+"/received");b.out(x.hash,path+"/hash");b.out(x.fire[0],path+"/fire");b.out(b.mux(x.fire[0],x.out[0],b.op(18,240,{zero})),path+"/packet");
 }
 order_fixture(b.m);b.m.validate();optimize_body(b.m);recover_words(b.m);regroup_bits(b.m);optimize_body(b.m);release_body(b.m);b.m.validate();return b.m;
}
#endif

struct MeshSoftware {
 struct Packet{uint64_t seq=0;unsigned src=0,dest=0;};
 struct Fifo{std::array<Packet,2> data{};unsigned head=0,count=0;};
 struct Node{std::array<Fifo,5> fifo{};std::array<unsigned,5> priority{};uint64_t seq=0,received=0,hash=0,tick=0;unsigned cfg=0;bool held=false;};
 struct Transfer{int winner=-1;bool fire=false;Packet data{};};
 unsigned side;std::vector<Node> nodes;std::vector<std::array<Transfer,5>> transfer;
 explicit MeshSoftware(unsigned s):side(s),nodes(s*s),transfer(s*s){}
 void eval(){constexpr unsigned opposite[5]={0,2,1,4,3};for(unsigned n=0;n<nodes.size();++n)for(unsigned p=0;p<5;++p){auto&x=nodes[n];auto&t=transfer[n][p];t={};t.data=x.fifo[0].data[x.fifo[0].head];
  for(unsigned k=0;k<5;++k){unsigned i=(x.priority[p]+k)%5;auto&q=x.fifo[i];if(q.count&&mesh_route(n,q.data[q.head].dest,side)==p){t.winner=i;t.data=q.data[q.head];break;}}
  unsigned peer=neighbor(n,p,side);bool ready=p?peer!=none&&nodes[peer].fifo[opposite[p]].count<2:((x.tick+n)&7)!=0;t.fire=t.winner>=0&&ready;
 }}
 std::vector<uint64_t> outputs(){eval();std::vector<uint64_t> out;for(unsigned n=0;n<nodes.size();++n){const auto&x=nodes[n];const auto&t=transfer[n][0];out.insert(out.end(),{x.seq,x.received,x.hash,uint64_t(t.fire),t.fire?t.data.seq:0,t.fire?uint64_t(t.data.src)|(uint64_t(t.data.dest)<<16):0,0,0});}return out;}
 void step(bool reset,unsigned traffic){eval();constexpr unsigned opposite[5]={0,2,1,4,3};for(unsigned n=0;n<nodes.size();++n){auto&x=nodes[n];bool sent=x.held&&x.fifo[0].count<2;unsigned dest=(x.seq+n+1)&(nodes.size()-1);if(dest==n)dest=(n+1)&(nodes.size()-1);Packet packet{x.seq,n,dest};
  bool nextheld=(x.held&&x.fifo[0].count==2)||x.cfg==2||(x.cfg==1&&((x.tick+n)&31)==0);
  if(sent)++x.seq;auto&t=transfer[n][0];if(t.fire){++x.received;x.hash=x.hash*UINT64_C(0x9e3779b185ebca87)^t.data.seq^t.data.src;}
  for(unsigned i=0;i<5;++i){auto&q=x.fifo[i];bool pop=false;for(auto&t:transfer[n])pop|=t.fire&&t.winner==int(i);unsigned peer=neighbor(n,i,side);bool push=i?peer!=none&&transfer[peer][opposite[i]].fire:sent;Packet payload=i&&peer!=none?transfer[peer][opposite[i]].data:packet;
   if(push)q.data[(q.head+q.count)&1]=payload;if(pop)q.head^=1;q.count+=unsigned(push)-unsigned(pop);if(reset)q.head=q.count=0;
  }
  for(unsigned p=0;p<5;++p){if(transfer[n][p].fire)x.priority[p]=(transfer[n][p].winner+1)%5;if(reset)x.priority[p]=0;}
  x.held=reset?false:nextheld;x.tick=reset?0:x.tick+1;x.cfg=traffic;if(reset)x.seq=x.received=x.hash=0;
 }}
};
