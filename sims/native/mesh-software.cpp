// Benchmarks cycle-exact NoC software models with offer snapshots and static 2x2 regions.
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <nlohmann/json.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>
#include <immintrin.h>
using Json=nlohmann::json;
static constexpr unsigned none=UINT32_MAX;
static void require(bool ok,const std::string&s){if(!ok)throw std::runtime_error(s);}
#define RDS_MESH_SOFTWARE_ONLY
#include "partition-mesh.hpp"

template<unsigned Side,bool Functional,unsigned Borrow> struct FastMesh {
 static constexpr unsigned Count=Side*Side;
 struct Full{uint64_t seq=0;uint16_t src=0,dest=0;};
 struct Perf{uint16_t dest=0;};
 using Packet=std::conditional_t<Functional,Full,Perf>;
 struct Slot{Packet packet{};uint8_t route=0;};
 struct Queue{Slot data[2]{};uint8_t head=0,count=0;};
 struct alignas(64) Node{Queue fifo[5]{};uint8_t priority[5]{};bool held=false;uint64_t seq=0,received=0,hash=0;};
 using Offer=std::conditional_t<Borrow==2,uint8_t,std::conditional_t<Borrow==1,const Packet*,Packet>>;
 struct alignas(64) Snapshot{Offer offers[5]{};uint8_t winner[5]{},valid=0,ready=0;};
 struct alignas(64) Epoch{std::atomic<uint64_t> value{0};};
 std::array<Node,Count> nodes{};
 std::array<std::array<Snapshot,Count>,2> snapshots{};
 std::array<std::array<unsigned,5>,Count> peers{};
 std::array<Epoch,8> progress{},done{};
 std::array<uint32_t,8> neighbors{};
 std::atomic<uint64_t> command{0};std::vector<std::thread> threads;
 unsigned workers,bank=0,cfg=0;uint64_t tick=0,total=0;
 bool tiled,local,unroll,stop=false,job_reset=false;unsigned job_traffic=0;uint64_t job_cycles=0;
 static constexpr unsigned opposite[5]={0,2,1,4,3};
 FastMesh(unsigned w,bool tile,bool neighbor_sync,bool two):workers(w),tiled(tile),local(neighbor_sync),unroll(two){
  require(w==1||w==2||w==4||w==8,"workers must be 1, 2, 4 or 8");require(Count%(w*(tiled?4:1))==0,"workers exceed static region count");
  for(unsigned n=0;n<Count;++n){for(unsigned p=0;p<5;++p)peers[n][p]=neighbor(n,p,Side);publish(n,0);}
  for(unsigned n=0;n<Count;++n)for(unsigned p=1;p<5;++p)if(peers[n][p]!=none){unsigned a=owner(n),b=owner(peers[n][p]);if(a!=b)neighbors[a]|=1u<<b;}
  for(unsigned id=1;id<workers;++id)threads.emplace_back([this,id]{uint64_t last=0;for(;;){uint64_t next;while((next=command.load(std::memory_order_acquire))==last)_mm_pause();if(stop)return;execute(id);last=next;done[id].value.store(last,std::memory_order_release);}});
 }
 ~FastMesh(){stop=true;command.fetch_add(1,std::memory_order_release);for(auto&t:threads)t.join();}
 unsigned owner(unsigned n)const{unsigned index=tiled?((n/Side/2)*(Side/2)+(n%Side)/2)*4+(n/Side%2)*2+n%2:n;return index/(Count/workers);}
 unsigned route(unsigned n,unsigned dest)const{return dest%Side>n%Side?1:dest%Side<n%Side?2:dest/Side<n/Side?3:dest/Side>n/Side?4:0;}
 const Packet& offered(unsigned n,const Snapshot&s,unsigned p)const{if constexpr(Borrow==2){unsigned index=s.offers[p];return nodes[n].fifo[index>>1].data[index&1].packet;}else if constexpr(Borrow==1)return *s.offers[p];else return s.offers[p];}
 void publish(unsigned n,unsigned b){auto&x=nodes[n];auto&s=snapshots[b][n];unsigned requests[5]{};s.ready=s.valid=0;
  for(unsigned i=0;i<5;++i){const auto&q=x.fifo[i];s.ready|=(q.count<2)<<i;if(q.count)requests[q.data[q.head].route]|=1u<<i;}
  for(unsigned p=0;p<5;++p)if(unsigned request=requests[p]){unsigned high=request&(31u<<x.priority[p]);unsigned winner=__builtin_ctz(high?high:request);s.valid|=1u<<p;s.winner[p]=winner;
   const auto&q=x.fifo[winner];if constexpr(Borrow==2)s.offers[p]=winner*2+q.head;else if constexpr(Borrow==1)s.offers[p]=&q.data[q.head].packet;else s.offers[p]=q.data[q.head].packet;
  }
 }
 void transition(unsigned n,unsigned b,bool reset,unsigned configuration,uint64_t time){auto&x=nodes[n];const auto&s=snapshots[b][n];unsigned pop=0;
  // Offers are independent of downstream readiness for deterministic unicast.
  for(unsigned p=0;p<5;++p)if(s.valid>>p&1){unsigned peer=peers[n][p];bool ready=p?peer!=none&&(snapshots[b][peer].ready>>opposite[p]&1):((time+n)&7)!=0;
   if(ready){unsigned winner=s.winner[p];pop|=1u<<winner;x.priority[p]=winner==4?0:winner+1;
    if(!p){++x.received;if constexpr(Functional){const auto&v=offered(n,s,0);x.hash=x.hash*UINT64_C(0x9e3779b185ebca87)^v.seq^v.src;}}
   }
  }
  bool source=x.held&&(s.ready&1);bool held=(x.held&&!(s.ready&1))||configuration==2||(configuration==1&&((time+n)&31)==0);
  for(unsigned i=0;i<5;++i){auto&q=x.fifo[i];unsigned peer=peers[n][i];bool push=i?peer!=none&&(s.ready>>i&1)&&(snapshots[b][peer].valid>>opposite[i]&1):source;
   if(push){Packet packet;if(i)packet=offered(peer,snapshots[b][peer],opposite[i]);else{unsigned dest=(x.seq+n+1)&(Count-1);if(dest==n)dest=(n+1)&(Count-1);packet.dest=dest;if constexpr(Functional){packet.seq=x.seq;packet.src=n;}}
    // An ordinary queue never overwrites an offered old head in this edge:
    // full queues cannot enqueue; a nonempty nonfull queue writes its tail.
    auto&slot=q.data[(q.head+q.count)&1];slot.packet=packet;slot.route=route(n,packet.dest);
   }
   unsigned take=pop>>i&1;q.head^=take;q.count+=unsigned(push)-take;if(reset)q.head=q.count=0;
  }
  x.seq+=source;x.held=reset?false:held;if(reset){x.seq=x.received=x.hash=0;for(auto&p:x.priority)p=0;}
  publish(n,b^1);
 }
 void barrier(unsigned id,uint64_t phase){if(workers==1)return;
  if(local||workers==2){progress[id].value.store(phase,std::memory_order_release);unsigned peers=local?neighbors[id]:(1u<<(1-id));while(peers){unsigned p=__builtin_ctz(peers);peers&=peers-1;while(progress[p].value.load(std::memory_order_acquire)<phase)_mm_pause();}return;}
  if(id){progress[id].value.store(phase,std::memory_order_release);while(progress[0].value.load(std::memory_order_acquire)<phase)_mm_pause();}
  else{for(unsigned p=1;p<workers;++p)while(progress[p].value.load(std::memory_order_acquire)<phase)_mm_pause();progress[0].value.store(phase,std::memory_order_release);}
 }
 void cycle(unsigned id,unsigned&b,unsigned&configuration,uint64_t&time,uint64_t&phase){
#ifdef RDS_MESH_STRESS
  if(((phase+id*17)&31)==0)for(unsigned delay=0;delay<2000;++delay)_mm_pause();
#endif
  if(tiled){for(unsigned tile=id*(Count/4/workers);tile<(id+1)*(Count/4/workers);++tile){unsigned n=(tile/(Side/2))*2*Side+(tile%(Side/2))*2;
    transition(n,b,job_reset,configuration,time);transition(n+1,b,job_reset,configuration,time);transition(n+Side,b,job_reset,configuration,time);transition(n+Side+1,b,job_reset,configuration,time);
  }}else for(unsigned n=id*(Count/workers);n<(id+1)*(Count/workers);++n)transition(n,b,job_reset,configuration,time);
  barrier(id,++phase);b^=1;configuration=job_traffic;time=job_reset?0:time+1;
 }
 void execute(unsigned id){unsigned b=bank,configuration=cfg;uint64_t time=tick,phase=total,c=0;
  if(unroll)for(;c+1<job_cycles;c+=2){cycle(id,b,configuration,time,phase);cycle(id,b,configuration,time,phase);}
  for(;c<job_cycles;++c)cycle(id,b,configuration,time,phase);
 }
 void run(uint64_t cycles,bool reset,unsigned traffic){if(!cycles)return;job_cycles=cycles;job_reset=reset;job_traffic=traffic;uint64_t epoch=command.fetch_add(1,std::memory_order_release)+1;execute(0);
  for(unsigned id=1;id<workers;++id)while(done[id].value.load(std::memory_order_acquire)!=epoch)_mm_pause();bank^=cycles&1;cfg=traffic;tick=reset?0:tick+cycles;total+=cycles;
 }
 std::vector<uint64_t> outputs()const{std::vector<uint64_t> out;for(unsigned n=0;n<Count;++n){const auto&x=nodes[n];const auto&s=snapshots[bank][n];bool fire=(s.valid&1)&&((tick+n)&7)!=0;uint64_t seq=0,meta=0;if constexpr(Functional)if(fire){const auto&p=offered(n,s,0);seq=p.seq;meta=uint64_t(p.src)|(uint64_t(p.dest)<<16);}out.insert(out.end(),{x.seq,x.received,x.hash,uint64_t(fire),seq,meta,0,0});}return out;}
 size_t storage()const{return sizeof nodes+sizeof snapshots;}
};

struct NativeMesh{
 rds_sim*s=nullptr;
 NativeMesh(const std::string&dir,unsigned workers){unsigned flags=0;require(bool(std::ifstream(dir+"/policy")>>flags),"missing native policy");char error[512];rds_options o{workers,flags};s=rds_load_with_options((dir+"/model.rsim").c_str(),&o,error,sizeof error);if(!s)throw std::runtime_error(error);rds_set_strict(s,false);require(!rds_use_compiled(s,(dir+"/w"+std::to_string(workers)+".so").c_str()),rds_error(s));require(rds_get_stats(s).workers==workers,"actual workers differ");}
 ~NativeMesh(){rds_free(s);}
 void run(uint64_t cycles,bool reset,unsigned traffic){require(!rds_set_u64(s,0,reset)&&!rds_set_u64(s,1,traffic)&&!rds_advance_cycles(s,cycles),rds_error(s));}
 std::vector<uint64_t> outputs(){require(!rds_eval(s),rds_error(s));std::vector<uint64_t> out;for(unsigned p=2;p<rds_get_stats(s).ports;++p){size_t at=out.size(),words=(rds_port_width(s,p)+63)/64;out.resize(at+words);require(!rds_get(s,p,out.data()+at,words),rds_error(s));}return out;}
};
static inline std::vector<uint64_t> controls(const std::vector<uint64_t>&v){std::vector<uint64_t>r;for(size_t i=0;i<v.size();i+=8)r.insert(r.end(),{v[i],v[i+1],v[i+3]});return r;}
template<unsigned Side,bool F,unsigned B> static void verify_one(const std::string&dir,unsigned workers,bool tiled,bool local,bool unroll){FastMesh<Side,F,B> fast(workers,tiled,local,unroll);MeshSoftware oracle(Side);NativeMesh native(dir,1);
 for(unsigned t=0;t<1800;++t){bool reset=t<2||t==517;unsigned traffic=t<1200?(t/150)%3:0;auto expected=oracle.outputs(),actual=fast.outputs();require((F?actual==expected:controls(actual)==controls(expected)),"software cycle mismatch at "+std::to_string(t));require(native.outputs()==expected,"native/oracle cycle mismatch");fast.run(1,reset,traffic);native.run(1,reset,traffic);oracle.step(reset,traffic);}
 for(unsigned block=0;block<60;++block){unsigned count=std::array<unsigned,7>{0,1,2,3,7,16,31}[block%7],traffic=block%3;bool reset=block%19==0;fast.run(count,reset,traffic);native.run(count,reset,traffic);for(unsigned i=0;i<count;++i)oracle.step(reset,traffic);auto expected=oracle.outputs(),actual=fast.outputs();require((F?actual==expected:controls(actual)==controls(expected)),"software batch mismatch");require(native.outputs()==expected,"native batch mismatch");}
 std::cerr<<"PASS side="<<Side<<" functional="<<F<<" borrow="<<B<<" workers="<<workers<<" tile="<<tiled<<" local="<<local<<" unroll="<<unroll<<'\n';
}
template<unsigned Side> static void verify(const std::string&dir){for(unsigned w:{1,2,4,8}){if(Side*Side<w)continue;
 for(bool tile:{false,true}){if(tile&&Side*Side<w*4)continue;for(bool local:{false,true})for(bool unroll:{false,true}){
  verify_one<Side,true,false>(dir,w,tile,local,unroll);verify_one<Side,true,true>(dir,w,tile,local,unroll);verify_one<Side,false,false>(dir,w,tile,local,unroll);verify_one<Side,true,2>(dir,w,tile,local,unroll);
 }}}
}
template<unsigned Side,bool F,unsigned B> static Json measure(unsigned workers,uint64_t cycles,unsigned traffic,bool tiled,bool local,bool unroll){FastMesh<Side,F,B> model(workers,tiled,local,unroll);model.run(1,true,traffic);auto begin=std::chrono::steady_clock::now();model.run(cycles,false,traffic);double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count();auto signature=model.outputs();for(unsigned i=0;i<Side*Side;++i)require(!traffic||(signature[i*8]>cycles/1024&&signature[i*8+1]>cycles/1024),"stalled software endpoint");return {{"ns_per_cycle",ns/cycles},{"signature",signature},{"controls",controls(signature)},{"storage_bytes",model.storage()}};}
template<unsigned Side> static int dispatch(int argc,char**argv){std::string action=argv[1],dir=argv[2];if(action=="verify"){verify<Side>(dir);return 0;}require(action=="measure"&&argc==10,"measure DIR native|functional|borrowed|indexed|perf|oracle WORKERS CYCLES TRAFFIC row|tile global|neighbors UNROLL");std::string mode=argv[3];unsigned workers=std::stoul(argv[4]),traffic=std::stoul(argv[6]),unroll=std::stoul(argv[9]);uint64_t cycles=std::stoull(argv[5]);std::string layout=argv[7],sync=argv[8];require(cycles&&traffic<=2&&(unroll==1||unroll==2)&&(layout=="row"||layout=="tile")&&(sync=="global"||sync=="neighbors"),"invalid benchmark arguments");Json row;bool tile=layout=="tile",local=sync=="neighbors";
 if(mode=="native"){NativeMesh model(dir,workers);model.run(1,true,traffic);auto begin=std::chrono::steady_clock::now();model.run(cycles,false,traffic);double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count();auto signature=model.outputs();row={{"ns_per_cycle",ns/cycles},{"signature",signature},{"controls",controls(signature)}};}
 else if(mode=="oracle"){require(workers==1,"oracle requires one worker");MeshSoftware model(Side);model.step(true,traffic);auto begin=std::chrono::steady_clock::now();for(uint64_t i=0;i<cycles;++i)model.step(false,traffic);double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count();auto signature=model.outputs();row={{"ns_per_cycle",ns/cycles},{"signature",signature},{"controls",controls(signature)}};}
 else if(mode=="functional")row=measure<Side,true,false>(workers,cycles,traffic,tile,local,unroll==2);
 else if(mode=="borrowed")row=measure<Side,true,true>(workers,cycles,traffic,tile,local,unroll==2);
 else if(mode=="indexed")row=measure<Side,true,2>(workers,cycles,traffic,tile,local,unroll==2);
 else if(mode=="perf")row=measure<Side,false,false>(workers,cycles,traffic,tile,local,unroll==2);else throw std::runtime_error("unknown model");
 row["mode"]=mode;row["nodes"]=Side*Side;row["workers"]=workers;row["cycles"]=cycles;row["traffic"]=traffic;row["layout"]=layout;row["sync"]=sync;row["unroll"]=unroll;std::cout<<row<<'\n';return 0;
}
#ifndef RDS_MESH_LIBRARY_ONLY
int main(int argc,char**argv){try{require(argc>=3,"verify DIR | measure DIR MODE WORKERS CYCLES TRAFFIC LAYOUT SYNC UNROLL");unsigned side;require(bool(std::ifstream(std::string(argv[2])+"/side")>>side),"missing mesh side");switch(side){case 2:return dispatch<2>(argc,argv);case 4:return dispatch<4>(argc,argv);case 8:return dispatch<8>(argc,argv);default:throw std::runtime_error("software experiment supports sides 2, 4 and 8");}}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

#endif
