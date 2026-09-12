// Checks lifted flow/state cones, Boolean arithmetic and shared handshake masks against original execution.
#include "../../rhodium/sim/compiler/kernel.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
using namespace rds;
static void check(bool ok,const std::string &why){if(!ok)throw std::runtime_error(why);}
static void compile(const std::string &path,bool release){
  pid_t pid=fork();check(pid>=0,"fork failed");
  if(!pid){auto library=path+".so";execlp("clang","clang","-O3","-march=native",release?"-DNDEBUG":"-UNDEBUG","-shared","-fPIC",path.c_str(),"-o",library.c_str(),nullptr);_exit(127);}
  int status;check(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&!WEXITSTATUS(status),"C compilation failed");
}
int main(int argc,char **argv){try{
  check(argc==2,"supply artifact directory");std::string dir=argv[1];std::filesystem::create_directories(dir);
  Model m;m.metadata={{"format","rhodium-simulation-ir-v1"},{"occurrences",Json::array({"fixture"})}};
  for(auto name:{"ports","registers","memories","writes","reads","assertions","objects","origins","inventory","contracts"})m.metadata[name]=Json::array();
  m.metadata["opcodes"]={"constant","copy","not","and","or","xor","add","sub","mul","shl","shru","shrs","eq","ult","slt","mux_lookup","onehot_mux","extract","zext","sext","pack","vector_index","vector_inject","vector_write_set","memory_read_async","decode","set_clear","balance","counter_step","object_query","alu","byte_merge"};
  auto value=[&](unsigned w){Id v=m.widths.size();m.widths.push_back(w);return v;};
  auto input=[&](unsigned w,const char *name){Id v=value(w);m.metadata["ports"].push_back({0,v,name});return v;};
  auto op=[&](unsigned code,unsigned w,std::vector<Id> a,std::vector<uint64_t> im=std::vector<uint64_t>{}){Id v=value(w);m.ops.push_back({code,v,std::move(a),std::move(im)});return v;};
  auto out=[&](Id v,const std::string &name){m.metadata["ports"].push_back({1,v,name});};
  auto contract=[&](Id v,const char *kind,Id in,Id ready){m.metadata["contracts"].push_back({0,kind,Json::array({{"in0.bits",in},{"out0.bits",v},{"out0.ready",ready}}),Json::array()});};
  Id reset=input(1,"reset"),select=input(1,"select"),x=input(64,"x"),capture=input(64,"capture"),wide=input(257,"wide"),key=input(65,"key");
  Id key31=input(31,"key31"),key32=input(32,"key32");
  Id array=input(771,"array"),array_onehot=input(2,"array-onehot");
  Id keyed=op(8,64,{op(5,64,{op(18,64,{op(20,63,{key31,key32})}),op(0,64,{}, {0xa35})}),op(0,64,{}, {0x1357})});
  contract(keyed,"map",key31,reset);out(keyed,"keyed");
  Id zero=op(0,64,{}, {0}),one=op(0,64,{}, {1});
  Id boolean=op(3,1,{op(4,1,{op(5,1,{op(3,1,{reset,select}),op(4,1,{reset,select})}),reset}),op(2,1,{select})});
  contract(boolean,"map",reset,select);out(boolean,"boolean");
  // One-bit add/subtract are XOR, while multiply is AND. Keeping arithmetic
  // opcodes prevents word recovery across these independent bit lanes.
  for(unsigned code:{6u,7u,8u}){
    std::vector<Id> lanes;
    for(unsigned bit=0;bit<8;++bit)
      lanes.push_back(op(code,1,{op(17,1,{x},{bit}),op(17,1,{capture},{bit})}));
    Id bank=op(20,8,lanes);out(bank,"bit-arithmetic-"+std::to_string(code));
    contract(bank,"map",x,select);
  }
  // Several row decisions consume the same ready/grant columns. Grants are
  // arbitrary bit masks, so sharing must not assume one-hot arbitration.
  Id ready2=op(17,1,{capture},{63});
  for(unsigned rows:{3u,7u,5u}){
    std::vector<Id> ready={reset,select,ready2};
    std::vector<Id> grants;
    for(unsigned col=0;col<3;++col){unsigned width=col==0?rows:col==1&&rows==3?5:7;Id owner=m.metadata["objects"].size();
      Id payload=op(17,width,{x},{8*col});
      m.metadata["objects"].push_back({1,width,1,0,{reset,select,payload,ready[col]},"grant-column-"+std::to_string(rows)+"-"+std::to_string(col)});
      grants.push_back(op(29,width,{}, {owner,3}));
    }
    Id z=op(0,4,{}, {0}),empty=op(0,1,{}, {0});
    for(unsigned row=0;row<rows;++row){
      std::vector<Id> lanes;
      for(unsigned col=0;col<3;++col)lanes.push_back(op(3,1,{ready[col],op(17,1,{grants[col]},{row})}));
      lanes.push_back(empty);
      Id accepted=rows==5?op(4,1,{op(4,1,{lanes[0],lanes[1]}),op(4,1,{lanes[2],empty})})
                         :op(2,1,{op(12,1,{op(20,4,lanes),z})});
      contract(accepted,"map",x,select);out(accepted,"handshake-"+std::to_string(rows)+"-"+std::to_string(row));
    }
  }
  // Sibling controls share a data-dependent predicate. They must be evaluated
  // together without conflating readiness and validity or changing captures.
  Id predicate=op(12,1,{op(5,64,{x,capture}),zero});
  Id ready0=op(3,1,{predicate,select});
  Id valid0=op(4,1,{predicate,op(2,1,{select})});
  Id ready1=op(5,1,{valid0,ready0});
  m.metadata["contracts"].push_back({0,"atomic-fork",Json::array({{"in0.ready",ready0},{"out0.valid",valid0},{"in1.ready",ready1},{"out0.ready",select}}),Json::array()});
  out(ready0,"ready0");out(valid0,"valid0");out(ready1,"ready1");
  // The intervening observable computation forbids delaying the early result
  // until the later result. The two fields still form a legal acyclic circuit.
  Id early=op(3,1,{select,reset});
  Id between=op(15,64,{early,x,capture},{1});out(between,"between");
  Id late=op(12,1,{op(5,64,{op(6,64,{between,x}),x}),capture});
  m.metadata["contracts"].push_back({0,"join",Json::array({{"in0.ready",early},{"out0.valid",late}}),Json::array()});
  out(early,"early");out(late,"late");
  for(unsigned lane=0;lane<12;++lane){Id a=x,b=capture;
    for(unsigned j=0;j<10;++j){a=op(6,64,{op(5,64,{a,capture}),op(0,64,{}, {j+lane+3})});b=op(8,64,{op(5,64,{b,x}),op(0,64,{}, {j+3})});}
    Id selected=op(15,64,{select,a,b},{1});contract(selected,"map",x,select);out(selected,"map"+std::to_string(lane));
  }
  // Multiple uses inside one arm should remain in that arm. A nested mux
  // shares the diamond with its selector, exercising enclosing-scope placement.
  Id heavy=x;for(unsigned j=0;j<12;++j)heavy=op(8,64,{op(5,64,{heavy,capture}),op(0,64,{}, {2*j+3})});
  Id left=op(6,64,{heavy,capture}),right=op(5,64,{heavy,x});
  Id nested=op(15,64,{op(12,1,{heavy,zero}),left,right},{1});
  Id diamond=op(15,64,{select,capture,op(8,64,{nested,heavy})},{1});
  contract(diamond,"map",x,select);out(diamond,"diamond");
  // A zero control proves these whole cones constant even while captured data
  // changes. The wide path requires two independent controls to be zero.
  Id gated=op(3,64,{op(19,64,{select}),op(5,64,{heavy,op(6,64,{heavy,capture})})});
  contract(gated,"gate",x,select);out(gated,"gated");
  Id wide_heavy=wide;
  for(unsigned j=0;j<10;++j)wide_heavy=op(6,257,{op(5,257,{wide_heavy,op(18,257,{capture})}),wide});
  Id wide_gated=op(4,257,{op(3,257,{op(19,257,{select}),wide_heavy}),op(3,257,{op(19,257,{reset}),wide})});
  contract(wide_gated,"gate",wide,select);out(wide_gated,"wide-gated");
  // An escaping result and a select that depends on a data arm must stay live.
  Id shared=op(6,64,{x,capture});out(shared,"shared");
  Id choose=op(12,1,{shared,zero}),d=op(15,64,{choose,shared,op(6,64,{shared,one})},{1});
  d=op(5,64,{op(6,64,{d,one}),op(2,64,{capture})});contract(d,"filter",x,select);out(d,"feedback");
  // A wide-selector priority mux exercises local addresses and repeated keys.
  Id a=op(5,257,{op(2,257,{wide}),op(0,257,{}, {3,4,5,6,1})});
  Id b=op(6,257,{wide,op(0,257,{}, {9,8,7,6,1})});
  Id mux=op(15,257,{key,wide,a,b,a},{7,1,7,1,9,0});contract(mux,"demux",wide,select);out(mux,"wide-mux");
  Id repacked=op(20,257,{op(17,1,{wide},{0}),op(17,63,{wide},{1}),op(17,65,{wide},{64}),op(17,128,{wide},{129})});
  contract(repacked,"map",wide,select);out(repacked,"repacked");
  Id extended=op(2,257,{op(19,257,{op(2,63,{op(17,63,{wide},{2})})})});
  contract(extended,"map",wide,select);out(extended,"extended");
  // Snapshot state assembles successive words and holds when disabled.
  Id q=value(257),shift=op(0,64,{}, {64});
  Id next=op(4,257,{op(9,257,{q,shift}),op(18,257,{op(6,64,{x,capture})})});
  next=op(15,257,{select,q,next},{1});Id zwide=op(0,257,{}, {0,0,0,0,0});
  m.metadata["registers"].push_back({q,next,reset,zwide});out(q,"assembled");
  // Strict partial operations remain ordinary graph roots, even if unobserved.
  Id oh=input(2,"onehot");op(16,64,{oh,x,capture});
  Id element0=op(17,257,{array},{0}),element1=op(17,257,{array},{257}),element2=op(17,257,{array},{514});
  out(op(16,257,{array_onehot,element1,element2}),"indexed-onehot");
  out(op(15,257,{key,element0,element2,element1},{7,1,9,0}),"indexed-priority");
  for(const auto&o:m.ops)m.metadata["origins"].push_back({o.out,"fixture",o.out,"value","test.operation",0,m.widths[o.out]});
  m.validate();m.write_binary(dir+"/original.rsim");Model lifted=m;
  {auto words=m;optimize_body(words);recover_words(words);regroup_bits(words);optimize_body(words);words.validate();
    for(const auto &p:words.metadata["ports"]){auto name=p[2].get<std::string>();
      if(name.rfind("bit-arithmetic-",0)!=0)continue;
      auto producer=std::find_if(words.ops.begin(),words.ops.end(),[&](const Op&o){return o.out==p[1];});
      check(producer!=words.ops.end()&&producer->code==(name.back()=='8'?3u:5u)&&words.widths[producer->out]==8,
            "one-bit arithmetic did not recover a word Boolean operation");
    }
  }
  lift_indexed_datapaths(lifted);lifted.validate();
  check(lifted.metadata["indexed_datapath_summary"]["selections"].get<unsigned>()>=2,"datapath fixture did not lift array selection");
  bundle_contract_controls(lifted);lifted.validate();
  check(!lifted.metadata["contract_control_summary"]["groups"].empty(),"sibling control fixture did not bundle");
  check(lifted.metadata["contract_control_summary"]["dependency_rejections"].get<unsigned>()>0,"early consumer fixture did not reject bundling");
  lift_contracts(lifted);lifted.validate();
  check(lifted.metadata["contract_kernel_summary"]["programs"].get<unsigned>()>=12,"fixture did not lift");
  check(lifted.ops.size()<m.ops.size()/2,"private operations not removed");
  share_handshake_rows(lifted);lifted.validate();
  check(lifted.metadata["handshake_row_summary"]["outputs"].get<unsigned>()>=15,"pack/OR row fixtures did not share word masks");
  auto preoptimized=lifted;
  optimize_contract_programs(lifted);lifted.validate();
  check(lifted.metadata["contract_program_optimization"]["changed"].get<unsigned>()>0,"local word fixture did not optimize");
  guard_contract_programs(lifted);lifted.validate();
  check(lifted.metadata["contract_guard_summary"]["programs"].get<unsigned>()>0,"inactivity fixture did not produce guards");
  bool wide_guard=false;
  for(const auto &g:lifted.metadata["contract_guard_summary"]["guards"])
    wide_guard|=lifted.widths[g["output"].get<Id>()]==257&&g["zero_inputs"].size()==2;
  check(wide_guard,"wide independent controls did not produce a joint guard");
  preoptimized.write_binary(dir+"/preoptimized.rsim");
  {std::ofstream f(dir+"/lifted.json");f<<lifted.json();}
  lifted=Model::read(dir+"/lifted.json");lifted.write_binary(dir+"/lifted.rsim");
  Model specialized=lifted;Json ports=Json::array();std::vector<Op> literals;
  for(const auto &p:specialized.metadata["ports"]){std::string name=p[2];
    if(p[0]==0 && (name=="x"||name=="capture"||name=="select"))
      literals.push_back({0,p[1],{}, {name=="x"?17u:name=="capture"?31u:1u}});
    else ports.push_back(p);
  }
  specialized.metadata["ports"]=ports;literals.insert(literals.end(),specialized.ops.begin(),specialized.ops.end());specialized.ops=std::move(literals);
  optimize_body(specialized);specialized.validate();
  uint64_t expected_map=31;for(unsigned j=0;j<10;++j)expected_map=(expected_map^17)*(j+3);
  for(const auto &p:specialized.metadata["ports"])if(p[2]=="map0"){
    auto d=std::find_if(specialized.ops.begin(),specialized.ops.end(),[&](const Op&o){return o.out==p[1];});
    check(d!=specialized.ops.end()&&d->code==0&&d->imm[0]==expected_map,"contract specialization lost exact arithmetic");
  }
  Model eager=lifted;for(auto &o:eager.ops)if(o.code==32)o.imm[0]=1;eager.write_binary(dir+"/eager.rsim");
  Model cached=lifted;unsigned caches=0;bool boundary_key=false;
  for(auto &o:cached.ops)if(o.code==32){uint64_t bits=0;for(auto a:o.args)bits+=cached.widths[a];
    o.imm[0]=3; // Wide/noneligible programs retain ordinary emission.
    if(bits<=63&&cached.widths[o.out]<=64){++caches;boundary_key|=bits==63;}}
  check(boundary_key,"fixture did not exercise the 63-bit cache key boundary");
  check(caches>0,"fixture has no exact-key control program");cached.validate();cached.write_binary(dir+"/cached.rsim");
  // Reject malformed programs in both exchange and binary readers.
  auto found=std::find_if(lifted.ops.begin(),lifted.ops.end(),[](const Op&o){return o.code==32;});check(found!=lifted.ops.end(),"no kernel");
  for(unsigned kind=0;kind<5;++kind){Model bad=lifted;auto &o=bad.ops[found-lifted.ops.begin()];
    if(kind==0)o.imm.pop_back();
    if(kind==1)o.imm[1]=129;
    if(kind==2)o.imm[3]=0;
    if(kind==3)o.imm[3+o.imm[1]+o.imm[2]]=32;
    if(kind==4){rds_kernel_format k;check(rds_kernel_parse(o.imm.data(),o.imm.size(),&k),"parse");o.imm[k.ops[0].imm-k.ops[0].nargs]=UINT64_MAX;}
    bool rejected=false;try{bad.validate();}catch(const std::exception&){rejected=true;}check(rejected,"compiler accepted bad program");
    auto file=dir+"/bad.rsim";bad.write_binary(file);char error[512];auto *s=rds_load(file.c_str(),error,sizeof error);check(!s,"runtime accepted bad program");
  }
  auto view=std::find_if(lifted.ops.begin(),lifted.ops.end(),[](const Op&o){return o.code==33;});
  check(view!=lifted.ops.end(),"no fused payload view");
  for(unsigned kind=0;kind<3;++kind){Model bad=lifted;auto &o=bad.ops[view-lifted.ops.begin()];
    if(kind==0)o.imm.pop_back();
    if(kind==1)o.imm[0]=bad.widths[o.args[1]];
    if(kind==2)o.args.pop_back();
    bool rejected=false;try{bad.validate();}catch(const std::exception&){rejected=true;}
    check(rejected,"compiler accepted malformed payload view");
    auto file=dir+"/bad-view.rsim";bad.write_binary(file);char error[512];
    auto *s=rds_load(file.c_str(),error,sizeof error);check(!s,"runtime accepted malformed payload view");
  }
  std::vector<rds_sim*> sims;
  for(unsigned mode=0;mode<10;++mode){char error[512];bool parallel=mode==5||mode==6||mode==9;rds_options opt{parallel?4u:1u,mode<2?RDS_REFERENCE:4290056208u};
    std::string model=dir+(mode==0?"/original.rsim":mode==2?"/eager.rsim":mode>=7?"/cached.rsim":"/lifted.rsim");
    auto *s=rds_load_with_options(model.c_str(),&opt,error,sizeof error);check(s,error);
    if(parallel)check(rds_get_stats(s).workers>1,"parallel fixture did not activate workers");
    if(mode>=2){auto source=dir+"/kernel-"+std::to_string(mode)+".c";check(!rds_emit_c(s,source.c_str(),4096),rds_error(s));compile(source,mode%2==0);check(!rds_use_compiled(s,(source+".so").c_str()),rds_error(s));}
    sims.push_back(s);
  }
  std::mt19937_64 random(0x4c494654);
  for(unsigned cycle=0;cycle<1000;++cycle){uint64_t w[5]={random(),random(),random(),random(),random()&1},k[2]={cycle%5==0?11u:cycle%3?7u:9u,cycle%3?1u:0u};
    uint64_t array_words[13];for(auto &word:array_words)word=random();array_words[12]&=7;
    uint64_t scalar[7]={cycle%37==0,cycle%2,random(),random(),cycle%2?1u:2u,random()&0x7fffffffu,random()&0xffffffffu};const char *names[]={"reset","select","x","capture","onehot","key31","key32"};
    bool permissive=cycle%41==0;if(permissive)scalar[4]=cycle%2?3:0;
    for(auto*s:sims){rds_set_strict(s,!permissive);for(unsigned j=0;j<7;++j)check(!rds_set_u64(s,rds_find_port(s,names[j]),scalar[j]),rds_error(s));
      check(!rds_set(s,rds_find_port(s,"array"),array_words,13),rds_error(s));
      check(!rds_set_u64(s,rds_find_port(s,"array-onehot"),scalar[4]),rds_error(s));
      check(!rds_set(s,rds_find_port(s,"wide"),w,5),rds_error(s));check(!rds_set(s,rds_find_port(s,"key"),k,2),rds_error(s));check(!rds_eval(s),rds_error(s));check(!rds_eval(s),rds_error(s));}
    for(const auto&p:m.metadata["ports"])if(p[0]==1){std::string name=p[2];size_t words=(m.widths[p[1].get<Id>()]+63)/64;std::vector<uint64_t> expected(words),actual(words);
      check(!rds_get(sims[0],rds_find_port(sims[0],name.c_str()),expected.data(),words),"reference output");
      if(name.rfind("bit-arithmetic-",0)==0)
        check(expected[0]==((name.back()=='8'?scalar[2]&scalar[3]:scalar[2]^scalar[3])&255),"independent Boolean arithmetic");
      for(unsigned i=1;i<sims.size();++i){check(!rds_get(sims[i],rds_find_port(sims[i],name.c_str()),actual.data(),words),"output");check(actual==expected,"diverged: "+name+" cycle "+std::to_string(cycle)+" mode "+std::to_string(i));}}
    for(auto*s:sims)check(!rds_advance(s),rds_error(s));
    if(cycle==499)for(unsigned mode=7;mode<sims.size();++mode){auto library=dir+"/kernel-"+std::to_string(mode)+".c.so";
      check(!rds_use_compiled(sims[mode],library.c_str()),rds_error(sims[mode]));}
  }
  for(auto*s:sims){rds_set_strict(s,1);check(!rds_set_u64(s,rds_find_port(s,"onehot"),1),rds_error(s));
    check(!rds_set_u64(s,rds_find_port(s,"array-onehot"),3),rds_error(s));check(rds_eval(s)!=0,"strict invalid indexed onehot was accepted");}
  for(auto*s:sims)rds_free(s);
  std::cout<<"PASS: captured maps, branch arms, shared/feedback values, wide priority muxes, snapshot updates, malformed programs, eager/lazy C, debug/release and 1/4 workers\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
