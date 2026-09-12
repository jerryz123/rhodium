/* Proves static offer cuts, groups state owners, and emits one-barrier bulk transitions. */
#define _POSIX_C_SOURCE 200809L
#include "codegen.h"
#include "kernel-format.h"
#include <inttypes.h>
#define MIXED (UINT32_MAX-1)
struct rds_offer_plan {
    rds_sim model;
    rds_schedule view;
    uint32_t groups, words, count;
    uint32_t *group, *owner, *definition, *dependency, *offset, *input_port;
    unsigned char *state, *offer, *update, *control, *publish;
    uint64_t *cost;
    bool *pending, *staged;
    uint32_t *prepare;
};
static void *copy_array(const void *source,size_t count,size_t width){
    void *p=calloc(count?count:1,width);if(p&&count)memcpy(p,source,count*width);return p;
}
void rds_offer_free(rds_offer_plan *p){
    if(!p)return;
    if(p->model.objects)for(uint32_t i=0;i<p->model.nx;++i)free(p->model.objects[i].inputs);
    free(p->model.objects);free(p->model.values);free(p->model.ops);free(p->model.args);free(p->model.imm);free(p->model.regs);free(p->model.ports);
    free(p->group);free(p->owner);free(p->definition);free(p->dependency);free(p->offset);free(p->input_port);
    free(p->state);free(p->offer);free(p->update);free(p->control);free(p->publish);free(p->cost);free(p->pending);free(p->staged);free(p->prepare);free(p);
}
static uint32_t root(uint32_t *parent,uint32_t i){while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;}
static void unite(uint32_t *parent,uint32_t a,uint32_t b){a=root(parent,a);b=root(parent,b);if(a!=b)parent[b]=a;}
static uint32_t combine(uint32_t a,uint32_t b){return a==RDS_NONE?b:b==RDS_NONE||a==b?a:MIXED;}
static bool close_update(rds_offer_plan *p,unsigned char *needed){
    const rds_sim *m=&p->model;
    for(uint32_t i=m->no;i--;){const rds_op *o=&m->ops[i];if(!needed[o->out])continue;
        if(p->dependency[o->out]<p->groups&&!p->state[o->out]){p->offer[o->out]=1;continue;}
        /* Queries with register-dependent requests cannot be precomputed from
         * the new owned state. Keep the ordinary schedule for such a model. */
        if(o->code==RDS_OBJECT_QUERY)return false;
        for(uint32_t a=0;a<o->nargs;++a)needed[m->args[o->args+a]]=1;
    }
    return true;
}
rds_offer_plan *rds_offer_build(const rds_sim *s){
    if(s->schedule->count<2||!s->nx||s->nx>2048||s->nv>200000||s->nm||s->nw||s->ns||s->nc||s->schedule->tail_count)return NULL;
    for(uint32_t i=0;i<s->nx;++i)if(!((s->objects[i].kind==1&&!s->objects[i].flags)||s->objects[i].kind==10))return NULL;
    for(uint32_t i=0;i<s->no;++i){const rds_op *o=&s->ops[i];
        if(o->code>=RDS_INDEX&&o->code<=RDS_READ)return NULL;
        if(o->code==RDS_CONTRACT){struct rds_kernel_format k;if(!rds_kernel_parse(s->imm+o->imm,o->nimm,&k))return NULL;
            for(uint32_t j=0;j<k.count;++j)if(k.ops[j].code>=RDS_INDEX&&k.ops[j].code<=RDS_READ)return NULL;}}
    rds_offer_plan *p=aligned_alloc(64,sizeof *p);if(!p)return NULL;memset(p,0,sizeof *p);
    p->model=*s;
    /* Only these copies are owned. Other source pointers remain borrowed until
     * emission; no generated execution dereferences this compiler graph. */
    p->model.values=NULL;p->model.ops=NULL;p->model.args=NULL;p->model.imm=NULL;p->model.regs=NULL;p->model.objects=NULL;p->model.ports=NULL;
#define COPY(field,count) do{p->model.field=copy_array(s->field,s->count,sizeof *s->field);if(!p->model.field)goto fail;}while(0)
    COPY(values,nv);COPY(ops,no);COPY(args,na);COPY(imm,ni);COPY(regs,nr);COPY(ports,np);
#undef COPY
    p->model.objects=calloc(s->nx,sizeof *p->model.objects);if(!p->model.objects)goto fail;
    for(uint32_t i=0;i<s->nx;++i){p->model.objects[i]=s->objects[i];p->model.objects[i].inputs=copy_array(s->objects[i].inputs,s->objects[i].ni,sizeof(uint32_t));if(!p->model.objects[i].inputs)goto fail;}
#define ALLOC(field,count,type) do{p->field=calloc((count)?(count):1,sizeof(type));if(!p->field)goto fail;}while(0)
    ALLOC(group,s->nx,uint32_t);ALLOC(definition,s->nv,uint32_t);ALLOC(dependency,s->nv,uint32_t);
    ALLOC(offset,s->nv,uint32_t);ALLOC(input_port,s->nv,uint32_t);ALLOC(state,s->nv,unsigned char);ALLOC(offer,s->nv,unsigned char);
    ALLOC(pending,s->nx,bool);ALLOC(staged,s->nr,bool);ALLOC(prepare,s->nx,uint32_t);
    for(uint32_t i=0;i<s->nv;++i)p->definition[i]=p->dependency[i]=p->offset[i]=p->input_port[i]=RDS_NONE;
    for(uint32_t i=0;i<s->nx;++i){p->group[i]=i;p->prepare[i]=RDS_NONE;}
    for(uint32_t i=0;i<s->no;++i)p->definition[s->ops[i].out]=i;
    for(uint32_t i=0;i<s->nr;++i){p->state[s->regs[i].q]=1;p->model.values[s->regs[i].q].state=s->regs[i].next;}
    /* Bit 0 marks FF state; bit 1 marks changing public inputs. Neither may
     * appear in an offer, which must remain valid across batch boundaries. */
    for(uint32_t i=0;i<s->np;++i)if(!s->ports[i].direction){p->input_port[s->ports[i].value]=i;p->state[s->ports[i].value]|=2;}
    /* Close each matcher's request cone over snapshot state, never over its
     * accepted-update inputs. This preserves the forward/reverse distinction. */
    unsigned char *seen=calloc(s->nv?s->nv:1,1);if(!seen)goto fail;
    for(uint32_t i=0;i<s->no;++i){const rds_op *o=&s->ops[i];if(o->code!=RDS_OBJECT_QUERY||s->objects[s->imm[o->imm]].kind!=10)continue;
        memset(seen,0,s->nv);for(uint32_t a=0;a<o->nargs;++a)seen[s->args[o->args+a]]=1;
        for(uint32_t j=i;j--;){const rds_op *d=&s->ops[j];if(!seen[d->out])continue;
            if(d->code==RDS_OBJECT_QUERY)unite(p->group,(uint32_t)s->imm[o->imm],(uint32_t)s->imm[d->imm]);
            for(uint32_t a=0;a<d->nargs;++a)seen[s->args[d->args+a]]=1;
        }
    }
    free(seen);
    uint32_t *ids=malloc(s->nx*sizeof *ids);if(!ids)goto fail;
    for(uint32_t i=0;i<s->nx;++i)ids[i]=RDS_NONE;
    for(uint32_t i=0;i<s->nx;++i){uint32_t r=root(p->group,i);if(ids[r]==RDS_NONE)ids[r]=p->groups++;}
    uint32_t *mapped=malloc(s->nx*sizeof *mapped);if(!mapped){free(ids);goto fail;}
    for(uint32_t i=0;i<s->nx;++i)mapped[i]=ids[root(p->group,i)];
    free(p->group);p->group=mapped;
    free(ids);
    if(p->groups<s->schedule->count||(uint64_t)p->groups*s->nv*3>64u*1024u*1024u)goto fail;
    ALLOC(owner,p->groups,uint32_t);ALLOC(cost,p->groups,uint64_t);
    ALLOC(update,(size_t)p->groups*s->nv,unsigned char);ALLOC(publish,(size_t)p->groups*s->nv,unsigned char);
    ALLOC(control,(size_t)p->groups*s->nv,unsigned char);
    for(uint32_t i=0;i<s->no;++i){const rds_op *o=&s->ops[i];uint32_t g=RDS_NONE;
        for(uint32_t a=0;a<o->nargs;++a){uint32_t v=s->args[o->args+a];g=combine(g,p->dependency[v]);p->state[o->out]|=p->state[v];}
        if(o->code==RDS_OBJECT_QUERY)g=combine(g,p->group[s->imm[o->imm]]);
        p->dependency[o->out]=g;
    }
    for(uint32_t i=0;i<s->nx;++i){uint32_t g=p->group[i];p->model.objects[i].owner=g;p->cost[g]+=16+8*s->objects[i].words;
        for(uint32_t a=0;a<s->objects[i].ni;++a){uint32_t v=s->objects[i].inputs[a];if(v!=RDS_NONE){p->update[(size_t)g*s->nv+v]=1;
                if(s->objects[i].kind!=1||a!=2)p->control[(size_t)g*s->nv+v]=1;}}}
    for(uint32_t g=0;g<p->groups;++g)if(!close_update(p,p->update+(size_t)g*s->nv))goto fail;
    for(uint32_t i=0;i<s->nr;++i){const rds_reg *r=&s->regs[i];uint32_t g=p->dependency[r->d];
        if(g>=p->groups){g=0;uint64_t best=UINT64_MAX;
            for(uint32_t j=0;j<p->groups;++j){uint64_t cost=p->cost[j]+(p->update[(size_t)j*s->nv+r->q]?0:UINT64_C(1)<<32);if(cost<best){best=cost;g=j;}}}
        p->model.regs[i].owner=g;p->cost[g]+=s->values[r->q].words;
        unsigned char *u=p->update+(size_t)g*s->nv;u[r->d]=1;if(r->reset!=RDS_NONE){u[r->reset]=1;u[r->reset_value]=1;}
        unsigned char *c=p->control+(size_t)g*s->nv;c[r->d]=1;if(r->reset!=RDS_NONE){c[r->reset]=1;c[r->reset_value]=1;}
    }
    for(uint32_t g=0;g<p->groups;++g)if(!close_update(p,p->update+(size_t)g*s->nv)||!close_update(p,p->control+(size_t)g*s->nv))goto fail;
    for(uint32_t v=0;v<s->nv;++v)if(p->offer[v])p->publish[(size_t)p->dependency[v]*s->nv+v]=1;
    for(uint32_t g=0;g<p->groups;++g){unsigned char *u=p->update+(size_t)g*s->nv,*v=p->publish+(size_t)g*s->nv;
        for(uint32_t i=s->no;i--;){const rds_op *o=&s->ops[i];if(v[o->out]){
                if(p->state[o->out]||(p->dependency[o->out]!=g&&p->dependency[o->out]!=RDS_NONE))goto fail;
                for(uint32_t a=0;a<o->nargs;++a)v[s->args[o->args+a]]=1;
            }}
        for(uint32_t i=0;i<s->no;++i){const rds_op *o=&s->ops[i];p->cost[g]+=((u[o->out]&&!p->offer[o->out])+v[o->out])*rds_operation_cost(s,o);}
        for(uint32_t id=0;id<s->nv;++id)if((u[id]||v[id])&&p->definition[id]==RDS_NONE&&!p->state[id]&&p->input_port[id]==RDS_NONE)goto fail;
    }
    /* Grow balanced connected regions using actual offer traffic as edge weight. */
    uint64_t *edges=calloc((size_t)p->groups*p->groups,sizeof *edges);if(!edges)goto fail;
    uint64_t remaining=0;uint32_t unassigned=p->groups;
    for(uint32_t g=0;g<p->groups;++g){p->owner[g]=RDS_NONE;remaining+=p->cost[g];
        for(uint32_t v=0;v<s->nv;++v)if(p->update[(size_t)g*s->nv+v]&&p->offer[v]&&p->dependency[v]!=g){uint32_t other=p->dependency[v];edges[(size_t)g*p->groups+other]+=s->values[v].words;edges[(size_t)other*p->groups+g]+=s->values[v].words;}}
    for(uint32_t lane=0;lane<s->schedule->count;++lane){uint64_t load=0,target=(remaining+s->schedule->count-lane-1)/(s->schedule->count-lane);bool first=true;
        while(unassigned&&(lane+1==s->schedule->count||first||(load<target&&unassigned>s->schedule->count-lane-1))){uint32_t best=RDS_NONE;uint64_t score=0;
            for(uint32_t g=0;g<p->groups;++g)if(p->owner[g]==RDS_NONE){uint64_t touch=0;for(uint32_t j=0;j<p->groups;++j)if(p->owner[j]==lane)touch+=edges[(size_t)g*p->groups+j];
                if(best==RDS_NONE||touch>score){best=g;score=touch;}}
            p->owner[best]=lane;load+=p->cost[best];remaining-=p->cost[best];--unassigned;first=false;
        }
    }
    free(edges);
    for(uint32_t lane=0;lane<s->schedule->count;++lane){p->words=(p->words+7)&~7u;
        for(uint32_t v=0;v<s->nv;++v)if(p->offer[v]&&p->owner[p->dependency[v]]==lane){p->offset[v]=p->words;p->words+=s->values[v].words;++p->count;}}
    p->words=(p->words+7)&~7u;if(!p->words||p->words>1024*1024)goto fail;
    p->view.count=s->schedule->count;p->model.schedule=&p->view;p->model.emit_private=true;p->model.linear_decode=true;
    p->model.emit_offsets=NULL;p->model.emit_used=NULL;p->model.emit_small=NULL;p->model.emit_constants=NULL;p->model.emit_destination=NULL;
    return p;
fail:rds_offer_free(p);return NULL;
#undef ALLOC
}
uint32_t rds_c_offer_owner(const rds_sim *s,uint32_t id){const rds_offer_plan *p=s->schedule->offers;return p?p->owner[p->group[id]]:s->objects[id].owner;}
void rds_c_offer_layout(FILE *f,const rds_sim *s){const rds_offer_plan *p=s->schedule->offers;if(!p)return;fprintf(f,"_Alignas(64) uint64_t bulk_offers[2][%u];",p->words);
    for(uint32_t lane=0;lane<s->schedule->count;++lane){bool first=true;for(uint32_t g=0;g<p->groups;++g)if(p->owner[g]==lane){fprintf(f,"%sbool offer_same_%u;",first?"_Alignas(64) ":"",g);first=false;}}
}
static void emit_values(FILE *f,const rds_sim *s,rds_offer_plan *p,uint32_t g,bool publish){
    const rds_sim *m=&p->model;const unsigned char *needed=(publish?p->publish:p->update)+(size_t)g*m->nv;
    for(uint32_t v=0;v<m->nv;++v)if(needed[v]){
        if(!publish&&p->offer[v])fprintf(f,"const uint64_t*cv%u=h->bulk_offers[bank]+%u;\n",v,p->offset[v]);
        else if(p->definition[v]==RDS_NONE){fprintf(f,"const uint64_t*cv%u=",v);
            if(p->state[v]&1)fprintf(f,"q+%zu",m->values[v].state);
            else rds_c_pointer(f,rds_c_ref(s,s->ports[p->input_port[v]].value));
            fputs(";\n",f);
        }else fprintf(f,"uint64_t cv%u[%u];\n",v,m->values[v].words);
    }
    if(publish){for(uint32_t i=0;i<m->no;++i)if(needed[m->ops[i].out])rds_c_operation(f,m,&m->ops[i]);return;}
    const unsigned char *control=p->control+(size_t)g*m->nv;bool payload=false;
    for(uint32_t i=0;i<m->no;++i){const rds_op *o=&m->ops[i];if(!needed[o->out]||p->offer[o->out])continue;
        if(control[o->out])rds_c_operation(f,m,o);else payload=true;}
    bool registers=false;for(uint32_t i=0;i<m->nr;++i)registers|=m->regs[i].owner==g;
    if(!registers){
        /* These offers depend only on owned object state. If no transition
         * changes that state, synchronize the alternate bank once, then reuse
         * both copies. Preparation scratch is recomputed by ordinary stepping. */
        fputs("if(!(0",f);
        for(uint32_t id=0;id<m->nx;++id){const rds_object *o=&m->objects[id];if(p->group[id]!=g)continue;
            fprintf(f,"||cv%u[0]",o->inputs[0]);
            if(o->kind==1){char count[64];rds_c_object_view(count,sizeof count,m,id,false);
                fprintf(f,"||(cv%u[0]&&%s<%u)||(cv%u[0]&&%s!=0)",o->inputs[1],count,o->depth,o->inputs[3],count);
            }else if(o->flags&32)fprintf(f,"||cv%u[0]",o->inputs[o->depth+1]);
            else for(uint32_t col=0;col<o->depth;++col)fprintf(f,"||cv%u[0]",o->inputs[1+o->width*o->depth+col]);
        }
        fprintf(f,")){if(!h->offer_same_%u){",g);
        for(uint32_t v=0;v<m->nv;++v)if(p->offer[v]&&p->dependency[v]==g)fprintf(f,"memcpy(h->bulk_offers[bank^1]+%u,h->bulk_offers[bank]+%u,%u);",p->offset[v],p->offset[v],m->values[v].words*8);
        fprintf(f,"h->offer_same_%u=true;}return 0;}\n",g);
    }
    if(payload){
        /* Only FIFO payload computations may be skipped. Control, matcher and
         * FF cones have completed above. Ordinary FIFO writes are accepted
         * against old occupancy, including writes on a reset edge. */
        fputs("if(0",f);
        for(uint32_t id=0;id<m->nx;++id){const rds_object *o=&m->objects[id];if(p->group[id]!=g||o->kind!=1)continue;char count[64];rds_c_object_view(count,sizeof count,m,id,false);
            fprintf(f,"||(cv%u[0]&&%s<%u)",o->inputs[1],count,o->depth);}
        fputs("){\n",f);
        for(uint32_t i=0;i<m->no;++i){const rds_op *o=&m->ops[i];if(needed[o->out]&&!p->offer[o->out]&&!control[o->out])rds_c_operation(f,m,o);}
        fputs("}\n",f);
    }
}
void rds_c_offers(FILE *f,const rds_sim *s){
    rds_offer_plan *p=s->schedule->offers;
    if(p){fprintf(f,"/* Immutable offers: %u groups, %u values, %u bytes per bank. */\n#ifdef NDEBUG\n",p->groups,p->count,p->words*8);
        for(uint32_t g=0;g<p->groups;++g){
            fprintf(f,"static inline int offer_publish_%u(rds_compiled_context*c,unsigned bank){object_state*restrict h=c->hot;const uint64_t*restrict q=c->q;uint64_t*restrict v=c->v;(void)h;(void)q;(void)v;(void)bank;\n",g);
            emit_values(f,s,p,g,true);
            for(uint32_t v=0;v<p->model.nv;++v)if(p->offer[v]&&p->dependency[v]==g)fprintf(f,"memcpy(h->bulk_offers[bank]+%u,cv%u,%u);\n",p->offset[v],v,p->model.values[v].words*8);
            fprintf(f,"h->offer_same_%u=false;",g);
            fputs("return 0;}\n",f);
            fprintf(f,"static inline int offer_update_%u(rds_compiled_context*c,unsigned bank){object_state*restrict h=c->hot;const uint64_t*restrict q=c->q;uint64_t*restrict v=c->v,*restrict n=c->next;(void)h;(void)q;(void)v;(void)n;(void)bank;\n",g);
            emit_values(f,s,p,g,false);
            rds_c_transition_objects(f,&p->model,g,p->pending,p->prepare);
            rds_c_registers(f,&p->model,g,p->staged);
            fprintf(f,"return offer_publish_%u(c,bank^1);}\n",g);
        }
        for(uint32_t phase=0;phase<2;++phase)for(uint32_t lane=0;lane<s->schedule->count;++lane){
            fprintf(f,"static int offer_%s_%u(rds_compiled_context*c){unsigned bank=c->cycle&1;(void)bank;",phase?"step":"init",lane);
            for(uint32_t g=0;g<p->groups;++g)if(p->owner[g]==lane)fprintf(f,"if(offer_%s_%u(c,bank))return -1;",phase?"update":"publish",g);
            fputs("return 0;}\n",f);
        }
        fputs("static int offer_finish(rds_compiled_context*c){object_state*h=c->hot;(void)h;",f);
        if(s->schedule->cache_objects)for(uint32_t lane=0;lane<s->schedule->count;++lane)fprintf(f,"memset(h->payload_dirty_%u,1,%u);",lane,s->nx);
        if(s->schedule->cache_fields)fputs("h->fields_valid=false;",f);
        fputs("return 0;}\n#endif\n",f);
    }
    fputs("phase_fn rds_generated_offer_bind(uint32_t lane,uint32_t phase){(void)lane;(void)phase;\n#ifdef NDEBUG\n",f);
    if(p){fputs("if(phase==2)return offer_finish;switch(lane){",f);for(uint32_t lane=0;lane<s->schedule->count;++lane)fprintf(f,"case %u:return phase?offer_step_%u:offer_init_%u;",lane,lane,lane);fputs("}\n",f);}
    fputs("#endif\nreturn 0;}\n",f);
}
static uint64_t hash(uint64_t h,const void *data,size_t size){const unsigned char *p=data;while(size--)h=(h^*p++)*UINT64_C(1099511628211);return h;}
uint64_t rds_offer_key(const rds_sim *s,uint64_t h){const rds_offer_plan *p=s->schedule->offers;bool present=p!=NULL;h=hash(h,&present,sizeof present);if(!p)return h;
    const uint32_t version=5;h=hash(h,&version,sizeof version);
    const rds_sim *m=&p->model;h=hash(h,m->ops,m->no*sizeof *m->ops);h=hash(h,m->args,m->na*sizeof *m->args);h=hash(h,m->imm,m->ni*sizeof *m->imm);
    for(uint32_t v=0;v<m->nv;++v){h=hash(h,&m->values[v].width,sizeof m->values[v].width);h=hash(h,&m->values[v].state,sizeof m->values[v].state);}
    h=hash(h,p->group,m->nx*sizeof *p->group);h=hash(h,p->owner,p->groups*sizeof *p->owner);return h;
}
void rds_offer_report(FILE *f,const rds_sim *s){const rds_offer_plan *p=s->schedule->offers;fputs(",\"bulk_offers\":",f);if(!p){fputs("null",f);return;}
    fprintf(f,"{\"groups\":%u,\"values\":%u,\"bank_bytes\":%u,\"owners\":[",p->groups,p->count,p->words*8);
    for(uint32_t g=0;g<p->groups;++g)fprintf(f,"%s%u",g?",":"",p->owner[g]);fputs("],\"group_costs\":[",f);
    for(uint32_t g=0;g<p->groups;++g)fprintf(f,"%s%" PRIu64,g?",":"",p->cost[g]);fputs("]}",f);
}
