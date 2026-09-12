// Checks native TLB queries and publication against original RTL and an independent Sv39 oracle.
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
static void require(bool ok,const std::string&why){if(!ok)throw std::runtime_error(why);}
struct Entry {uint64_t va=0,ppn=0;unsigned level=0,flags=0;bool valid=false;};
static std::array<uint64_t,3> lookup(const std::vector<Entry>&entries,uint64_t va,bool enabled,unsigned access,unsigned privilege,bool sum,bool mxr,bool probe){
    if(!enabled)return {1,0,va&UINT64_C(0xffffffffffffff)};
    size_t index=0;bool hit=false;
    for(size_t i=0;i<entries.size();++i){const auto&e=entries[i];unsigned low=e.level==1?21:e.level==2?30:12;
        if(e.valid&&((e.va>>low)&((UINT64_C(1)<<(39-low))-1))==((va>>low)&((UINT64_C(1)<<(39-low))-1))){index=i;hit=true;break;}}
    const auto&e=entries[index];unsigned low=e.level==0?12:e.level==1?21:30;
    uint64_t mask=(UINT64_C(1)<<low)-1,pa=((e.ppn<<12)&~mask)|(va&mask);
    hit=hit&&((va>>39)==(((va>>38)&1)?UINT64_C(0x1ffffff):0));
    bool user=e.flags&64,read=e.flags&32,write=e.flags&16,execute=e.flags&8,allowed=false;
    if(probe){
        bool fetch=(privilege==0?user:!user)&&execute;
        bool data=(privilege==0?user:!user||(privilege==1&&sum));
        allowed=fetch||(data&&(read||(mxr&&execute)||write));
    }else{
        bool priv=privilege==0?user:!user||(privilege==1&&sum&&access!=0);
        bool kind=false;
        switch(access){
          case 0:kind=execute;break;
          case 1:kind=read||(mxr&&execute);break;
          case 2:kind=write&&(e.flags&1);break;
          case 3:kind=read||write||(mxr&&execute);break;
        }
        allowed=priv&&kind&&(e.flags&2);
    }
    return {hit,hit&&!allowed,pa&UINT64_C(0xffffffffffffff)};
}
static void put(std::array<uint64_t,3>&words,unsigned low,unsigned width,uint64_t value){
    for(unsigned bit=0;bit<width;++bit)if((value>>bit)&1)words[(low+bit)/64]|=UINT64_C(1)<<((low+bit)%64);
}
static void compile(const std::string&source,bool release){
    pid_t pid=fork();require(pid>=0,"fork failed");if(!pid){const char*cc=std::getenv("CC");if(!cc)cc="cc";
        auto output=source+".so";execlp(cc,cc,"-std=c17","-O3","-march=native",release?"-DNDEBUG":"-UNDEBUG","-shared","-fPIC",source.c_str(),"-o",output.c_str(),nullptr);_exit(127);}
    int status;require(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&!WEXITSTATUS(status),"generated C compilation failed");
}
static int host_effect(void*context,const uint64_t*,size_t,uint64_t*,size_t){return *static_cast<bool*>(context)?-1:0;}
static void failed_fill(const std::string&dir){
    for(unsigned mode=0;mode<3;++mode){char error[512];rds_options options{1,mode?RDS_LIFT_TRANSITIONS|RDS_LIFT_PRIMITIVES:1u};
        auto*s=rds_load_with_options((dir+"/tlb-host.rsim").c_str(),&options,error,sizeof error);require(s,error);
        bool fail=false;require(!rds_bind_host(s,nullptr,host_effect,&fail),rds_error(s));std::string library;
        if(mode){auto source=dir+"/tlb-host-"+std::to_string(mode)+".c";require(!rds_emit_c(s,source.c_str(),4096),rds_error(s));compile(source,mode==2);library=source+".so";require(!rds_use_compiled(s,library.c_str()),rds_error(s));}
        auto set=[&](const char*name,uint64_t value){require(!rds_set_u64(s,rds_find_port(s,name),value),rds_error(s));};
        auto fill=[&](uint64_t ppn){std::array<uint64_t,3> words{};put(words,175,1,1);put(words,111,64,0x12345000);put(words,11,44,ppn);put(words,2,7,34);
            require(!rds_set(s,rds_find_port(s,"fill_in"),words.data(),3),rds_error(s));};
        auto check=[&](uint64_t hit,uint64_t address){require(!rds_eval(s),rds_error(s));uint64_t value;
            require(!rds_get_u64(s,rds_find_port(s,"hit"),&value)&&value==hit,"failed fill changed hit");
            if(hit)require(!rds_get_u64(s,rds_find_port(s,"physical_address"),&value)&&value==address,"failed fill changed entry priority");};
        set("enabled",1);set("virtual_address",0x12345000);fill(0xabc);fail=true;
        require(rds_advance(s)!=0,"host failure was not propagated");check(0,0);
        fail=false;require(!rds_advance(s),rds_error(s));check(1,0xabc000);
        fill(0xdef);fail=true;require(rds_advance(s)!=0,"second host failure was not propagated");
        if(mode)require(!rds_use_compiled(s,library.c_str()),rds_error(s));check(1,0xabc000);
        fail=false;fill(0x123);require(!rds_advance(s),rds_error(s));check(1,0xabc000);
        rds_free(s);
    }
}
int main(int argc,char**argv){try{
    require(argc==2,"usage: tlb-test fixture-directory");std::string dir=argv[1];std::mt19937_64 random(785319);
    for(unsigned depth:{2,4,8}){
        std::string prefix=dir+"/tlb-"+std::to_string(depth);std::vector<rds_sim*> engines;std::vector<std::string> libraries;
        for(unsigned mode=0;mode<6;++mode){char error[512];rds_options options{mode==5?4u:1u,mode<3?1u:RDS_LIFT_TRANSITIONS|RDS_LIFT_PRIMITIVES};
            std::string model=prefix+(mode==0?"-raw":mode==1?"-unshared":"")+".rsim";
            auto*s=rds_load_with_options(model.c_str(),&options,error,sizeof error);require(s,error);engines.push_back(s);libraries.emplace_back();
            if(mode>=3){std::string source=prefix+"-mode"+std::to_string(mode)+".c";require(!rds_emit_c(s,source.c_str(),mode==3?2:4096),rds_error(s));
                compile(source,mode!=3);libraries.back()=source+".so";require(!rds_use_compiled(s,libraries.back().c_str()),rds_error(s));}}
        std::vector<Entry> entries(depth);unsigned replacement=0;
        for(unsigned cycle=0;cycle<6000;++cycle){
            if(cycle==3001)for(size_t mode=3;mode<engines.size();++mode)require(!rds_use_compiled(engines[mode],libraries[mode].c_str()),rds_error(engines[mode]));
            bool reset=cycle==0||cycle%503==0,invalidate=cycle%127==0,fill=cycle%11==1||cycle<depth+2,fill_fault=cycle%17==3;
            Entry update;update.va=random();update.ppn=random()&UINT64_C(0xfffffffffff);update.level=(cycle/13)%4;update.flags=(cycle/3)%128;update.valid=true;
            // Repeated and overlapping matches exercise lowest-entry priority,
            // including level3's exact-VPN match and large-page address default.
            if(cycle%3)update.va=UINT64_C(0xffffffc080100000)+(cycle%4)*4096;
            uint64_t va=cycle%5?entries[(cycle/5)%depth].va:random();va=(va&~UINT64_C(4095))|(random()&4095);
            uint64_t pva=cycle%3?entries[(cycle/7)%depth].va:random();pva=(pva&~UINT64_C(4095))|(random()&4095);
            bool enabled=cycle%7!=0,penabled=cycle%9!=0,sum=(cycle/4)&1,mxr=(cycle/8)&1,psum=(cycle/16)&1,pmxr=(cycle/32)&1;
            unsigned access=cycle%4,privilege=(cycle/4)%4,pprivilege=(cycle/8)%4;
            auto expected=lookup(entries,va,enabled,access,privilege,sum,mxr,false),probe=lookup(entries,pva,penabled,0,pprivilege,psum,pmxr,true);
            std::array<uint64_t,3> fill_words{};put(fill_words,175,1,fill);put(fill_words,111,64,update.va);put(fill_words,55,56,random());
            put(fill_words,11,44,update.ppn);put(fill_words,9,2,update.level);put(fill_words,2,7,update.flags);put(fill_words,1,1,fill_fault);put(fill_words,0,1,cycle&1);
            const char*names[]={"reset","invalidate_all","virtual_address","enabled","access","privilege","sum","mxr","probe_virtual_address","probe_enabled","probe_privilege","probe_sum","probe_mxr"};
            uint64_t values[]={reset,invalidate,va,enabled,access,privilege,sum,mxr,pva,penabled,pprivilege,psum,pmxr};
            for(size_t mode=0;mode<engines.size();++mode){auto*s=engines[mode];
                for(unsigned i=0;i<13;++i)require(!rds_set_u64(s,rds_find_port(s,names[i]),values[i]),std::string("set ")+names[i]+": "+rds_error(s));
                require(!rds_set(s,rds_find_port(s,"fill_in"),fill_words.data(),3),rds_error(s));require(!rds_eval(s),rds_error(s));
                const char*outputs[]={"hit","fault","physical_address","probe_hit","probe_fault","probe_physical_address"};
                for(unsigned i=0;i<6;++i){uint64_t actual;require(!rds_get(s,rds_find_port(s,outputs[i]),&actual,1),rds_error(s));
                    uint64_t wanted=i<3?expected[i]:probe[i-3];require(actual==wanted,"TLB depth "+std::to_string(depth)+" mode "+std::to_string(mode)+" cycle "+std::to_string(cycle)+" "+outputs[i]+" expected "+std::to_string(wanted)+" got "+std::to_string(actual));}}
            for(auto*s:engines)require(!rds_advance(s),rds_error(s));
            if(reset||invalidate){entries.assign(depth,{});replacement=0;}else if(fill&&!fill_fault){entries[replacement]=update;replacement=(replacement+1)%depth;}
        }
        for(auto*s:engines)rds_free(s);
    }
    failed_fill(dir);
    std::cout<<"TLB: original RTL, shared/unshared queries, debug/release, reset/fill/invalidate, failed publication and Sv39 demand/cache-management/probe permission replay passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
