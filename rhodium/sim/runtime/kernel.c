/* Validates, interprets and emits pure contract programs without cycle allocation. */
#define _POSIX_C_SOURCE 200809L
#include "codegen.h"
#include "kernel-format.h"

typedef struct {
    rds_sim sim;
    struct rds_kernel_format format;
    rds_value values[RDS_KERNEL_VALUES];
    rds_op ops[RDS_KERNEL_OPS];
} kernel_view;

static bool decode(const rds_sim *parent,const rds_op *o,kernel_view *k){
    memset(&k->sim,0,sizeof k->sim);
    if(!rds_kernel_parse(parent->imm+o->imm,o->nimm,&k->format) || k->format.inputs!=o->nargs)return false;
    const struct rds_kernel_format *p=&k->format;
    for(uint32_t i=0;i<p->values;++i){
        k->values[i]=(rds_value){p->widths[i],(p->widths[i]+63)/64,p->offsets[i],SIZE_MAX,RDS_NONE};
        if(i<p->inputs && p->widths[i]!=parent->values[parent->args[o->args+i]].width)return false;
    }
    if(p->widths[p->values-1]!=parent->values[o->out].width)return false;
    for(uint32_t i=0;i<p->count;++i){const struct rds_kernel_instruction *a=&p->ops[i];
        k->ops[i]=(rds_op){a->code,a->out,a->args,a->nargs,a->imm,a->nimm};
    }
    k->sim.nv=p->values;k->sim.no=p->count;k->sim.na=p->arguments;k->sim.ni=o->nimm;
    k->sim.values=k->values;k->sim.ops=k->ops;k->sim.args=k->format.args;
    k->sim.imm=parent->imm+o->imm;k->sim.value_words=p->words;
    k->sim.linear_decode=true;k->sim.set_clear=parent->set_clear;k->sim.strict=parent->strict;
    return true;
}
bool rds_kernel_validate(const rds_sim *s,const rds_op *o){
    kernel_view k;
    return decode(s,o,&k) && rds_validate_graph(&k.sim,k.format.inputs);
}
uint64_t rds_kernel_cost(const rds_sim *s,const rds_op *o){
    kernel_view k;uint64_t work=0;
    if(!decode(s,o,&k))return UINT32_MAX;
    for(uint32_t i=0;i<k.sim.no;++i)work+=rds_operation_cost(&k.sim,&k.ops[i]);
    return work;
}
int rds_kernel_execute(rds_sim *s,const rds_op *o){
    kernel_view k;uint64_t storage[RDS_KERNEL_WORDS];
    if(!decode(s,o,&k))return rds_fail(s,"invalid contract program");
    k.sim.arena=storage;
    for(uint32_t i=0;i<k.format.inputs;++i)
        memcpy(rds_data(&k.sim,i),rds_data(s,s->args[o->args+i]),k.values[i].words*8);
    for(uint32_t i=0;i<k.sim.no;++i)if(rds_execute(&k.sim,&k.ops[i]))return rds_fail(s,k.sim.error);
    memcpy(rds_data(s,o->out),rds_data(&k.sim,k.sim.nv-1),s->values[o->out].words*8);
    return 0;
}
typedef struct {
    uint32_t parent[RDS_KERNEL_ARGS+1],depth[RDS_KERNEL_ARGS+1],count;
    uint32_t place[RDS_KERNEL_VALUES],arm[RDS_KERNEL_ARGS];
} kernel_scopes;
/* Place a shared value in the closest common enclosing branch of all its
 * consumers. A diamond wholly inside one arm remains conditional; sharing
 * across different arms or with a selector moves it to their common scope. */
static void require_scope(kernel_scopes *p,uint32_t v,uint32_t scope){
    uint32_t old=p->place[v];
    if(old==RDS_NONE){p->place[v]=scope;return;}
    while(old!=scope){if(p->depth[old]>=p->depth[scope])old=p->parent[old];else scope=p->parent[scope];}
    p->place[v]=old;
}
static void plan_scopes(kernel_view *k,kernel_scopes *p,bool lazy){
    memset(p,0,sizeof *p);p->count=1;
    for(uint32_t v=0;v<k->sim.nv;++v)p->place[v]=RDS_NONE;
    p->place[k->sim.nv-1]=0;
    for(uint32_t i=k->sim.no;i-->0;){const rds_op *o=&k->ops[i];uint32_t scope=p->place[o->out];
        if(scope==RDS_NONE)continue;
        for(uint32_t j=0;j<o->nargs;++j){uint32_t target=scope;
            if(lazy && o->code==RDS_MUX && j){target=p->count++;p->parent[target]=scope;p->depth[target]=p->depth[scope]+1;}
            p->arm[o->args+j]=target;require_scope(p,k->sim.args[o->args+j],target);
        }
    }
}
static void emit_scope(FILE *,kernel_view *,const kernel_scopes *,uint32_t);
static void emit_arm(FILE *f,kernel_view *k,const rds_op *o,uint32_t j,const kernel_scopes *p){
    uint32_t target=p->arm[o->args+j];
    if(target!=p->place[o->out])emit_scope(f,k,p,target);
    fprintf(f,"memcpy(cv%u,cv%u,%u);\n",o->out,k->sim.args[o->args+j],k->values[o->out].words*8);
}
/* Constant geometry must stay constant C geometry. A generic pack loop makes
 * its array of widths/addresses dynamic to the host optimizer and recreates
 * precisely the extraction/materialization overhead this pass is removing. */
static bool emit_projection(FILE *f,kernel_view *k,const rds_op *o){
    uint32_t code=o->code;const uint32_t *a=k->sim.args+o->args;
    if(code!=RDS_COPY && code!=RDS_SLICE && code!=RDS_ZEXT && code!=RDS_SEXT && code!=RDS_PACK)return false;
    for(uint32_t w=0;w<k->values[o->out].words;++w){
        uint32_t low=w*64,bits=k->values[o->out].width-low;if(bits>64)bits=64;
        fprintf(f,"cv%u[%u]=0",o->out,w);uint32_t position=0;
        for(uint32_t j=0;j<o->nargs;++j){
            uint32_t start=code==RDS_SLICE?(uint32_t)k->sim.imm[o->imm]:0;
            uint32_t width=k->values[a[j]].width-start;
            uint32_t begin=position>low?position:low,stop=position+width<low+bits?position+width:low+bits;
            if(stop>begin){uint32_t source=start+begin-position,take=stop-begin;
                fprintf(f,"|(((cv%u[%u]>>%u)",a[j],source/64,source%64);
                if(source%64+take>64)fprintf(f,"|(cv%u[%u]<<%u)",a[j],source/64+1,64-source%64);
                fprintf(f,")&UINT64_C(%llu))<<%u",(unsigned long long)rds_mask(take),begin-low);
            }
            position+=width;
        }
        if(code==RDS_SEXT && low+bits>k->values[a[0]].width){uint32_t aw=k->values[a[0]].width,shift=aw>low?aw-low:0;
            fprintf(f,"|((UINT64_C(0)-((cv%u[%u]>>%u)&1))&(UINT64_MAX<<%u))",a[0],(aw-1)/64,(aw-1)%64,shift);
        }
        fprintf(f,";cv%u[%u]&=UINT64_C(%llu);\n",o->out,w,(unsigned long long)rds_mask(bits));
    }
    return true;
}
static void emit_node(FILE *f,kernel_view *k,uint32_t i,const kernel_scopes *p){
    const rds_op *o=&k->ops[i];const uint32_t *a=k->sim.args+o->args;
    if(o->code==RDS_MUX){
        // Local addresses cannot use the outer arena's cg_ptr table. Explicit
        // arms also let the host compiler eliminate private payload storage.
        for(uint32_t j=2;j<o->nargs;++j){
            fprintf(f,"%sif(!cg_cmp(cv%u,(const uint64_t[]){",j==2?"":"else ",a[0]);
            for(uint32_t w=0;w<k->values[a[0]].words;++w)
                fprintf(f,"UINT64_C(%llu),",(unsigned long long)k->sim.imm[o->imm+(j-2)*k->values[a[0]].words+w]);
            fprintf(f,"},%u)){\n",k->values[a[0]].words);
            emit_arm(f,k,o,j,p);fputs("}\n",f);
        }
        fputs(o->nargs>2?"else {\n":"{\n",f);emit_arm(f,k,o,1,p);fputs("}\n",f);
    }else{
        if(!emit_projection(f,k,o))rds_c_operation(f,&k->sim,o);
    }
}
static void emit_scope(FILE *f,kernel_view *k,const kernel_scopes *p,uint32_t scope){
    for(uint32_t i=0;i<k->sim.no;++i)if(p->place[k->ops[i].out]==scope)emit_node(f,k,i,p);
}
void rds_c_kernel_local(FILE *f,const rds_sim *s,const rds_op *o){
    kernel_view k;
    if(!decode(s,o,&k)){fputs("return cg_fail(c,\"invalid contract program\");",f);return;}
    k.sim.emit_private=true;
    fputs("{\n",f);
    bool cached=rds_c_kernel_cacheable(s,o);uint32_t id=0;
    if(cached){while(id<s->no && s->ops[id].out!=o->out)++id;if(id==s->no)cached=false;}
    if(cached){uint32_t shift=0;fputs("uint64_t contract_key=UINT64_C(0x8000000000000000)",f);
        for(uint32_t i=0;i<k.format.inputs;++i){
            fprintf(f,"|((contract_values[%zu]&UINT64_C(%llu))<<%u)",k.values[i].offset,(unsigned long long)rds_mask(k.values[i].width),shift);
            shift+=k.values[i].width;}
        fprintf(f,";if(h->contract_cache_%u[0]==contract_key){contract_values[%zu]=h->contract_cache_%u[1];}else{\n",id,k.values[k.sim.nv-1].offset,id);
    }
    for(uint32_t i=0;i<k.sim.nv;++i){fprintf(f,"uint64_t cv%u[%u];",i,k.values[i].words);
        if(i<k.format.inputs)fprintf(f,"memcpy(cv%u,contract_values+%zu,%u);",i,k.values[i].offset,k.values[i].words*8);
        fputc('\n',f);
    }
    kernel_scopes scopes;plan_scopes(&k,&scopes,s->imm[o->imm]!=1);
    emit_scope(f,&k,&scopes,0);
    fprintf(f,"memcpy(contract_values+%zu,cv%u,%u);",k.values[k.sim.nv-1].offset,k.sim.nv-1,k.values[k.sim.nv-1].words*8);
    if(cached)fprintf(f,"h->contract_cache_%u[1]=cv%u[0];h->contract_cache_%u[0]=contract_key;}\n",id,k.sim.nv-1,id);
    fputs("}\n",f);
}
bool rds_c_kernel_cacheable(const rds_sim *s,const rds_op *o){
    if(!s->schedule||s->schedule->count!=1||o->code!=RDS_CONTRACT||s->imm[o->imm]!=3||s->values[o->out].width>64)return false;
    uint64_t bits=0;for(uint32_t i=0;i<o->nargs;++i)bits+=s->values[s->args[o->args+i]].width;
    return bits<=63;
}
void rds_c_kernel(FILE *f,const rds_sim *s,const rds_op *o){
    kernel_view k;
    if(!decode(s,o,&k)){fputs("return cg_fail(c,\"invalid contract program\");",f);return;}
    fputs("{/* contract program: private intermediates */\nuint64_t *contract_out=",f);
    rds_c_pointer(f,rds_c_ref(s,o->out));fputs(";\n",f);
    fprintf(f,"uint64_t contract_values[%u];\n",k.format.words);
    for(uint32_t i=0;i<k.format.inputs;++i){fprintf(f,"memcpy(contract_values+%zu,",k.values[i].offset);
        rds_c_pointer(f,rds_c_ref(s,s->args[o->args+i]));fprintf(f,",%u);\n",k.values[i].words*8);}
    rds_c_kernel_local(f,s,o);
    fprintf(f,"memcpy(contract_out,contract_values+%zu,%u);}\n",k.values[k.sim.nv-1].offset,s->values[o->out].words*8);
}
