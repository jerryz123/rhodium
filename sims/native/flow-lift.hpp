// Emits a restricted scalar flow IR directly as a snapshot-preserving software step.
#pragma once
#include "../../rhodium/sim/compiler/model.hpp"
#include <set>
#include <sstream>
#include <stdexcept>

// Experimental backend: fixed scalar ports, total pure operations, ordinary
// one-entry queues and scoreboards. Reject other contracts rather than silently
// weakening them. No workload, name, object ID or traffic assumptions are used.
static std::string lift_flow(const rds::Model &m) {
    using namespace rds;
    auto check=[](bool b){if(!b)throw std::runtime_error("unsupported flow-lift contract");};
    m.validate();
    for(auto key:{"registers","memories","writes","reads","assertions"})check(m.metadata.at(key).empty());
    check(m.metadata.at("ports").size()==12);
    for(auto w:m.widths)check(w<=64);
    std::set<Id> available;
    std::ostringstream out;
    out<<"/* Executes a scalar flow graph with direct state and demand-driven pure expressions. */\n"
          "#include <stdint.h>\n#include <stdlib.h>\n"
          "typedef struct {uint64_t x[8];} flow_input;\n"
          "typedef struct {uint64_t x[4];} flow_output;\n"
          "typedef struct {uint64_t unused;\n";
    unsigned objects=m.metadata.at("objects").size();
    for(unsigned j=0;j<objects;++j){
        const auto &o=m.metadata["objects"][j];
        unsigned kind=o[0],width=o[1],depth=o[2],flags=o[3];
        check(flags==0&&((kind==1&&depth==1&&width>0&&width<=64)||(kind==4&&width==depth&&width<=64)));
        if(kind==4){
            check(o[4].size()==5);
            for(unsigned k:{2u,4u})check(m.widths[o[4][k]]<=6&&(UINT64_C(1)<<m.widths[o[4][k]])<=depth);
        }
        out<<"uint64_t data"<<j<<";unsigned full"<<j<<";\n";
    }
    out<<"} flow_state;\n";
    auto call=[](Id id){return "v"+std::to_string(id)+"(s,i)";};
    for(unsigned j=0;j<8;++j){
        const auto &p=m.metadata["ports"][j];check(p[0]==0);Id id=p[1];check(available.insert(id).second);
        // The typed entry contract already bounds inputs. Repeated masking is
        // unnecessary in release and can change mux lowering into jump tables.
        out<<"static inline uint64_t v"<<id<<"(const flow_state*s,const flow_input*i){(void)s;return i->x["<<j<<"];}\n";
    }
    for(const auto &op:m.ops){
        check(!available.count(op.out));for(Id a:op.args)check(available.count(a));
        auto a=[&](unsigned j){return call(op.args.at(j));};
        std::string expression;
        switch(op.code){
        case 0:expression="UINT64_C("+number(op.imm).convert_to<std::string>()+")";break;
        case 1:case 18:expression=a(0);break;
        case 2:expression="~"+a(0);break;
        case 3:case 4:case 5:case 6:case 7:case 8:{
            const char* operators[]={"&","|","^","+","-","*"};
            expression=a(0)+operators[op.code-3]+a(1);break;
        }
        case 9:case 10:
            if(m.widths[op.args[1]]<=6)expression=a(0)+(op.code==9?"<<":">>")+a(1);
            else expression=a(1)+"<64?("+a(0)+(op.code==9?"<<":">>")+"("+a(1)+"&63)):0";
            break;
        case 12:expression=a(0)+"=="+a(1);break;
        case 13:expression=a(0)+"<"+a(1);break;
        case 15:
            expression=a(1);
            for(unsigned j=op.args.size()-1;j>=2;--j)expression=a(0)+"==UINT64_C("+std::to_string(op.imm[j-2])+")?"+a(j)+":("+expression+")";
            break;
        case 17:expression=a(0)+">>"+std::to_string(op.imm[0]);break;
        case 29:{
            check(op.args.empty());unsigned id=op.imm[0],q=op.imm[1];const auto &o=m.metadata["objects"][id];
            if(o[0]==4){check(q==0);expression="s->data"+std::to_string(id);}
            else{check(q<=3);expression=q==3?"s->data"+std::to_string(id):(q==1?"!":"")+std::string("s->full")+std::to_string(id);}
            break;
        }
        default:check(false);
        }
        available.insert(op.out);
        out<<"static inline uint64_t v"<<op.out<<"(const flow_state*s,const flow_input*i){(void)s;(void)i;return ("<<expression<<")&UINT64_C("<<mask(m.widths[op.out])<<");}\n";
    }
    out<<"void *flow_create(void){return calloc(1,sizeof(flow_state));}\nvoid flow_destroy(void*p){free(p);}\n"
          "static inline __attribute__((always_inline)) flow_output flow_core(void*p,const flow_input*i){flow_state*s=p;flow_output o;\n";
    out<<"#ifndef NDEBUG\n";
    for(unsigned j=0;j<8;++j){unsigned w=m.widths[m.metadata["ports"][j][1]];if(w<64)out<<"if(i->x["<<j<<"]>>"<<w<<")abort();\n";}
    out<<"#endif\n";
    for(unsigned j=0;j<4;++j){const auto &port=m.metadata["ports"][8+j];check(port[0]==1&&available.count(port[1]));out<<"o.x["<<j<<"]="<<call(port[1])<<";\n";}
    // Compute every proposed update from the old snapshot before publishing any.
    for(unsigned j=0;j<objects;++j){
        const auto &o=m.metadata["objects"][j];auto in=[&](unsigned k){return call(o[4][k]);};
        if(o[0]==4){
            check(o[4].size()==5);
            out<<"uint64_t next"<<j<<"="<<in(0)<<"?0:(s->data"<<j<<"|("<<in(1)<<"?(UINT64_C(1)<<"<<in(2)<<"):0))&~("<<in(3)<<"?(UINT64_C(1)<<"<<in(4)<<"):0);\n";
        }else{
            check(o[4].size()==4);
            out<<"unsigned en"<<j<<"=!s->full"<<j<<"&&"<<in(1)<<",de"<<j<<"=s->full"<<j<<"&&"<<in(3)<<";\n"
               <<"uint64_t next"<<j<<"=en"<<j<<"?"<<in(2)<<":s->data"<<j<<";\n"
               <<"unsigned full"<<j<<"="<<in(0)<<"?0:s->full"<<j<<"+en"<<j<<"-de"<<j<<";\n";
        }
    }
    for(unsigned j=0;j<objects;++j){out<<"s->data"<<j<<"=next"<<j<<";\n";if(m.metadata["objects"][j][0]==1)out<<"s->full"<<j<<"=full"<<j<<";\n";}
    out<<"return o;}\nflow_output flow_lifted(void*p,const flow_input*i){return flow_core(p,i);}\n";return out.str();
}
