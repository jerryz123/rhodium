// Measures exact region transitions with recorded boundaries as a synchronization-free counterfactual.
#define RDS_MESH_LIBRARY_ONLY
#include "mesh-software.cpp"
#include <set>
using Model=FastMesh<8,true,0>;
struct Region {
 Model model{1,false,false,false};
 std::vector<unsigned> owned,boundary;
 std::vector<Model::Snapshot> trace;
};
static unsigned region_of(unsigned n,unsigned workers,bool tiled){unsigned index=tiled?((n/8/2)*4+n%8/2)*4+(n/8%2)*2+n%2:n;return index/(64/workers);}
int main(int argc,char**argv){try{
 require(argc==6,"mesh-replay FIXTURE WORKERS WINDOW REPEATS row|tile");std::string dir=argv[1];unsigned workers=std::stoul(argv[2]),window=std::stoul(argv[3]),repeats=std::stoul(argv[4]);bool tiled=std::string(argv[5])=="tile";
 require((workers==1||workers==2||workers==4||workers==8)&&window&&window<=4096&&repeats&&(tiled||std::string(argv[5])=="row"),"invalid replay arguments");unsigned side=0;std::ifstream(dir+"/side")>>side;require(side==8,"replay requires side 8");
 Model reference(1,false,false,false);NativeMesh native(dir,1);reference.run(1,true,2);native.run(1,true,2);reference.run(2048,false,2);native.run(2048,false,2);require(reference.outputs()==native.outputs(),"initial native mismatch");
 const auto seed_nodes=reference.nodes;const auto seed_snapshots=reference.snapshots;unsigned seed_bank=reference.bank;uint64_t seed_tick=reference.tick;
 std::vector<std::unique_ptr<Region>> regions;size_t trace_bytes=0;
 for(unsigned id=0;id<workers;++id){auto r=std::make_unique<Region>();for(unsigned n=0;n<64;++n)if(region_of(n,workers,tiled)==id)r->owned.push_back(n);
  if(tiled)std::sort(r->owned.begin(),r->owned.end(),[](unsigned a,unsigned b){auto order=[](unsigned n){return ((n/8/2)*4+n%8/2)*4+(n/8%2)*2+n%2;};return order(a)<order(b);});
  std::set<unsigned> boundary;for(unsigned n:r->owned)for(unsigned p=1;p<5;++p){unsigned peer=neighbor(n,p,8);if(peer!=none&&region_of(peer,workers,tiled)!=id)boundary.insert(peer);}r->boundary.assign(boundary.begin(),boundary.end());r->trace.reserve(window*r->boundary.size());regions.push_back(std::move(r));
 }
 for(unsigned cycle=0;cycle<window;++cycle){for(auto&r:regions)for(unsigned n:r->boundary)r->trace.push_back(reference.snapshots[reference.bank][n]);reference.run(1,false,2);native.run(1,false,2);require(reference.outputs()==native.outputs(),"recorded native mismatch");}
 const auto expected=reference.outputs();for(const auto&r:regions)trace_bytes+=r->trace.size()*sizeof(Model::Snapshot);
 auto replay=[&](Region&r){auto&m=r.model;for(unsigned n:r.owned){m.nodes[n]=seed_nodes[n];m.snapshots[0][n]=seed_snapshots[0][n];m.snapshots[1][n]=seed_snapshots[1][n];}
  unsigned bank=seed_bank;size_t at=0;for(unsigned cycle=0;cycle<window;++cycle){for(unsigned n:r.boundary)m.snapshots[bank][n]=r.trace[at++];for(unsigned n:r.owned)m.transition(n,bank,false,2,seed_tick+cycle);bank^=1;}m.bank=bank;m.tick=seed_tick+window;
 };
 auto check=[&](Region&r){auto actual=r.model.outputs();for(unsigned n:r.owned)for(unsigned k=0;k<8;++k)require(actual[n*8+k]==expected[n*8+k],"replay output mismatch");};
 for(auto&r:regions){replay(*r);check(*r);}
 std::atomic<bool> go{false};std::vector<std::thread> threads;std::array<uint64_t,8> hashes{};std::vector<double> worker_ns(workers);
 auto execute=[&](unsigned id){while(!go.load(std::memory_order_acquire))_mm_pause();auto start=std::chrono::steady_clock::now();auto&r=*regions[id];uint64_t h=0;for(unsigned repeat=0;repeat<repeats;++repeat){replay(r);for(unsigned n:r.owned)h+=r.model.nodes[n].hash+r.model.nodes[n].seq;}worker_ns[id]=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/(uint64_t(window)*repeats);hashes[id]=h;};
 for(unsigned id=1;id<workers;++id)threads.emplace_back(execute,id);
 auto begin=std::chrono::steady_clock::now();go.store(true,std::memory_order_release);execute(0);for(auto&t:threads)t.join();double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count();
 uint64_t hash=0;for(unsigned id=0;id<workers;++id){check(*regions[id]);hash+=hashes[id];}uint64_t expected_hash=0;for(unsigned n=0;n<64;++n)expected_hash+=reference.nodes[n].hash+reference.nodes[n].seq;require(hash==expected_hash*repeats,"replay checksum mismatch");
 std::cout<<Json({{"mode","recorded-boundary"},{"workers",workers},{"window",window},{"repeats",repeats},{"layout",tiled?"tile":"row"},{"ns_per_cycle",ns/(uint64_t(window)*repeats)},{"worker_ns_per_cycle",worker_ns},{"trace_bytes",trace_bytes},{"signature",expected},{"checksum",hash},{"includes_restore_and_boundary_copy",true},{"excludes_recording",true}})<<'\n';
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
