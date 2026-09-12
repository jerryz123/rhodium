// Replays stationary priorities and optional column splitting with independent query operands.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <filesystem>
#include <iostream>
#include <random>
#include <sys/wait.h>
#include <unistd.h>
using namespace rds;
static void check(bool ok,const std::string&why){if(!ok)throw std::runtime_error(why);}
static void command(std::vector<std::string>args){pid_t pid=fork();check(pid>=0,"fork");if(!pid){std::vector<char*>v;for(auto&a:args)v.push_back(a.data());v.push_back(nullptr);execvp(v[0],v.data());_exit(127);}int status;check(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"generated compile");}
struct Host{bool fail=false;unsigned calls=0;};
static int host(void*context,const uint64_t*,size_t,uint64_t*out,size_t){auto*h=static_cast<Host*>(context);++h->calls;out[0]=0;return h->fail?-1:0;}
static Model fixture(unsigned rows,unsigned cols,unsigned flags){Model m;
 for(auto k:{"ports","objects","registers","memories","reads","writes","assertions","origins","inventory"})m.metadata[k]=Json::array();
 m.widths={1,rows*cols,rows*cols,cols,rows};const char*names[]={"reset","updates","queries","accepts","taken"};for(Id i=0;i<5;++i)m.metadata["ports"].push_back({0,i,names[i]});
 auto emit=[&](unsigned code,unsigned w,std::vector<Id>a,std::vector<uint64_t>im=std::vector<uint64_t>{}){Id v=m.widths.size();m.widths.push_back(w);m.ops.push_back({code,v,std::move(a),std::move(im)});return v;};
 Id zero=emit(0,rows,{}, {0}),no=emit(0,1,{}, {0});std::vector<Id>inputs{0},query;
 uint64_t mask=0;for(unsigned c=0;c<cols;++c){Id request=emit(17,rows,{1},{c*rows});inputs.push_back(c%3==1?emit(3,rows,{request,zero}):request);query.push_back(emit(17,rows,{2},{c*rows}));if(c%3)mask|=UINT64_C(1)<<c;}
 inputs.push_back(emit(3,cols,{3,emit(0,cols,{}, {mask})}));m.metadata["objects"].push_back({10,rows,cols,flags,inputs,"matcher"});
 m.metadata["objects"].push_back({8,1,1,0,{no,no,no,no,no},"host"});
 for(unsigned c=0;c<cols;++c){Id prefix=emit(29,rows,std::vector<Id>(query.begin(),query.begin()+c+1),{0,c}),step=emit(29,rows,{4,query[c]},{0,cols+c});m.metadata["ports"].push_back({1,prefix,"prefix"+std::to_string(c)});m.metadata["ports"].push_back({1,step,"step"+std::to_string(c)});}
 m.validate();return m;
}
int main(int argc,char**argv){try{check(argc==2,"supply artifact directory");std::filesystem::create_directories(argv[1]);
 for(auto [rows,cols]:std::vector<std::pair<unsigned,unsigned>>{{1,1},{3,4},{7,6},{64,3},{3,64}})for(unsigned flags:{32u,96u}){
  Model original=fixture(rows,cols,flags),folded=original;fold_stationary_matchers(folded);folded.validate();
  check(folded.metadata["stationary_matcher_summary"]["queries_rewritten"].get<unsigned>()>0,"stationary queries were not lowered");check(folded.metadata["objects"].size()==2&&folded.metadata["objects"][0][3]==flags&&folded.metadata["objects"][0][5]=="matcher","matcher ownership changed");
  if(std::getenv("RDS_TEST_SPLIT_MATCHERS")){split_matcher_columns(folded);folded.validate();}
  std::string base=std::string(argv[1])+"/"+std::to_string(rows)+"-"+std::to_string(cols)+"-"+std::to_string(flags);original.write_binary(base+"-original.rsim");folded.write_binary(base+"-folded.rsim");
  Host callbacks;std::vector<rds_sim*>engines;std::vector<std::string>libraries;
  for(unsigned mode=0;mode<5;++mode){char error[512];rds_options options{mode==3?4u:1u,mode<2?RDS_REFERENCE:4290056208u};auto*s=rds_load_with_options((base+(mode==0||mode==2?"-original.rsim":"-folded.rsim")).c_str(),&options,error,sizeof error);check(s,error);rds_set_strict(s,mode!=4);check(!rds_bind_host(s,"host",host,&callbacks),rds_error(s));std::string library;
   if(mode>=2){std::string source=base+"-"+std::to_string(mode)+".c";library=source+".so";check(!rds_emit_c(s,source.c_str(),0),rds_error(s));command({"clang","-O3","-march=native",mode==4?"-DNDEBUG":"-UNDEBUG","-shared","-fPIC",source,"-o",library});check(!rds_use_compiled(s,library.c_str()),rds_error(s));}engines.push_back(s);libraries.push_back(library);
  }
  std::mt19937_64 random(897+rows+cols+flags);std::vector<uint64_t>updates((rows*cols+63)/64),queries(updates.size());uint64_t rowmask=rows==64?UINT64_MAX:(UINT64_C(1)<<rows)-1,colmask=cols==64?UINT64_MAX:(UINT64_C(1)<<cols)-1;
  for(unsigned cycle=0;cycle<300;++cycle){std::fill(updates.begin(),updates.end(),0);for(unsigned c=0;c<cols;++c){uint64_t value=random()&rowmask;if(flags&64)value=random()%3?UINT64_C(1)<<(random()%rows):0;for(unsigned r=0;r<rows;++r)if(value&(UINT64_C(1)<<r)){unsigned bit=c*rows+r;updates[bit/64]|=UINT64_C(1)<<(bit%64);}}
   for(auto&w:queries)w=random();
   if(rows*cols%64)queries.back()&=(UINT64_C(1)<<(rows*cols%64))-1;
   uint64_t reset=cycle==0||cycle%73==0,accepts=random()&colmask,taken=random()&rowmask;
   for(auto*s:engines){for(auto [name,value]:std::vector<std::pair<const char*,uint64_t>>{{"reset",reset},{"accepts",accepts},{"taken",taken}})check(!rds_set_u64(s,rds_find_port(s,name),value),rds_error(s));for(auto [name,data]:std::vector<std::pair<const char*,std::vector<uint64_t>*>>{{"updates",&updates},{"queries",&queries}})check(!rds_set(s,rds_find_port(s,name),data->data(),data->size()),rds_error(s));}
   auto evaluate=[&,columns=cols](){unsigned before=callbacks.calls;std::vector<uint64_t>expected;for(unsigned e=0;e<engines.size();++e){auto*s=engines[e];check(!rds_eval(s),rds_error(s));std::vector<uint64_t>actual;for(unsigned c=0;c<columns;++c)for(auto stem:{"prefix","step"}){uint64_t value;check(!rds_get(s,rds_find_port(s,(stem+std::to_string(c)).c_str()),&value,1),rds_error(s));actual.push_back(value);}if(e==0)expected=actual;else check(actual==expected,"priority or independent query mismatch");}check(callbacks.calls==before,"eval published host effect");};
   evaluate();evaluate();if(cycle%87==0){callbacks.fail=true;for(auto*s:engines)check(rds_advance(s)!=0,"failed host published state");callbacks.fail=false;evaluate();}for(auto*s:engines)check(!rds_advance(s),rds_error(s));
   if(cycle==123)for(unsigned e=2;e<engines.size();++e)check(!rds_use_compiled(engines[e],libraries[e].c_str()),rds_error(engines[e]));
  }
  // Grant-reuse inputs keep their strict one-hot contract after query lowering.
  if((flags&64)&&rows>1&&cols>2){std::fill(updates.begin(),updates.end(),0);
   for(unsigned bit:{2*rows,2*rows+1})updates[bit/64]|=UINT64_C(1)<<(bit%64);
   for(unsigned e=0;e<engines.size();++e){auto*s=engines[e];check(!rds_set_u64(s,rds_find_port(s,"reset"),0),rds_error(s));check(!rds_set(s,rds_find_port(s,"updates"),updates.data(),updates.size()),rds_error(s));check(!rds_eval(s),rds_error(s));check((rds_advance(s)!=0)==(e!=4),"strict invalid grant changed");}
  }
  for(auto*s:engines)rds_free(s);
  std::cout<<"PASS rows="<<rows<<" columns="<<cols<<" flags="<<flags<<'\n';
 }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
