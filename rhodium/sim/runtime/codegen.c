/* Compiles fixed worker functions and attaches state-only native execution. */
#define _POSIX_C_SOURCE 200809L
#include "codegen.h"
#include "kernel-format.h"
#include <dlfcn.h>
#include <inttypes.h>
#include <sys/stat.h>

enum { BLOCK_OPS = 64, BLOCK_BINDINGS = 4096, SOURCE_BUDGET = 4 * 1024 * 1024 };
#define CACHE_GUARD UINT32_C(0x80000000)
typedef struct { uint32_t code, width; instruction ins; } item;
typedef struct { uint32_t shape, first, count, begin, end, lane, guard; char *annotated; } command;
typedef struct { char *body; } shape;
typedef struct {
    FILE *out;
    const rds_sim *sim;
    int32_t *locals;
    const bool *constant;
    const size_t *state;
    const size_t *destinations;
    const uint64_t *values;
    uint32_t bindings[BLOCK_BINDINGS], outputs[BLOCK_BINDINGS], count, temporary;
    bool overflow;
} emitter;

/* Analyze word versions in the existing schedule. Conditional execution never
 * moves a read across a scratch-slot overwrite or a worker boundary. */
typedef void (*word_visit)(uint32_t, void *);
static void value_words(const rds_sim *s, uint32_t id, word_visit visit, void *context) {
    if (id == RDS_NONE) return;
    for (uint32_t j=0;j<s->values[id].words;++j) visit((uint32_t)s->values[id].offset+j,context);
}
static void item_outputs(const rds_sim *s,const item *o,word_visit visit,void *context) {
    if(o->code==GENERIC)value_words(s,s->ops[o->ins.out].out,visit,context);
    else visit(o->ins.out,context);
}
static void item_inputs(const rds_sim *s,const item *o,word_visit visit,void *context) {
    if(o->code==GENERIC){const rds_op *p=&s->ops[o->ins.out];
        for(uint32_t j=0;j<p->nargs;++j)value_words(s,s->args[p->args+j],visit,context);
        return;}
    const instruction *i=&o->ins;visit(i->a,context);
    switch(o->code){
    case RDS_COPY:case RDS_NOT:case RDS_ZEXT:case RDS_SLICE:break;
    case SLICE_CROSS:visit(i->a+1,context);break;
    default:visit(i->b,context);break;
    }
    if(o->code==SELECT||o->code==RDS_SET_CLEAR||o->code==RDS_BALANCE||o->code==RDS_COUNTER_STEP)visit(i->c,context);
}
typedef struct { uint32_t *last,*uses,*demand; uint32_t index,guard; } demand_context;
static uint32_t join_demand(uint32_t a,uint32_t b) {
    return a==RDS_NONE?b:b==RDS_NONE?a:a==b?a:0;
}
static void define_word(uint32_t word,void *context){demand_context *d=context;d->last[word]=d->index;}
static void use_word(uint32_t word,void *context){demand_context *d=context;if(d->last[word]!=RDS_NONE)++d->uses[d->last[word]];}
static void need_word(uint32_t word,void *context){demand_context *d=context;d->demand[word]=join_demand(d->demand[word],d->guard);}
static void take_word(uint32_t word,void *context){demand_context *d=context;d->guard=join_demand(d->guard,d->demand[word]);d->demand[word]=RDS_NONE;}
static void sink_value(const rds_sim *s,uint32_t id,uint32_t guard,demand_context *d){
    d->guard=guard;value_words(s,id,need_word,d);value_words(s,id,use_word,d);
}
static bool supported(const rds_sim *s, const item *i);
static int demand_plan(const rds_sim *s,const item *items,uint32_t n,uint32_t *guards,size_t *destinations,uint32_t *pending_owner,bool *staged,bool *pending){
    size_t words=s->value_words?s->value_words:1;
    uint32_t *last=malloc(words*sizeof *last),*demand=malloc(words*sizeof *demand);
    uint32_t *uses=calloc(n?n:1,sizeof *uses),*lanes=calloc(n?n:1,sizeof *lanes);
    if(!last||!demand||!uses||!lanes){free(last);free(demand);free(uses);free(lanes);return -1;}
    memset(last,255,words*sizeof *last);memset(demand,255,words*sizeof *demand);
    demand_context d={last,uses,demand,0,0};
    for(uint32_t i=0;i<n;++i){d.index=i;item_inputs(s,&items[i],use_word,&d);item_outputs(s,&items[i],define_word,&d);}
    if(s->schedule->instruction_guards)for(uint32_t i=0;i<n;++i){
        uint32_t g=s->schedule->instruction_guards[i];
        uint32_t count;
        const uint32_t *a = rds_guard_atoms(s->schedule, &g, &count);
        for (uint32_t j = 0; j < count; ++j) use_word((a[j]-s->nx-s->nw-1)/2,&d);
    }
    for(uint32_t lane=0;lane<s->schedule->count;++lane){const worker *w=&s->schedule->workers[lane];
        for(uint32_t j=0;j<w->count;++j){const batch *b=&s->schedule->batches[w->first+j];
            for(uint32_t i=b->first;i<b->first+b->count;++i)lanes[i]=lane;}}
    for(uint32_t i=0;i<s->np;++i)sink_value(s,s->ports[i].value,0,&d);
    for(uint32_t i=0;i<s->nr;++i){const rds_reg *r=&s->regs[i];sink_value(s,r->d,0,&d);sink_value(s,r->reset,0,&d);sink_value(s,r->reset_value,0,&d);}
    for(uint32_t i=0;i<s->nw;++i){const rds_write *r=&s->writes[i];
        sink_value(s,r->data,s->eager_combinational?0:s->nx+1+i,&d);
        sink_value(s,r->address,0,&d);sink_value(s,r->enable,0,&d);sink_value(s,r->mask,0,&d);}
    for(uint32_t i=0;i<s->ns;++i){const rds_read *r=&s->reads[i];sink_value(s,r->address,0,&d);sink_value(s,r->enable,0,&d);sink_value(s,r->write,0,&d);}
    for(uint32_t i=0;i<s->nc;++i){const rds_assert *r=&s->checks[i];sink_value(s,r->condition,0,&d);sink_value(s,r->guard,0,&d);sink_value(s,r->reset,0,&d);}
    for(uint32_t i=0;i<s->nx;++i){const rds_object *o=&s->objects[i];
        for(uint32_t j=0;j<o->ni;++j)sink_value(s,o->inputs[j],!s->eager_combinational&&((o->kind==1&&!(o->flags&5))||o->kind==2||o->kind==3||o->kind==9)&&j==2?i+1:0,&d);}
    /* A terminal computed value with one register consumer writes
     * next-state at its original evaluation point. Reset is applied in staging. */
    if(!s->materialize_state)for(uint32_t r=0;r<s->nr;++r){const rds_value *v=&s->values[s->regs[r].d];uint32_t p=last[v->offset];
        if(p==RDS_NONE||uses[p]!=v->words || (s->schedule->tail_count && p>=s->schedule->tail_first))continue;
        if(s->schedule->instruction_cache&&s->schedule->instruction_cache[p]!=RDS_NONE)continue;
        bool eligible=supported(s,&items[p]);
        if(items[p].code==GENERIC){const rds_op *op=&s->ops[items[p].ins.out];
            eligible=eligible||(op->code==RDS_WRITE_SET&&s->imm[op->imm+2]==1);}
        if(!eligible)continue;
        bool match=true;for(uint32_t j=0;j<v->words;++j)if(last[v->offset+j]!=p)match=false;
        if(match){staged[r]=true;for(uint32_t j=0;j<v->words;++j)destinations[v->offset+j]=s->regs[r].next+j;}}
    /* A sole payload sink can consume its producer directly in pending storage.
     * Queries observe old data, so speculative pending writes preserve snapshots. */
    for(uint32_t id=0;id<s->nx;++id){const rds_object *o=&s->objects[id];
        if(!o->pending||!((o->kind>=1&&o->kind<=3)||o->kind==9)||o->inputs[2]==RDS_NONE)continue;
        const rds_value *v=&s->values[o->inputs[2]];uint32_t p=last[v->offset];
        if(p==RDS_NONE||uses[p]!=v->words||!supported(s,&items[p]) || (s->schedule->tail_count && p>=s->schedule->tail_first))continue;
        if(s->schedule->instruction_cache&&s->schedule->instruction_cache[p]!=RDS_NONE)continue;
        bool match=true;for(uint32_t j=0;j<v->words;++j)if(last[v->offset+j]!=p)match=false;
        if(match){pending[id]=true;for(uint32_t j=0;j<v->words;++j){destinations[v->offset+j]=j;pending_owner[v->offset+j]=id;}}
    }
    if(s->schedule->instruction_guards){
        memcpy(guards,s->schedule->instruction_guards,(size_t)n*sizeof *guards);
        free(last);free(demand);free(uses);free(lanes);return 0;
    }
    for(uint32_t i=n;i--;){
        d.guard=RDS_NONE;item_outputs(s,&items[i],take_word,&d);
        if(d.guard==RDS_NONE)d.guard=0;
        if(items[i].code==GENERIC){uint32_t code=s->ops[items[i].ins.out].code;
            if((code==RDS_ONEHOT&&!s->guarded_onehot)||code==RDS_ONEHOT_VIEW||code==RDS_INDEX||code==RDS_INJECT||code==RDS_WRITE_SET||code==RDS_READ)d.guard=0;}
        if(d.guard>s->nx+s->nw){uint32_t word=(d.guard-s->nx-s->nw-1)/2,p=last[word];
            if(p!=RDS_NONE&&(p>=i||lanes[p]!=lanes[i]))d.guard=0;
        } else if(d.guard){uint32_t input[2],count=1;
            if(d.guard<=s->nx){const rds_object *o=&s->objects[d.guard-1];input[0]=o->inputs[1];
                if(o->kind==1&&(o->flags&2))input[count++]=o->inputs[3];}
            else input[0]=s->writes[d.guard-s->nx-1].enable;
            for(uint32_t j=0;j<count;++j){uint32_t p=last[s->values[input[j]].offset];
                if(p!=RDS_NONE&&(p>=i||lanes[p]!=lanes[i]))d.guard=0;}}
        guards[i]=d.guard;
        if(s->select_regions && !s->eager_combinational && !d.guard && items[i].code==SELECT){
            const instruction *in=&items[i].ins;
            d.guard=0;need_word(in->a,&d);
            d.guard=s->nx+s->nw+1+2*in->a;need_word(in->b,&d);
            d.guard=s->nx+s->nw+2+2*in->a;need_word(in->c,&d);
        }else if(s->select_regions && !s->eager_combinational && !d.guard && items[i].code==GENERIC){
            const rds_op *op=&s->ops[items[i].ins.out];const uint32_t *a=s->args+op->args;
            if(op->code==RDS_MUX && op->nargs==3 && s->values[a[0]].width==1 && s->imm[op->imm]<=1){
                uint32_t word=(uint32_t)s->values[a[0]].offset;bool key=s->imm[op->imm]!=0;
                d.guard=0;value_words(s,a[0],need_word,&d);
                d.guard=s->nx+s->nw+1+2*word+(key?0:1);value_words(s,a[1],need_word,&d);
                d.guard=s->nx+s->nw+1+2*word+(key?1:0);value_words(s,a[2],need_word,&d);
            }else item_inputs(s,&items[i],need_word,&d);
        }else item_inputs(s,&items[i],need_word,&d);
    }
    free(last);free(demand);free(uses);free(lanes);return 0;
}
static void emit_atom(FILE *f, const rds_sim *s, uint32_t guard) {
    uint32_t word=(guard-s->nx-s->nw-1)/2;size_t state=SIZE_MAX;
    for(uint32_t i=0;i<s->nv;++i)if(s->values[i].offset<=word && word<s->values[i].offset+s->values[i].words && s->values[i].state!=SIZE_MAX){state=s->values[i].state+word-s->values[i].offset;break;}
    fputs((guard-s->nx-s->nw-1)%2?"!!":"!",f);
    if(state!=SIZE_MAX)fprintf(f,"q[%zu]",state);else fprintf(f,"%s[%u]",rds_c_small_word(s,word)?"((uint8_t*)v)":"v",rds_c_arena_word(s,word));
}
static void emit_guard(FILE *f,const rds_sim *s,uint32_t guard,uint32_t lane){
    bool cold=s->cold_regions && !(guard&CACHE_GUARD);
    if(cold && (guard&RDS_GUARD_OR)){
        const rds_guard_or *u=&s->schedule->guard_unions[guard&~RDS_GUARD_OR];
        cold=u->count!=0;
        for(uint32_t j=0;j<u->count;++j)cold&=(u->atoms[j]&1)!=0;
    }else if(cold && guard>s->nx+s->nw)cold=(guard-s->nx-s->nw-1)%2!=0;
    fputs(cold?"if(RDS_UNLIKELY(":"if(",f);
    if(guard&CACHE_GUARD){uint32_t d=guard&~CACHE_GUARD;
        if(s->schedule->flow_cache_objects && d==s->nx+s->nr){
            bool first=true;
            for(uint32_t at=0;at<s->nx;at+=8){
                uint64_t mask=0;unsigned bytes=s->nx-at<8?s->nx-at:8;
                for(unsigned j=0;j<bytes;++j)if(s->schedule->flow_cache_objects[at+j])mask|=UINT64_C(1)<<(8*j);
                if(mask){fprintf(f,"%s(cg_flow_dirty(h->payload_dirty_%u+%u,%u)&UINT64_C(0x%016" PRIx64 "))",first?"":"|",lane,at,bytes,mask);first=false;}
            }
        }else if(d<s->nx)fprintf(f,"h->payload_dirty_%u[%u]",lane,d);
        else fprintf(f,"field_dirty_%u",d-s->nx);
    }else if(guard & RDS_GUARD_OR){
        const rds_guard_or *u=&s->schedule->guard_unions[guard & ~RDS_GUARD_OR];
        fprintf(f,"%s",s->guarded_onehot?"c->strict||":"");
        if(!u->count)fputc('0',f);
        for(uint32_t j=0;j<u->count;++j){if(j)fputs("||",f);emit_atom(f,s,u->atoms[j]);}

    }else if(guard>s->nx+s->nw){
        uint32_t word=(guard-s->nx-s->nw-1)/2;size_t state=SIZE_MAX;
        for(uint32_t i=0;i<s->nv;++i)if(s->values[i].offset<=word && word<s->values[i].offset+s->values[i].words && s->values[i].state!=SIZE_MAX){state=s->values[i].state+word-s->values[i].offset;break;}
        fprintf(f,"%s%s",s->guarded_onehot?"c->strict||":"",(guard-s->nx-s->nw-1)%2?"":"!");
        if(state!=SIZE_MAX)fprintf(f,"q[%zu]",state);else fprintf(f,"%s[%u]",rds_c_small_word(s,word)?"((uint8_t*)v)":"v",rds_c_arena_word(s,word));

    }else if(guard<=s->nx){const rds_object *o=&s->objects[guard-1];
        fprintf(f,"%s(",s->guarded_onehot?"c->strict||":"");rds_c_pointer(f,rds_c_ref(s,o->inputs[1]));
        fputs(")[0]",f);
        if(o->kind==1){char count[64];rds_c_object_view(count,sizeof count,s,guard-1,false);fprintf(f,"&&(%s<%u",count,o->depth);
            if(o->flags&2){fputs("||(",f);rds_c_pointer(f,rds_c_ref(s,o->inputs[3]));fputs(")[0]",f);}fputc(')',f);}

    }else{fprintf(f,"%s(",s->guarded_onehot?"c->strict||":"");rds_c_pointer(f,rds_c_ref(s,s->writes[guard-s->nx-1].enable));fputs(")[0]",f);}
    fputs(cold?")){\n":"){\n",f);
}

/* Specialize operand offsets in C, preserving shared bodies as an explicit ablation. */
static void bound_body(FILE *out, const char *body, const uint32_t *bindings) {
    for (const char *p = body; *p;) {
        if (p[0] == 'b' && p[1] == '[') {
            char *end;
            unsigned long index = strtoul(p + 2, &end, 10);
            if (*end == ']') { fprintf(out, "%u", bindings[index]); p = end + 1; continue; }
        }
        fputc(*p++, out);
    }
}

static uint64_t hash_bytes(uint64_t h, const void *data, size_t size) {
    const unsigned char *p = data;
    for (size_t i = 0; i < size; ++i) h = (h ^ p[i]) * UINT64_C(1099511628211);
    return h;
}
static uint64_t key(const rds_sim *s) {
    const rds_schedule *p = s->schedule;
    if (p->plan_released) return p->compiled_key;
    uint64_t h = UINT64_C(14695981039346656037);
    /* Version covers emission semantics. The rest identifies immutable layout,
     * operations, parameters, and worker ownership, never the current inputs. */
    uint32_t version = s->lift_primitives ? (s->lift_transitions ? 101 : 100) : s->lift_transitions ? 87 : s->flow_prepare ? 86 : (p->flow_cache_objects ? 75 : 57);
    if(s->flow_prepare)version+=129; /* Deferred FIFO batches with a proved empty fast path. */
    if(s->lift_primitives&&p->count==1)version+=UINT32_C(0x80000); /* Publish pinned FIFO payloads directly. */
    if(s->lift_primitives&&p->count==1)version+=UINT32_C(0x100000); /* Omit unused FIFO pending scratch. */
    if(s->lift_primitives&&p->count==1)version+=UINT32_C(0x200000); /* Byte matcher priorities. */
    version+=UINT32_C(0x400000); /* Validated matcher priority shift range. */
    version+=UINT32_C(0x800009); /* Immutable offer cuts and one-barrier bulk entries. */
    version+=UINT32_C(0x1000000); /* Sv39 cache-management permissions. */
    if(s->inline_bodies==2)version+=UINT32_C(0x10000);
#define HASH(ptr, count) h = hash_bytes(h, ptr, (size_t)(count) * sizeof *(ptr))
    bool field_cache=p->cache_fields!=NULL;HASH(&field_cache,1);
    if(field_cache){HASH(p->cache_fields,s->nr);HASH(p->field_masks,s->next_words);}
    HASH(&p->tail_first,1);HASH(&p->tail_count,1);
    HASH(&p->guard_union_count,1); HASH(p->guard_unions,p->guard_union_count);
    HASH(&s->guarded_onehot,1); HASH(&s->cold_regions,1);
    HASH(&version, 1); HASH(&s->wide_regions,1); HASH(&s->select_regions,1); HASH(&s->eager_combinational,1); HASH(&s->materialize_state,1); HASH(&p->count, 1); HASH(&s->batches, 1);
    h=rds_offer_key(s,h);
    for(uint32_t i=0;i<s->nv;++i) {
        const rds_value *v=&s->values[i];
        HASH(&v->width,1); HASH(&v->words,1); HASH(&v->offset,1); HASH(&v->state,1); HASH(&v->origin,1);
    }
    HASH(s->ops, s->no); HASH(s->args, s->na); HASH(s->imm, s->ni);
    HASH(p->batches, s->batches);
    if(p->flow_cache_objects){HASH(p->flow_cache_objects,s->nx);HASH(&s->flow_prepare,1);}
    if (s->batches) {
        batch *b = &p->batches[s->batches - 1]; HASH(p->instructions, b->first + b->count);
        if(p->instruction_guards)HASH(p->instruction_guards,b->first+b->count);
        if(p->instruction_cache)HASH(p->instruction_cache,b->first+b->count);
    }
    for (uint32_t i = 0; i < p->count; ++i) {
        HASH(&p->workers[i].first, 1); HASH(&p->workers[i].count, 1);
    }
    for (uint32_t i = 0; i < s->nx; ++i) {
        const rds_object *o=&s->objects[i];
        HASH(&o->kind,1); HASH(&o->width,1); HASH(&o->depth,1); HASH(&o->flags,1); HASH(&o->owner,1);
        HASH(o->inputs,o->ni);
    }
    for (uint32_t i=0;i<s->nm;++i) { HASH(&s->mems[i].width,1); HASH(&s->mems[i].depth,1); }
    for (uint32_t i = 0; i < p->constant_count; ++i) {
        HASH(&p->constants[i].offset, 1); HASH(&p->constants[i].words, 1);
        HASH(s->arena + p->constants[i].offset, p->constants[i].words);
    }
    /* Sink liveness controls which C locals are stored at block boundaries. */
    HASH(s->regs, s->nr); HASH(s->writes, s->nw); HASH(s->reads, s->ns);
    for (uint32_t i = 0; i < s->np; ++i) HASH(&s->ports[i].value, 1);
    for (uint32_t i = 0; i < s->nc; ++i) {
        HASH(&s->checks[i].condition, 1); HASH(&s->checks[i].reset, 1); HASH(&s->checks[i].guard, 1);
    }
#undef HASH
    return h;
}
uint64_t rds_emission_key(const rds_sim *s) { return key(s); }
static uint32_t binding(emitter *e, uint32_t offset) {
    for (uint32_t i = 0; i < e->count; ++i) if (e->bindings[i] == offset) return i;
    if (e->count == BLOCK_BINDINGS) { e->overflow = true; return 0; }
    e->bindings[e->count] = offset; return e->count++;
}
static void read_word(emitter *e, uint32_t offset, char *text) {
    if (e->constant[offset]) snprintf(text, 48, "UINT64_C(0x%016" PRIx64 ")", e->values[offset]);
    else if (e->locals[offset] >= 0) snprintf(text, 48, "t%d", e->locals[offset]);
    else if (e->state[offset] != SIZE_MAX) snprintf(text, 48, "q[b[%u]]", binding(e, (uint32_t)e->state[offset]));
    else snprintf(text, 48, "%s[b[%u]]",rds_c_small_word(e->sim,offset)?"((uint8_t*)v)":"v", binding(e, rds_c_arena_word(e->sim,offset)));
}
static uint32_t start_word(emitter *e, uint32_t offset) {
    uint32_t t = e->temporary++;
    if (t < BLOCK_BINDINGS) e->outputs[t] = offset; else e->overflow = true;
    e->locals[offset] = (int32_t)t;
    fprintf(e->out, "uint64_t t%u = ", t); return t;
}
static void end_word(emitter *e, uint32_t t, uint32_t width) {
    fprintf(e->out, "; t%u &= UINT64_C(0x%016" PRIx64 "); (void)t%u;\n", t, rds_mask(width), t);
}
bool rds_c_memory_view(const rds_sim *s,const rds_op *o){
    if(!s->lift_primitives||s->schedule->count!=1||o->code!=RDS_READ)return false;
    uint32_t width=s->values[s->args[o->args]].width;
    return width<32&&(UINT64_C(1)<<width)<=s->mems[s->imm[o->imm]].depth;
}
static bool supported(const rds_sim *s, const item *i) {
    if (i->code != GENERIC) return true;
    const rds_op *o = &s->ops[i->ins.out];
    if (s->values[o->out].words > 64 || o->nargs > 128) return false;
    switch (o->code) {
    case RDS_READ: return rds_c_memory_view(s,o);
    case RDS_OBJECT_QUERY: return s->objects[s->imm[o->imm]].kind!=6 || s->objects[s->imm[o->imm]].depth<=64;
    case RDS_ALU: case RDS_BYTE_MERGE: case RDS_CONTRACT: return true;
    case RDS_DECODE: return rds_c_decode_table_id(s,o)!=RDS_NONE;
    case RDS_COPY: case RDS_PACK: case RDS_SLICE: case RDS_ZEXT: case RDS_SEXT: return true;
    case RDS_INDEX: {
        const uint32_t *a=s->args+o->args;
        const rds_value *base=&s->values[a[0]];
        uint32_t iw=s->values[a[1]].width,ew=s->values[o->out].width;
        bool fixed=base->state!=SIZE_MAX ||
            (s->emit_constants && s->emit_constants[base->offset]!=RDS_NONE);
        return (base->width<=64 || (fixed && ew<=64 && !(64%ew))) && iw<64 &&
            (UINT64_C(1)<<iw)<=s->imm[o->imm];
    }
    case RDS_SHL: case RDS_SHRU:
        return s->values[o->out].width>64 && s->values[o->out].width<=128 &&
            s->values[s->args[o->args+1]].width<=64;
    case RDS_MUX: return s->values[s->args[o->args]].width <= 64 && o->nargs <= 16;
    default: return false;
    }
}
static void scalar(emitter *e, const item *op) {
    const instruction *i = &op->ins;
    char a[48], b[48] = "0", c[48] = "0";
    read_word(e, i->a, a);
    switch (op->code) {
    case RDS_COPY: case RDS_NOT: case RDS_ZEXT: case RDS_SLICE: break;
    case SLICE_CROSS: read_word(e, i->a + 1, b); break;
    default: read_word(e, i->b, b); break;
    }
    if (op->code == SELECT || op->code == RDS_SET_CLEAR || op->code == RDS_BALANCE || op->code == RDS_COUNTER_STEP)
        read_word(e, i->c, c);
    uint32_t t = start_word(e, i->out);
    switch (op->code) {
    case RDS_COPY: case RDS_ZEXT: fprintf(e->out, "%s", a); break;
    case RDS_NOT: fprintf(e->out, "~%s", a); break;
    case RDS_AND: fprintf(e->out, "%s & %s", a, b); break;
    case RDS_OR: fprintf(e->out, "%s | %s", a, b); break;
    case RDS_XOR: fprintf(e->out, "%s ^ %s", a, b); break;
    case RDS_ADD: fprintf(e->out, "%s + %s", a, b); break;
    case RDS_SUB: fprintf(e->out, "%s - %s", a, b); break;
    case RDS_MUL: fprintf(e->out, "%s * %s", a, b); break;
    case RDS_SET_CLEAR: fprintf(e->out, "(%s | %s) & ~%s", a, b, c); break;
    case RDS_BALANCE: fprintf(e->out, "%s + %s - %s", a, b, c); break;
    case RDS_COUNTER_STEP: fprintf(e->out, "!%s ? %s : %s == %s ? 0 : %s + 1", b, a, a, c, a); break;
    case RDS_EQ: fprintf(e->out, "%s == %s", a, b); break;
    case RDS_ULT: if(e->constant[i->b]&&!e->values[i->b])fputs("0",e->out);else fprintf(e->out, "%s < %s", a, b); break;
    case RDS_SHL: fprintf(e->out, "%s < %u ? %s << (%s & 63) : 0", b, op->width, a, b); break;
    case RDS_SHRU: fprintf(e->out, "%s < %u ? %s >> (%s & 63) : 0", b, op->width, a, b); break;
    case RDS_SLICE: fprintf(e->out, "%s >> %u", a, i->c); break;
    case SLICE_CROSS: fprintf(e->out, "(%s >> %u) | (%s << %u)", a, i->c, b, 64 - i->c); break;
    case SELECT: fprintf(e->out, "%s ? %s : %s", a, c, b); break;
    }
    end_word(e, t, op->width);
}
/* Add a fixed bit interval to a local destination. Geometry is resolved here,
 * so generated execution contains no bit-copy loop, width lookup, or division. */
static void fixed_interval(emitter *e, uint32_t t, uint32_t out_bit, uint32_t source, uint32_t bit, uint32_t width) {
    char a[48], b[48]; read_word(e, source + bit / 64, a);
    fprintf(e->out, "t%u |= (((%s >> %u)", t, a, bit % 64);
    if (bit % 64 + width > 64) {
        read_word(e, source + bit / 64 + 1, b);
        fprintf(e->out, " | (%s << %u)", b, 64 - bit % 64);
    }
    fprintf(e->out, ") & UINT64_C(0x%016" PRIx64 ")) << %u;\n", rds_mask(width), out_bit);
}
/* Snapshot queries are ordinary expressions. Keep their words in local SSA
 * temporaries so adjacent projections and consumers do not force arena copies. */
static void query_words(emitter *e,const rds_sim *s,const rds_op *op){
    uint32_t id=(uint32_t)s->imm[op->imm],query=(uint32_t)s->imm[op->imm+1];
    const rds_object *o=&s->objects[id];const uint32_t *args=s->args+op->args;
    uint32_t token=e->temporary;
    char count[64],validity[64];rds_c_object_view(count,sizeof count,s,id,false);rds_c_object_view(validity,sizeof validity,s,id,true);
    if(o->kind==11){
        char a[48],b[48];read_word(e,(uint32_t)s->values[args[0]].offset,a);
        if(query==3||query==4){read_word(e,(uint32_t)s->values[args[0]].offset+1,b);fprintf(e->out,"rds_tlb_result lookup%u={%s,%s};\n",token,a,b);}
        else{read_word(e,(uint32_t)s->values[args[1]].offset,b);fprintf(e->out,"rds_tlb_result lookup%u=rds_tlb_lookup(h->o%u.data,%u,%s,%s!=0);\n",token,id,o->depth,a,b);}
    }else if(o->kind==6&&!(query==2&&(o->flags&1))){ /* A request bitset shares arbitration across result words. */
        fprintf(e->out,"uint64_t requests%u=0",token);
        for(uint32_t j=0;j<o->depth;++j){char a[48];read_word(e,(uint32_t)s->values[args[j]].offset,a);fprintf(e->out,"|((%s&UINT64_C(1))<<%u)",a,j);}
        if(o->depth==2&&!(o->flags&16)){
            fprintf(e->out,";bool first%u=(requests%u&1)!=0,second%u=(requests%u&2)!=0;",token,token,token,token);
            fprintf(e->out,"bool prefer_second%u=",token);
            if(o->flags&2)fprintf(e->out,"h->o%u.head==1",id);else fputs("false",e->out);
            fprintf(e->out,";uint32_t choice%u=second%u&&(!first%u||prefer_second%u);(void)choice%u;\n",token,token,token,token,token);
        }else{
        fprintf(e->out,";uint64_t eligible%u=requests%u;",token,token);
        if(o->flags&2)fprintf(e->out,"{uint64_t upper=h->o%u.head<64?requests%u&(UINT64_MAX<<h->o%u.head):0;if(upper)eligible%u=upper;}",id,token,id,token);
        fprintf(e->out,"uint32_t choice%u=eligible%u?(uint32_t)__builtin_ctzll(eligible%u):0;bool chosen%u=requests%u!=0;",token,token,token,token,token);
        if(o->flags&16)fprintf(e->out,"if(%s){choice%u=h->o%u.tail;chosen%u=true;}",count,token,id,token);
        fprintf(e->out,"(void)choice%u;(void)chosen%u;\n",token,token);
        }
    }else if(o->kind==10){
        fprintf(e->out,"uint64_t grant%u=0;{uint64_t taken=",token);
        if(query>=o->depth){char a[48];read_word(e,(uint32_t)s->values[args[0]].offset,a);fputs(a,e->out);}else fputs("0",e->out);
        fputs(";",e->out);
        uint32_t columns=query>=o->depth?1:query+1;
        for(uint32_t col=0;col<columns;++col){fputs("{uint64_t requests=0",e->out);
            if(o->flags&32){char a[48];read_word(e,(uint32_t)s->values[args[query>=o->depth?1:col]].offset,a);fprintf(e->out,"|%s",a);}
            else for(uint32_t row=0;row<o->width;++row){char a[48];read_word(e,(uint32_t)s->values[args[query>=o->depth?1+row:col*o->width+row]].offset,a);fprintf(e->out,"|((%s&UINT64_C(1))<<%u)",a,row);}
            fprintf(e->out,";requests&=~taken;uint64_t priority=h->o%u.data[%u];uint64_t upper=requests&(UINT64_MAX<<(priority&63));uint64_t eligible=upper?upper:requests;grant%u=eligible&(~eligible+1);taken|=grant%u;}",id,query>=o->depth?query-o->depth:col,token,token);}
        fputs("}\n",e->out);
    }
    for(uint32_t word=0;word<s->values[op->out].words;++word){
        char input[48]="0";if(op->nargs)read_word(e,(uint32_t)s->values[args[0]].offset+(query==3&&o->kind==1?word:0),input);
        uint32_t t=start_word(e,(uint32_t)s->values[op->out].offset+word);
        switch(o->kind){
        case 1: /* FIFO: bypass dependencies remain exactly those of this query. */
            if(query==0)fprintf(e->out,"%s",count);
            else if(query==1){fprintf(e->out,"%s<%u",count,o->depth);if(o->flags&2)fprintf(e->out,"||(%s!=0)",input);}
            else if(query==2){fprintf(e->out,"%s!=0",count);if(o->flags&4)fprintf(e->out,"||(%s!=0)",input);}
            else if(o->flags&1)fputs("0",e->out);
            else {if(o->flags&4)fprintf(e->out,"!%s?%s:",count,input);
                if(o->depth==1)fprintf(e->out,"h->o%u.data[%u]",id,word);
                else fprintf(e->out,"h->o%u.data[(size_t)h->o%u.head*%u+%u]",id,id,o->words,word);}
            break;
        case 2: /* Pipeline readiness is the downstream readiness or an empty stage. */
            if(query==1){fprintf(e->out,"(%s!=0)",input);for(uint32_t j=0;j<o->depth;++j)fprintf(e->out,"||!%s[%u]",validity,j);}
            else if(query==2)fprintf(e->out,"%s[%u]",validity,o->depth-1);
            else if(o->flags&1)fputs("0",e->out);
            else fprintf(e->out,"h->o%u.data[%u]",id,(o->depth-1)*o->words+word);
            break;
        case 3:
            if(query==2)fprintf(e->out,"%s",count);
            else if(o->flags&1)fputs("0",e->out);
            else fprintf(e->out,"h->o%u.data[%u]",id,word);
            break;
        case 4:fprintf(e->out,"h->o%u.data[%u]",id,word);break;
        case 5:
            fprintf(e->out,"%s",count);
            if(query)fprintf(e->out,"==%u",query==1?0:o->depth);
            break;
        case 6:
            if(query==0)fprintf(e->out,"choice%u",token);
            else if(query==1){
                if(o->depth==2&&!(o->flags&16))fprintf(e->out,"requests%u!=0",token);
                else fprintf(e->out,"chosen%u&&((requests%u>>choice%u)&1)",token,token,token);
            }
            else if(query==2){
                if(o->flags&1)fputs("0",e->out);
                else {for(uint32_t j=1;j<o->depth;++j){read_word(e,(uint32_t)s->values[args[o->depth+j]].offset+word,input);fprintf(e->out,"choice%u==%u?%s:",token,j,input);}
                    read_word(e,(uint32_t)s->values[args[o->depth]].offset+word,input);fputs(input,e->out);}
            }else {read_word(e,(uint32_t)s->values[args[o->depth]].offset,input);
                if(o->depth==2&&!(o->flags&16)){
                    /* Ready depends on the winning request, without a generic
                     * priority search. Packet ownership is deliberately excluded. */
                    if(query==3)fprintf(e->out,"first%u&&(!second%u||!prefer_second%u)&&(%s!=0)",token,token,token,input);
                    else fprintf(e->out,"second%u&&(!first%u||prefer_second%u)&&(%s!=0)",token,token,token,input);
                }else fprintf(e->out,"chosen%u&&choice%u==%u&&(%s!=0)",token,token,query-3,input);}
            break;
        case 7:
            fprintf(e->out,"%s",count);
            if(query)fprintf(e->out,"==%u&&(%s!=0)",o->depth-1,input);
            break;
        case 8:fprintf(e->out,"h->o%u.data[%u]",id,query);break;
        case 9:
            if(query==0){fputs("1",e->out);for(uint32_t j=0;j<o->depth;++j){read_word(e,(uint32_t)s->values[args[j]].offset,input);fprintf(e->out,"&&(!%s[%u]||(%s!=0))",validity,j,input);}}
            else if(query==1){if(o->flags&1)fputs("0",e->out);else fprintf(e->out,"h->o%u.data[%u]",id,word);}
            else fprintf(e->out,"%s[%u]",validity,query-2);
            break;
        case 10:fprintf(e->out,"grant%u",token);break;
        case 11:
            if(!query||query==5)fprintf(e->out,"lookup%u.%s",token,word?"metadata":"low");
            else{
                bool probe=query==2||query==4;unsigned base=query>=3?1:2;
                fprintf(e->out,"rds_tlb_fault(lookup%u,",token);
                if(probe)fputs("0",e->out);else{read_word(e,(uint32_t)s->values[args[base]].offset,input);fputs(input,e->out);}
                for(unsigned j=0;j<3;++j){read_word(e,(uint32_t)s->values[args[base+!probe+j]].offset,input);fprintf(e->out,",%s%s",input,j?"!=0":"");}
                fprintf(e->out,",%s)",probe?"true":"false");
            }
            break;
        }
        uint32_t bits=s->values[op->out].width-word*64;
        end_word(e,t,bits<64?bits:64);
    }
}
static void aggregate(emitter *e, const rds_sim *s, const rds_op *o) {
    if(o->code==RDS_OBJECT_QUERY){query_words(e,s,o);return;}
    if(rds_c_memory_view(s,o)){
        char index[48];read_word(e,(uint32_t)s->values[s->args[o->args]].offset,index);
        uint32_t token=e->temporary,words=s->values[o->out].words;
        // Preserve the IR's index range in C, including direct register inputs.
        // This redundant narrowing lets Clang bound pointer aliasing.
        fprintf(e->out,"const uint64_t*restrict memory_view%u=h->memory_%u+(size_t)(%s&UINT64_C(0x%016" PRIx64 "))*%u;\n",token,(uint32_t)s->imm[o->imm],index,rds_mask(s->values[s->args[o->args]].width),words);
        for(uint32_t j=0;j<words;++j){uint32_t t=start_word(e,(uint32_t)s->values[o->out].offset+j);
            fprintf(e->out,"memory_view%u[%u]",token,j);end_word(e,t,j+1==words?s->values[o->out].width:64);}
        return;
    }
    const uint32_t *args = s->args + o->args;
    uint32_t width = s->values[o->out].width, out = (uint32_t)s->values[o->out].offset;
    if(o->code==RDS_CONTRACT){
        struct rds_kernel_format k;
        if(!rds_kernel_parse(s->imm+o->imm,o->nimm,&k)){e->overflow=true;return;}
        uint32_t token=e->temporary;
        fprintf(e->out,"uint64_t contract_result%u[%u];{uint64_t contract_values[%u];\n",token,s->values[o->out].words,k.words);
        for(uint32_t i=0;i<k.inputs;++i)for(uint32_t word=0;word<s->values[args[i]].words;++word){
            char source[48];read_word(e,(uint32_t)s->values[args[i]].offset+word,source);
            fprintf(e->out,"contract_values[%u]=%s;\n",k.offsets[i]+word,source);
        }
        rds_c_kernel_local(e->out,s,o);
        fprintf(e->out,"memcpy(contract_result%u,contract_values+%u,%u);}\n",token,k.offsets[k.values-1],s->values[o->out].words*8);
        for(uint32_t word=0;word<s->values[o->out].words;++word){
            uint32_t t=start_word(e,out+word);fprintf(e->out,"contract_result%u[%u]",token,word);
            end_word(e,t,word+1==s->values[o->out].words?width-word*64:64);
        }
        return;
    }
    if(o->code==RDS_MUX && width>64) {
        /* Select one contiguous storage span, rather than making a separate
         * address choice per word that the host compiler may turn into a
         * gather. Never force a current-region temporary back into storage. */
        bool stored=true;
        for(uint32_t a=1;a<o->nargs;++a) {
            const rds_value *v=&s->values[args[a]];
            for(uint32_t w=0;w<v->words;++w)
                if(e->locals[v->offset+w]>=0)stored=false;
        }
        if(stored) {
            char selector[48];read_word(e,(uint32_t)s->values[args[0]].offset,selector);
            uint32_t token=e->temporary;
            fprintf(e->out,"const uint64_t *mux%u=",token);
            for(uint32_t a=2;a<o->nargs;++a) {
                fprintf(e->out,"%s==UINT64_C(0x%016" PRIx64 ")?",selector,s->imm[o->imm+a-2]);
                rds_c_pointer(e->out,rds_c_ref(s,args[a]));fputc(':',e->out);
            }
            rds_c_pointer(e->out,rds_c_ref(s,args[1]));fputs(";\n",e->out);
            for(uint32_t w=0;w<s->values[o->out].words;++w) {
                uint32_t t=start_word(e,out+w),bits=width-w*64;
                fprintf(e->out,"mux%u[%u]",token,w);
                end_word(e,t,bits<64?bits:64);
            }
            return;
        }
    }
    if(o->code==RDS_SHL || o->code==RDS_SHRU) {
        /* A two-word fixed-width shift is one host extended-integer expression,
         * not a partial-bit copy loop with intermediate arena storage. */
        char lo[48],hi[48],shift[48];
        read_word(e,(uint32_t)s->values[args[0]].offset,lo);
        read_word(e,(uint32_t)s->values[args[0]].offset+1,hi);
        read_word(e,(uint32_t)s->values[args[1]].offset,shift);
        uint32_t token=e->temporary;
        fprintf(e->out,"__uint128_t shifted%u=%s<%u?(((__uint128_t)%s<<64)|%s)%s(%s&127):0;\n",
                token,shift,width,hi,lo,o->code==RDS_SHL?"<<":">>",shift);
        for(uint32_t w=0;w<2;++w) {
            uint32_t t=start_word(e,out+w);
            fprintf(e->out,"(uint64_t)(shifted%u>>%u)",token,w*64);
            end_word(e,t,w?width-64:64);
        }
        return;
    }
    if(o->code==RDS_INDEX) {
        /* Verified geometry and the index type prove this lookup total. Keep
         * the packed source and selected field in the surrounding C region. */
        char base[48],index[48];
        read_word(e,(uint32_t)s->values[args[1]].offset,index);
        if(s->values[args[0]].width>64) {
            uint32_t t=start_word(e,out);
            fputc('(',e->out);rds_c_pointer(e->out,rds_c_ref(s,args[0]));
            fprintf(e->out,")[((%s)*%u)/64] >> (((%s)*%u)%%64)",index,width,index,width);
            end_word(e,t,width);return;
        }
        read_word(e,(uint32_t)s->values[args[0]].offset,base);
        uint32_t t=start_word(e,out);
        fprintf(e->out,"%s >> (%s * %u)",base,index,width);
        end_word(e,t,width);return;
    }
    if(o->code==RDS_DECODE) {
        char selector[48];read_word(e,(uint32_t)s->values[args[0]].offset,selector);
        uint32_t t=start_word(e,out);
        uint64_t membership_bits;
        if(rds_c_decode_bits(s,o,&membership_bits))fprintf(e->out,"(UINT64_C(0x%" PRIx64 ") >> (%s&63))&1",membership_bits,selector);
        else fprintf(e->out,"decode_%u[%s&%u]",rds_c_decode_table_id(s,o),selector,(1u<<s->values[args[0]].width)-1);
        end_word(e,t,width);return;
    }
    if(o->code==RDS_ALU || o->code==RDS_BYTE_MERGE) {
        char a[48],b[48],c[48];
        read_word(e,(uint32_t)s->values[args[0]].offset,a);
        read_word(e,(uint32_t)s->values[args[1]].offset,b);
        read_word(e,(uint32_t)s->values[args[2]].offset,c);
        uint32_t t=start_word(e,out);
        if(o->code==RDS_ALU)fprintf(e->out,"rds_alu(%u,%s,%s,%s)",width,a,b,c);
        else fprintf(e->out,"cg_byte_merge(%s,%s,%s)",a,b,c);
        end_word(e,t,width);return;
    }
    for (uint32_t word = 0; word < s->values[o->out].words; ++word) {
        uint32_t low = word * 64, bits = width - low < 64 ? width - low : 64;
        if (o->code == RDS_MUX) {
            char selector[48], value[48]; read_word(e, (uint32_t)s->values[args[0]].offset, selector);
            uint32_t t = start_word(e, out + word);
            for (uint32_t k = 2; k < o->nargs; ++k) {
                read_word(e, (uint32_t)s->values[args[k]].offset + word, value);
                fprintf(e->out, "%s == UINT64_C(0x%016" PRIx64 ") ? %s : ", selector, s->imm[o->imm + k - 2], value);
            }
            read_word(e, (uint32_t)s->values[args[1]].offset + word, value); fprintf(e->out, "%s", value);
            end_word(e, t, bits); continue;
        }
        uint32_t t = start_word(e, out + word); fprintf(e->out, "0;\n");
        if (o->code == RDS_PACK) {
            uint32_t position = 0;
            for (uint32_t k = 0; k < o->nargs; ++k) {
                uint32_t end = position + s->values[args[k]].width;
                uint32_t begin = position > low ? position : low;
                uint32_t stop = end < low + bits ? end : low + bits;
                if (stop > begin) fixed_interval(e, t, begin - low, (uint32_t)s->values[args[k]].offset, begin - position, stop - begin);
                position = end;
            }
        } else {
            uint32_t start = o->code == RDS_SLICE ? (uint32_t)s->imm[o->imm] : 0;
            uint32_t aw = s->values[args[0]].width;
            if (start + low < aw) {
                uint32_t take = aw - start - low < bits ? aw - start - low : bits;
                fixed_interval(e, t, 0, (uint32_t)s->values[args[0]].offset, start + low, take);
            }
            if (o->code == RDS_SEXT && low + bits > aw) {
                char sign[48]; read_word(e, (uint32_t)s->values[args[0]].offset + (aw - 1) / 64, sign);
                uint32_t shift = aw > low ? aw - low : 0;
                fprintf(e->out, "t%u |= (UINT64_C(0) - ((%s >> %u) & 1)) & (UINT64_MAX << %u);\n", t, sign, (aw - 1) % 64, shift);
            }
        }
        fprintf(e->out, "t%u &= UINT64_C(0x%016" PRIx64 "); (void)t%u;\n", t, rds_mask(bits), t);
    }
}
static void mark_value(const rds_sim *s, bool *live, uint32_t id, bool value) {
    if (id == RDS_NONE) return;
    const rds_value *v = &s->values[id];
    for (uint32_t i = 0; i < v->words; ++i) live[v->offset + i] = value;
}
static void reverse_item(const rds_sim *s, const item *o, bool *live) {
    if (o->code == GENERIC) {
        const rds_op *op = &s->ops[o->ins.out]; mark_value(s, live, op->out, false);
        uint32_t nargs=rds_c_onehot_storage(s,op)?1:op->nargs;
        for (uint32_t k = 0; k < nargs; ++k) mark_value(s, live, s->args[op->args + k], true);
        return;
    }
    const instruction *i = &o->ins; live[i->out] = false; live[i->a] = true;
    switch (o->code) {
    case RDS_COPY: case RDS_NOT: case RDS_ZEXT: case RDS_SLICE: break;
    case SLICE_CROSS: live[i->a + 1] = true; break;
    default: live[i->b] = true; break;
    }
    if (o->code == SELECT || o->code == RDS_SET_CLEAR || o->code == RDS_BALANCE || o->code == RDS_COUNTER_STEP) live[i->c] = true;
}
static void store_destination(FILE *f,size_t word,uint32_t object,uint32_t temporary){
    if(object!=RDS_NONE)fprintf(f,"h->o%u.pending[%zu]=t%u;\n",object,word,temporary);
    else fprintf(f,"%s[%zu]=t%u;\n","n",word,temporary);
}
/* Provenance belongs to a command, not to shape identity or its source budget.
 * Distinct instruction decompositions can emit identical executable C. */
static char *shape_without_provenance(const char *body) {
    size_t bytes=strlen(body);char *result=malloc(bytes+1);if(!result)return NULL;
    char *out=result;
    for(const char *p=body;*p;) {
        const char *end=strchr(p,'\n');size_t count=end?(size_t)(end-p)+1:strlen(p);
        if(strncmp(p,"/* rds-complete ",16)&&strncmp(p,"/* rds-store ",13)) {
            memcpy(out,p,count);out+=count;
        }
        p+=count;
    }
    *out=0;return result;
}
static int emit_c(rds_sim *s, const char *path, uint32_t max_shapes) {
    rds_schedule *p = s->schedule;
    if (!p || !path || s->value_words > UINT32_MAX) return rds_fail(s, "C emission requires a scheduled word-addressable model");
    if (p->plan_released) return rds_fail(s, "emit C before attaching a compiled model");
    uint32_t n = s->batches ? p->batches[s->batches - 1].first + p->batches[s->batches - 1].count : 0;
    /* Fusion demand follows model size, including when one worker executes
     * the whole design. Bound both shape count and body bytes together. */
    uint32_t budget_units = n / 8192 + (n % 8192 != 0);
    if (!budget_units) budget_units = 1;
    if (budget_units > 8) budget_units = 8;
    if (!max_shapes) max_shapes = 512 * budget_units;
    size_t source_budget = (size_t)SOURCE_BUDGET * budget_units;
    if (max_shapes > 4096) return rds_fail(s, "compiled shape limit exceeds 4096");
    item *items = calloc(n ? n : 1, sizeof *items);
    command *commands = calloc(n ? n : 1, sizeof *commands);
    shape *shapes = calloc(max_shapes, sizeof *shapes);
    bool *live = calloc(s->value_words ? s->value_words : 1, sizeof *live);
    bool *constant = calloc(s->value_words ? s->value_words : 1, sizeof *constant);
    size_t *state = malloc((s->value_words ? s->value_words : 1) * sizeof *state);
    size_t *destinations = malloc((s->value_words ? s->value_words : 1) * sizeof *destinations);
    bool *staged = calloc(s->nr?s->nr:1,sizeof *staged);
    bool *pending = calloc(s->nx?s->nx:1,sizeof *pending);
    uint32_t *pending_owner=malloc((s->value_words?s->value_words:1)*sizeof *pending_owner);
    uint32_t *guards = calloc(n?n:1,sizeof *guards);
    int32_t *locals = malloc((s->value_words ? s->value_words : 1) * sizeof *locals);
    uint32_t *last_definition = malloc((s->value_words ? s->value_words : 1) * sizeof *last_definition);
    uint32_t *prepare_at = malloc((s->nx?s->nx:1)*sizeof *prepare_at);
    uint32_t *prepare_group = calloc(s->nx?s->nx:1,sizeof *prepare_group);
    uint32_t *bindings = NULL, binding_count = 0, command_count = 0, shape_count = 0;
    size_t source_size = 0; int result = -1; FILE *out = NULL;
    if (!items || !commands || !shapes || !live || !constant || !state || !locals || !last_definition || !prepare_at || !prepare_group || !destinations || !staged || !pending || !pending_owner || !guards) goto done;
    for (size_t i=0;i<s->value_words;++i){state[i]=destinations[i]=SIZE_MAX;pending_owner[i]=RDS_NONE;}
    for (uint32_t i=0;i<s->nv;++i) if(s->values[i].state!=SIZE_MAX)
        for(uint32_t j=0;j<s->values[i].words;++j) state[s->values[i].offset+j]=s->values[i].state+j;
    for (uint32_t i = 0; i < p->constant_count; ++i)
        for (uint32_t j = 0; j < p->constants[i].words; ++j) constant[p->constants[i].offset + j] = true;
    for (uint32_t k = 0; k < s->batches; ++k) for (uint32_t j = p->batches[k].first; j < p->batches[k].first + p->batches[k].count; ++j)
        items[j] = (item){p->batches[k].code, p->batches[k].width, p->instructions[j]};
    if(demand_plan(s,items,n,guards,destinations,pending_owner,staged,pending))goto done;
    if(p->instruction_cache)for(uint32_t i=0;i<n;++i)
        if(p->instruction_cache[i]!=RDS_NONE)guards[i]=CACHE_GUARD|p->instruction_cache[i];
    /* A total SRAM read immediately consumed by an unconditional mux can join
     * that region. Local SSA then removes the read's intermediate array; the
     * host compiler sees the selection and loads together. Partial reads keep
     * their original guarded/error behavior. */
    for(uint32_t i=0;i+1<n;++i)if(items[i].code==GENERIC&&items[i+1].code==GENERIC&&guards[i+1]==0){
        const rds_op *read=&s->ops[items[i].ins.out],*mux=&s->ops[items[i+1].ins.out];
        if(!rds_c_memory_view(s,read)||mux->code!=RDS_MUX)continue;
        for(uint32_t a=1;a<mux->nargs;++a)if(s->args[mux->args+a]==read->out)guards[i]=0;
    }
    for (uint32_t lane = 0; lane < p->count; ++lane) {
        worker *w = &p->workers[lane]; if (!w->count) continue;
        batch *last = &p->batches[w->first + w->count - 1]; uint32_t end = last->first + last->count;
        for (uint32_t i = p->batches[w->first].first; i < end;) {
            uint32_t begin = i++; bool native = supported(s, &items[begin]);
            while (i < end && i - begin < (s->wide_regions ? 1024 : BLOCK_OPS) && supported(s, &items[i]) == native && guards[i]==guards[begin]) ++i;
            commands[command_count++] = (command){0, begin, i - begin, begin, i, lane, guards[begin],NULL};
        }
    }
    /* Infallible transitions only write private prepared fields. Move them
     * after their last owner-local input producer; current queries and later
     * validation still observe the original snapshot. */
    memset(last_definition,255,s->value_words*sizeof *last_definition);
    demand_context producers={.last=last_definition};
    for(uint32_t ci=0;ci<command_count;++ci){producers.index=ci;
        for(uint32_t i=commands[ci].begin;i<commands[ci].end;++i)item_outputs(s,&items[i],define_word,&producers);}
    for(uint32_t id=0;id<s->nx;++id){
        const rds_object *o=&s->objects[id];prepare_at[id]=RDS_NONE;
        if(p->count!=1||!p->prepare_local||!(o->kind==1||o->kind==2||o->kind==3||o->kind==6||o->kind==7||o->kind==9||(s->lift_transitions&&o->kind==11)))continue;
        uint32_t after=RDS_NONE;bool local=true;
        for(uint32_t a=0;a<o->ni;++a){if(o->inputs[a]==RDS_NONE)continue;const rds_value *v=&s->values[o->inputs[a]];
            for(uint32_t w=0;w<v->words;++w){uint32_t ci=last_definition[v->offset+w];if(ci==RDS_NONE)continue;
                if(commands[ci].lane!=o->owner)local=false;
                if(after==RDS_NONE||ci>after)after=ci;}}
        if(after==RDS_NONE)for(uint32_t ci=0;ci<command_count;++ci)if(commands[ci].lane==o->owner){after=ci;break;}
        if(local)prepare_at[id]=after;
    }
    for(uint32_t first=0;first<s->nx;){
        if(prepare_at[first]==RDS_NONE||!rds_c_fifo_batchable(s,first)){++first;continue;}
        uint32_t count=1;
        while(count<16&&first+count<s->nx&&prepare_at[first+count]!=RDS_NONE&&
              rds_c_fifo_batchable(s,first+count)&&s->objects[first+count].inputs[0]==s->objects[first].inputs[0])++count;
        if(count<4){first+=count;continue;}
        uint32_t group=count>=16?16:count>=8?8:4,after=prepare_at[first];
        for(uint32_t i=1;i<group;++i)if(prepare_at[first+i]>after)after=prepare_at[first+i];
        for(uint32_t i=0;i<group;++i){prepare_at[first+i]=after;prepare_group[first+i]=i?RDS_NONE:group;}
        first+=group;
    }
    for (uint32_t i = 0; i < s->np; ++i) mark_value(s, live, s->ports[i].value, true);
    for (uint32_t i = 0; i < s->nr; ++i) {
        mark_value(s, live, s->regs[i].d, true); mark_value(s, live, s->regs[i].reset, true); mark_value(s, live, s->regs[i].reset_value, true);
    }
    for (uint32_t i = 0; i < s->nw; ++i) {
        mark_value(s, live, s->writes[i].address, true); mark_value(s, live, s->writes[i].data, true);
        mark_value(s, live, s->writes[i].enable, true); mark_value(s, live, s->writes[i].mask, true);
    }
    for (uint32_t i = 0; i < s->ns; ++i) {
        mark_value(s, live, s->reads[i].address, true); mark_value(s, live, s->reads[i].enable, true); mark_value(s, live, s->reads[i].write, true);
    }
    for (uint32_t i = 0; i < s->nc; ++i) {
        mark_value(s, live, s->checks[i].condition, true); mark_value(s, live, s->checks[i].guard, true); mark_value(s, live, s->checks[i].reset, true);
    }
    for (uint32_t i = 0; i < s->nx; ++i) for (uint32_t j = 0; j < s->objects[i].ni; ++j) mark_value(s, live, s->objects[i].inputs[j], true);
    /* Deferred consumers are outside the worker command list. Their live-ins
     * must still force producer stores before the evaluation barrier. */
    for(uint32_t i=n;i-- > p->tail_first;)reverse_item(s,&items[i],live);
    for (uint32_t ci = command_count; ci--;) {
        command *c = &commands[ci];
        if (supported(s, &items[c->begin])) {
            char *body = NULL; size_t bytes = 0;
            FILE *body_file = open_memstream(&body, &bytes); if (!body_file) goto done;
            memset(locals, 255, s->value_words * sizeof *locals);
            demand_context definitions={.last=last_definition};
            for(uint32_t i=c->begin;i<c->end;++i){
                definitions.index=i;item_outputs(s,&items[i],define_word,&definitions);
            }
            emitter e = {.sim=s,.out = body_file, .locals = locals, .constant = constant, .state = state, .destinations = destinations, .values = s->arena};
            for (uint32_t i = c->begin; i < c->end; ++i) {
                uint32_t first = e.temporary;
                if (items[i].code == GENERIC) aggregate(&e, s, &s->ops[items[i].ins.out]); else scalar(&e, &items[i]);
                /* Markers remain command-local and are stripped from shape
                 * identity; absolute source IDs belong to its manifest. */
                fprintf(body_file,"/* rds-complete %u %u %u */\n",i-c->begin,first,e.temporary-first);
                /* Sole-consumer destinations are disjoint from current reads.
                 * Scratch live-outs use only their final definition, after all
                 * producer inputs have been read. Both can be stored here
                 * without extending their live ranges to the region boundary. */
                for (uint32_t t=first;t<e.temporary&&t<BLOCK_BINDINGS;++t) {
                    uint32_t offset=e.outputs[t];
                    if(destinations[offset]!=SIZE_MAX) {
                        fprintf(body_file,"/* rds-store %u */\n",t);
                        store_destination(body_file,destinations[offset],pending_owner[offset],t);
                    } else if(live[offset]&&last_definition[offset]==i) {
                        fprintf(body_file,"/* rds-store %u */\n",t);
                        fprintf(body_file,"%s[b[%u]] = t%u;\n",rds_c_small_word(s,offset)?"((uint8_t*)v)":"v",binding(&e,rds_c_arena_word(s,offset)),t);
                    }
                }
            }
            if (fclose(body_file)) { free(body); goto done; }
            char *annotated=body;body=shape_without_provenance(annotated);
            if(!body){free(annotated);goto done;}
            bytes=strlen(body);
            uint32_t id = 0;
            while (id < shape_count && strcmp(shapes[id].body, body)) ++id;
            if (!e.overflow && (id < shape_count || (shape_count < max_shapes && source_size + bytes <= source_budget))) {
                if (e.count > UINT32_MAX - binding_count) { free(body);free(annotated);goto done; }
                if (id == shape_count) { shapes[shape_count++].body = body; source_size += bytes; body = NULL; }
                uint32_t *more = realloc(bindings, ((size_t)binding_count + e.count + 1) * sizeof *bindings);
                if (!more) { free(body);free(annotated);goto done; } bindings = more;
                memcpy(bindings + binding_count, e.bindings, (size_t)e.count * sizeof *bindings);
                c->shape = id + 1; c->first = binding_count; c->count = e.count; binding_count += e.count;
                c->annotated=annotated;annotated=NULL;
            }
            free(body);free(annotated);
        }
        for (uint32_t i = c->end; i-- > c->begin;) reverse_item(s, &items[i], live);
        if(!(c->guard&CACHE_GUARD)&&c->guard>s->nx+s->nw){
            uint32_t count;
            const uint32_t *a=rds_guard_atoms(s->schedule,&c->guard,&count);
            for(uint32_t j=0;j<count;++j)live[(a[j]-s->nx-s->nw-1)/2]=true;
        }
    }
    out = fopen(path, "w"); if (!out) goto done;
    rds_c_prelude(out,s);
    fprintf(out,"/* rds-fusion-budget {\"instructions\":%u,\"shape_limit\":%u,\"source_limit\":%zu,\"shapes\":%u,\"source_bytes\":%zu} */\n",
            n,max_shapes,source_budget,shape_count,source_size);
    rds_c_object_layout(out,s,pending);
    fprintf(out,"/* rds-provenance {\"version\":1,\"source_key\":\"%016" PRIx64 "\",\"compiled_key\":\"%016" PRIx64 "\",\"workers\":%u,\"shared\":%s,\"sources\":[",s->source_key,key(s),p->count,s->shared_code?"true":"false");
    bool comma=false;
    for(uint32_t id=0;id<s->nx;++id)if(s->objects[id].kind==1&&s->objects[id].depth==1&&s->objects[id].flags==0) {
        char view[64];rds_c_object_view(view,sizeof view,s,id,false);
        fprintf(out,"%s[%u,\"%s\"]",comma?",":"",id,view);comma=true;
    }
    fputs("],\"query_views\":[",out);comma=false;
    for(uint32_t id=0;id<s->nx;++id)if(s->objects[id].kind==2) {
        char view[64];rds_c_object_view(view,sizeof view,s,id,true);
        fprintf(out,"%s[%u,2,\"%s[%u]\"]",comma?",":"",id,view,s->objects[id].depth-1);comma=true;
    }
    fputs("],\"state_views\":[",out);
    for(uint32_t r=0;r<s->nr;++r) {
        const rds_value *v=&s->values[s->regs[r].q];
        fprintf(out,"%s[%u,%u,%zu]",r?",":"",v->origin,v->width,v->state);
    }
    fputs("]} */\n",out);
    uint32_t arena_words=0;
    for(uint32_t i=0;i<s->value_words;++i)if(!s->emit_offsets)++arena_words;
    else if(s->emit_offsets[i]!=RDS_NONE){
        uint32_t end=rds_c_small_word(s,i)?s->emit_offsets[i]/8+1:s->emit_offsets[i]+1;
        if(end>arena_words)arena_words=end;
    }
    fprintf(out,"size_t rds_generated_arena_words(void){return %u;}\n",arena_words);
    uint64_t layout_key=UINT64_C(14695981039346656037);
    if(s->emit_offsets)layout_key=hash_bytes(layout_key,s->emit_offsets,s->value_words*sizeof *s->emit_offsets);
    if(s->emit_small)layout_key=hash_bytes(layout_key,s->emit_small,s->value_words*sizeof *s->emit_small);
    fprintf(out,"uint64_t rds_generated_layout_key(void){return UINT64_C(0x%016" PRIx64 ");}\n",layout_key);
    fputs("uint32_t rds_generated_arena_offset(uint32_t x){",out);
    if(!s->emit_offsets)fputs("return x;",out);
    else {uint32_t count=0;fputs("static const uint32_t spans[][3]={",out);
        for(uint32_t i=0;i<s->value_words;){if(s->emit_offsets[i]==RDS_NONE){++i;continue;}
            uint32_t begin=i++;while(i<s->value_words&&s->emit_offsets[i]!=RDS_NONE&&s->emit_offsets[i]==s->emit_offsets[begin]+i-begin)++i;
            fprintf(out,"{%u,%u,%u},",begin,i,s->emit_offsets[begin]);++count;}
        fprintf(out,"{0,0,0}};for(uint32_t i=0;i<%u;++i)if(x>=spans[i][0]&&x<spans[i][1])return x-spans[i][0]+spans[i][2];return UINT32_MAX;",count);}
    fputs("}\n",out);
    fputs("uint32_t rds_generated_arena_small(uint32_t x){(void)x;return 0",out);
    uint32_t packed_scalars=0;
    for(uint32_t i=0;i<s->value_words;){
        if(!rds_c_small_word(s,i)||s->emit_offsets[i]==RDS_NONE){++i;continue;}
        uint32_t begin=i++;
        while(i<s->value_words&&rds_c_small_word(s,i)&&s->emit_offsets[i]!=RDS_NONE)++i;
        packed_scalars+=i-begin;fprintf(out,"||(x>=%u&&x<%u)",begin,i);
    }
    fprintf(out,";}\n/* byte-stored scalar words: %u */\n",packed_scalars);

    uint32_t gated=0,direct_regs=0,direct_updates=0;
    for(uint32_t i=0;i<n;++i)gated+=guards[i]!=0;
    for(uint32_t i=0;i<s->nr;++i)if(staged[i]) {
        bool update=false;
        for(uint32_t j=0;j<s->no;++j)if(s->ops[j].out==s->regs[i].d)update=s->ops[j].code==RDS_WRITE_SET;
        if(update)++direct_updates;else ++direct_regs;
    }
    fprintf(out,"/* demand-gated operations: %u; direct register packs: %u */\n",gated,direct_regs);
    fprintf(out,"/* direct register updates: %u */\n",direct_updates);
    uint32_t direct_payloads=0;for(uint32_t i=0;i<s->nx;++i)direct_payloads+=pending[i];
    fprintf(out,"/* direct object payloads: %u */\n",direct_payloads);
    rds_c_decode_tables(out,s);
    rds_c_bound_effects(out,s);
    fputs("#if defined(__GNUC__) || defined(__clang__)\n"
          "#pragma GCC diagnostic pop\n#endif\n",out);
    fprintf(out, "#if defined(__GNUC__) || defined(__clang__)\n#define RDS_NOINLINE %s\n#else\n#define RDS_NOINLINE\n#endif\n",s->inline_bodies==2?"inline __attribute__((always_inline))":s->inline_bodies==1?"inline":"__attribute__((noinline))");
    fputs("#if defined(__GNUC__) || defined(__clang__)\n#define RDS_UNLIKELY(x) __builtin_expect(!!(x),0)\n#else\n#define RDS_UNLIKELY(x) (x)\n#endif\n",out);
    fputs("#if defined(__GNUC__) || defined(__clang__)\n#define RDS_INLINE_SELECTION inline __attribute__((always_inline))\n#else\n#define RDS_INLINE_SELECTION inline\n#endif\n",out);
    if(p->flow_cache_objects)fputs("#if defined(__GNUC__) || defined(__clang__)\n#define RDS_CACHE_NOINLINE __attribute__((noinline))\n#else\n#define RDS_CACHE_NOINLINE\n#endif\n",out);
    for (uint32_t i = 0; s->shared_code && i < shape_count; ++i)
        fprintf(out, "static RDS_NOINLINE void block_%u(uint64_t *restrict v, const uint64_t *restrict q, uint64_t *restrict n, object_state *restrict h, const uint32_t *restrict b) {\n(void)v; (void)q; (void)n; (void)h; (void)b;\n%s}\n", i, shapes[i].body);
    if (!s->shared_code) for (uint32_t ci = 0; ci < command_count; ++ci) {
        command *cmd = &commands[ci];
        if (!cmd->shape) continue;
        fprintf(out,"/* rds-region {\"id\":%u,\"begin\":%u,\"end\":%u,\"guard\":%u,\"operations\":[",ci,cmd->begin,cmd->end,cmd->guard);
        for(uint32_t i=cmd->begin;i<cmd->end;++i)
            fprintf(out,"%s[%u,%u,%u]",i==cmd->begin?"":",",p->instruction_origins[i],items[i].code==GENERIC,
                    items[i].code==GENERIC?s->values[s->ops[items[i].ins.out].out].width:items[i].width);
        fputs("]} */\n",out);
        fprintf(out,"/* command %u lane %u instructions %u..%u */\n",ci,cmd->lane,cmd->begin,cmd->end);
        bool outline_cache=s->inline_bodies==1&&p->flow_cache_objects&&cmd->guard==(CACHE_GUARD|(s->nx+s->nr));
        fprintf(out, "static %s void bound_%u(uint64_t *restrict v, const uint64_t *restrict q,uint64_t *restrict n,object_state *restrict h){\n(void)v;(void)q;(void)n;(void)h;\n",outline_cache?"RDS_CACHE_NOINLINE":"RDS_NOINLINE",ci);
        bound_body(out, cmd->annotated, bindings + cmd->first);
        fputs("/* rds-region-end */\n",out);
        /* The pure manifest ends before effects. Offline specialization may
         * replace its stores, but must execute the transition tail on both
         * paths. Guarded/cached producers cannot own unconditional updates. */
        if(s->lift_transitions&&!cmd->guard){
            fputs("/* rds-transition-tail */\n",out);
            for(uint32_t id=0;id<s->nx;++id)if(prepare_at[id]==ci){
                if(prepare_group[id]!=RDS_NONE){
                    if(prepare_group[id])rds_c_prepare_fifo_batch(out,s,id,prepare_group[id],pending);
                    else rds_c_prepare_object(out,s,id,pending);
                }
            }
        }
        fputs("}\n", out);
    }
    fprintf(out, "static const uint32_t bindings[] = {");
    for (uint32_t i = 0; i < binding_count; ++i) fprintf(out, "%u,", bindings[i]);
    fprintf(out, "0};\nuint64_t rds_generated_key(void) {return UINT64_C(0x%016" PRIx64 ");}\n", key(s));
    fprintf(out, "uint32_t rds_generated_blocks(void) {return %u;}\nuint32_t rds_generated_shapes(void) {return %u;}\n", command_count, shape_count);
    for(uint32_t ci=0;ci<command_count;++ci) if(!commands[ci].shape) {
        command *cmd=&commands[ci];
        fprintf(out,"/* command %u lane %u instructions %u..%u */\n",ci,cmd->lane,cmd->begin,cmd->end);
        bool inline_selection=p->count==1&&s->inline_bodies==1&&cmd->end==cmd->begin+1&&
            items[cmd->begin].code==GENERIC&&
            (rds_c_onehot_storage(s,&s->ops[items[cmd->begin].ins.out])||
             s->ops[items[cmd->begin].ins.out].code==RDS_ONEHOT_VIEW);
        fprintf(out,"static %s int direct_%u(rds_compiled_context*c){uint64_t*restrict v=c->v,*restrict n=c->next;const uint64_t*restrict q=c->q;object_state*restrict h=c->hot;(void)v;(void)q;(void)n;(void)h;\n",
                inline_selection?"RDS_INLINE_SELECTION":"RDS_NOINLINE",ci);
        for(uint32_t j=cmd->begin;j<cmd->end;++j) {
            if(items[j].code==GENERIC) {
                const rds_op *o=&s->ops[items[j].ins.out];size_t dst=destinations[s->values[o->out].offset];
                if(dst!=SIZE_MAX && o->code==RDS_WRITE_SET) {
                    rds_c_update_next(out,s,o,dst);
                }else if(dst!=SIZE_MAX){
                    char *body=NULL;size_t bytes=0;FILE *mem=open_memstream(&body,&bytes);if(!mem)goto done;
                    memset(locals,255,s->value_words*sizeof *locals);
                    emitter e={.sim=s,.out=mem,.locals=locals,.constant=constant,.state=state,.destinations=destinations,.values=s->arena};
                    aggregate(&e,s,o);
                    for(uint32_t t=0;t<e.temporary;++t)store_destination(mem,destinations[e.outputs[t]],pending_owner[e.outputs[t]],t);
                    if(fclose(mem)){free(body);goto done;}
                    fputs("{",out);bound_body(out,body,e.bindings);fputs("}\n",out);free(body);
                }else rds_c_operation(out,s,o);
            }
            else {
                char *body=NULL;size_t bytes=0;FILE *mem=open_memstream(&body,&bytes);if(!mem)goto done;
                memset(locals,255,s->value_words*sizeof *locals);
                emitter e={.sim=s,.out=mem,.locals=locals,.constant=constant,.state=state,.destinations=destinations,.values=s->arena};
                scalar(&e,&items[j]);
                for(uint32_t t=0;t<e.temporary;++t){size_t dst=destinations[e.outputs[t]];
                    if(dst!=SIZE_MAX)store_destination(mem,dst,pending_owner[e.outputs[t]],t);
                    else fprintf(mem,"%s[b[%u]]=t%u;\n",rds_c_small_word(s,e.outputs[t])?"((uint8_t*)v)":"v",binding(&e,rds_c_arena_word(s,e.outputs[t])),t);}
                if(fclose(mem)){free(body);goto done;}
                fputs("{static const uint32_t b[]={",out);for(uint32_t k=0;k<e.count;++k)fprintf(out,"%u,",e.bindings[k]);
                fprintf(out,"0};%s}\n",body);free(body);
            }
        }
        fputs("return 0;}\n",out);
    }
    for(uint32_t phase=0;phase<7;++phase) for(uint32_t lane=0;lane<(phase<3?p->count:1);++lane){
        fprintf(out,"static %sint phase_%u_%u(rds_compiled_context*c){uint64_t*restrict v=c->v,*restrict n=c->next;const uint64_t*restrict q=c->q;object_state*restrict h=c->hot;(void)v;(void)q;(void)n;(void)h;(void)bindings;\n",s->inline_bodies==2?"RDS_NOINLINE ":"",phase,lane);
        if(phase==0){
            if(p->cache_fields)for(uint32_t r=0;r<s->nr;++r)if(p->cache_fields[r]){
                fprintf(out,"bool field_dirty_%u=!h->fields_valid||(",r);
                uint32_t nkey=0;
                for(uint32_t j=0;j<s->values[s->regs[r].q].words;++j){
                    uint64_t mask=p->field_masks[s->regs[r].next+j];if(!mask)continue;
                    fprintf(out,"%s((q[%zu]^h->field_key_%u[%u])&UINT64_C(0x%016" PRIx64 "))",nkey?"|":"",s->values[s->regs[r].q].state+j,r,nkey,mask);++nkey;
                }
                fputs(");\n",out);
            }
            uint32_t open_guard=0;for(uint32_t ci=0;ci<command_count;++ci){command *cmd=&commands[ci];if(cmd->lane!=lane)continue;
            if(cmd->guard!=open_guard){if(open_guard)fputs("}\n",out);if(cmd->guard)emit_guard(out,s,cmd->guard,lane);open_guard=cmd->guard;}
            if(cmd->shape) {
                if(s->shared_code)fprintf(out,"block_%u(v,q,n,h,bindings+%u);\n",cmd->shape-1,cmd->first);
                else fprintf(out,"bound_%u(v,q,n,h);\n",ci);
            }
            else fprintf(out,"if(direct_%u(c))return -1;\n",ci);
            if(s->lift_transitions&&!s->shared_code&&cmd->shape&&!cmd->guard)continue;
            for(uint32_t id=0;id<s->nx;++id)if(prepare_at[id]==ci){
                if(prepare_group[id]==RDS_NONE)continue;
                if(open_guard){fputs("}\n",out);open_guard=0;}
                if(prepare_group[id])rds_c_prepare_fifo_batch(out,s,id,prepare_group[id],pending);
                else rds_c_prepare_object(out,s,id,pending);
            }
        }if(open_guard)fputs("}\n",out);if(p->cache_objects)fprintf(out,"memset(h->payload_dirty_%u,0,%u);\n",lane,s->nx);rds_c_registers(out,s,lane,staged);
            if(p->cache_fields){
                for(uint32_t r=0;r<s->nr;++r)if(p->cache_fields[r]){
                    fprintf(out,"if(field_dirty_%u){",r);uint32_t nkey=0;
                    for(uint32_t j=0;j<s->values[s->regs[r].q].words;++j)if(p->field_masks[s->regs[r].next+j])
                        fprintf(out,"h->field_key_%u[%u]=q[%zu];",r,nkey++,s->values[s->regs[r].q].state+j);
                    fputs("}\n",out);
                }
                fputs("h->fields_valid=true;\n",out);
            }
        }else if(phase<3)rds_c_objects(out,s,lane,phase,pending,prepare_at);
        else if(phase==6){
            for(uint32_t i=0;i<p->tail_count;++i)
                rds_c_operation(out,s,&s->ops[p->instructions[p->tail_first+i].out]);
        }else rds_c_effects(out,s,phase,staged);
        fputs("return 0;}\n",out);
    }
    /* Ordinary FIFOs and both matcher encodings prepare from their own state
     * and pinned values. After eval, no other owner reads their live storage.
     * Debug preparations retain checks; other semantic kinds keep the barrier. */
    bool bulk_fused=!p->tail_count&&!s->nc&&!s->nw&&!s->ns;
    for(uint32_t i=0;i<s->nx;++i){const rds_object*o=&s->objects[i];
        if(!((o->kind==1&&!o->flags)||o->kind==10))bulk_fused=false;
    }
    fprintf(out,"unsigned rds_generated_bulk_fused(void){\n#ifdef NDEBUG\nreturn %u;\n#else\nreturn 0;\n#endif\n}\n",bulk_fused?1:0);
    fputs("typedef int(*phase_fn)(rds_compiled_context*);\n",out);
    rds_c_offers(out,s);
    if(bulk_fused&&p->count>1){
        fputs("#ifdef NDEBUG\n",out);
        for(uint32_t lane=0;lane<p->count;++lane){
            fprintf(out,"static int transition_%u(rds_compiled_context*c){uint64_t*restrict v=c->v;const uint64_t*restrict q=c->q;object_state*restrict h=c->hot;(void)v;(void)q;(void)h;\n",lane);
            rds_c_transition_objects(out,s,lane,pending,prepare_at);
            fputs("return 0;}\n",out);
        }
        fputs("#endif\n",out);
    }
    fputs("phase_fn rds_generated_transition_bind(uint32_t lane){(void)lane;\n#ifdef NDEBUG\n",out);
    if(bulk_fused&&p->count>1){
        fputs("switch(lane){",out);
        for(uint32_t lane=0;lane<p->count;++lane)fprintf(out,"case %u:return transition_%u;",lane,lane);
        fputs("}\n",out);
    }
    fputs("#endif\nreturn 0;}\nphase_fn rds_generated_bind(uint32_t lane,uint32_t phase){switch(phase){",out);
    for(uint32_t phase=0;phase<7;++phase){fprintf(out,"case %u:",phase);
        if(phase<3){fputs("switch(lane){",out);for(uint32_t lane=0;lane<p->count;++lane)fprintf(out,"case %u:return phase_%u_%u;",lane,phase,lane);fputs("}return 0;",out);}
        else fprintf(out,"return phase_%u_0;",phase);
    }
    fputs("}return 0;}\n",out);
    if (ferror(out)) goto done;
    result = 0;
done:
    if (out && fclose(out)) result = -1;
    for (uint32_t i = 0; i < shape_count; ++i) free(shapes[i].body);
    for (uint32_t i = 0; i < command_count; ++i) free(commands[i].annotated);
    free(items); free(commands); free(shapes); free(live); free(constant); free(state); free(locals); free(last_definition); free(prepare_at); free(prepare_group); free(bindings); free(destinations); free(staged); free(pending); free(pending_owner); free(guards);
    return result ? rds_fail(s, "cannot emit bounded C blocks") : 0;
}
int rds_emit_c(rds_sim *s,const char *path,uint32_t max_shapes){
    if(!s->schedule||s->schedule->plan_released||s->value_words>UINT32_MAX)
        return rds_fail(s,"emit C before attaching a scheduled model");
    if ((uint64_t)s->nx + s->nw + 3 + 2 * (uint64_t)s->value_words >= RDS_GUARD_OR)
        return rds_fail(s,"compiled arena exceeds static guard encoding");
    size_t words=s->value_words?s->value_words:1;
    bool *used=calloc(words,sizeof *used),*small=calloc(words,sizeof *small);
    uint32_t *offsets=malloc(words*sizeof *offsets);
    if(!used||!small||!offsets){free(used);free(small);free(offsets);return rds_fail(s,"cannot allocate compiled arena map");}
    uint32_t *constants=malloc(words*sizeof *constants);
    if(!constants){free(used);free(small);free(offsets);return rds_fail(s,"cannot allocate constant map");}
    /* Account for every physical slot version, including scalar instructions
     * whose original value descriptors were released when the plan was built.
     * Whole values, API bindings and indexed pointer tables stay word-addressed.
     * Parallel workers retain separate word regions to avoid byte false sharing. */
    if(s->schedule->count==1){
        for(uint32_t i=0;i<s->value_words;++i)offsets[i]=0;
        for(uint32_t i=0;i<s->nv;++i){const rds_value *v=&s->values[i];
            if(v->state!=SIZE_MAX)continue;
            for(uint32_t w=0;w<v->words;++w){uint32_t width=v->words>1?64:v->width;
                if(width>offsets[v->offset+w])offsets[v->offset+w]=width;}}
        for(uint32_t i=0;i<s->batches;++i){const batch *b=&s->schedule->batches[i];
            if(b->code==GENERIC)continue;
            for(uint32_t j=0;j<b->count;++j){uint32_t word=s->schedule->instructions[b->first+j].out;
                if(b->width>offsets[word])offsets[word]=b->width;}}
        for(uint32_t i=0;i<s->value_words;++i)small[i]=offsets[i]>0&&offsets[i]<=8;
        for(uint32_t i=0;i<s->no;++i){const rds_op *o=&s->ops[i];
            if(o->code==RDS_MUX||o->code==RDS_ONEHOT)
                for(uint32_t j=1;j<o->nargs;++j)small[s->values[s->args[o->args+j]].offset]=false;}
    }
    for(uint32_t i=0;i<s->np;++i){const rds_value *v=&s->values[s->ports[i].value];
        for(uint32_t j=0;j<v->words;++j)small[v->offset+j]=false;}
    for(uint32_t i=0;i<s->value_words;++i)constants[i]=RDS_NONE;
    uint32_t count=0;
    for(uint32_t i=0;i<s->schedule->constant_count;++i){const constant_span *v=&s->schedule->constants[i];
        for(uint32_t j=0;j<v->words;++j)constants[v->offset+j]=count++;}
    s->emit_constants=constants;
    uint32_t destination=RDS_NONE;
    s->emit_small=small;s->emit_destination=&destination;
    s->emit_used=used;
    for(uint32_t i=0;i<s->np;++i){const rds_value *v=&s->values[s->ports[i].value];
        if(v->state==SIZE_MAX)for(uint32_t j=0;j<v->words;++j)(void)rds_c_arena_word(s,(uint32_t)v->offset+j);}
    int status=emit_c(s,path,max_shapes);
    /* Dense bindings can change shape sharing and exhaust the body budget at
     * a different command. A newly selected generic fallback can materialize
     * additional words. Grow the footprint until emission uses only mapped
     * storage, including raw-pointer constraints discovered by those fallbacks. */
    bool *mapped_small=malloc(words*sizeof *mapped_small);
    if(!mapped_small)status=rds_fail(s,"cannot allocate emission footprint");
    while(!status){uint32_t count=0,bytes=0;
        for(uint32_t i=0;i<s->value_words;++i)if(used[i]){if(small[i])++bytes;else ++count;}
        if((uint64_t)count*8+bytes>UINT32_MAX)memset(small,0,words*sizeof *small);
        count=0;for(uint32_t i=0;i<s->value_words;++i)offsets[i]=used[i]&&!small[i]?count++:RDS_NONE;
        bytes=count*8;
        for(uint32_t i=0;i<s->value_words;++i)if(used[i]&&small[i])offsets[i]=bytes++;
        memcpy(mapped_small,small,words*sizeof *small);
        s->emit_offsets=offsets;status=emit_c(s,path,max_shapes);s->emit_offsets=NULL;
        bool changed=false;
        for(uint32_t i=0;i<s->value_words;++i)if((used[i]&&offsets[i]==RDS_NONE)||mapped_small[i]!=small[i])changed=true;
        if(!changed)break;
    }
    free(mapped_small);s->emit_used=NULL;
    s->emit_constants=NULL;s->emit_small=NULL;s->emit_destination=NULL;
    free(constants);free(used);free(small);free(offsets);
    if(!status) {
        FILE *source=fopen(path,"rb");uint64_t hash=UINT64_C(14695981039346656037);unsigned char block[8192];size_t count;
        if(!source)return rds_fail(s,"cannot read emitted provenance");
        while((count=fread(block,1,sizeof block,source)))hash=hash_bytes(hash,block,count);
        bool failed=ferror(source)!=0;if(fclose(source))failed=true;
        if(failed)return rds_fail(s,"cannot hash emitted provenance");
        source=fopen(path,"a");if(!source)return rds_fail(s,"cannot append emitted provenance");
        fprintf(source,"/* rds-file-fnv64 %016" PRIx64 " */\n",hash);
        if(fclose(source))return rds_fail(s,"cannot finish emitted provenance");
    }
    return status;
}
void rds_compiled_free(rds_sim *s) {
    if (s->schedule->compiled_handle) dlclose(s->schedule->compiled_handle);
    free(s->schedule->compiled_hot);s->schedule->compiled_hot=NULL;
    s->schedule->compiled_objects=NULL;s->schedule->compiled_hot_bytes=0;
    free(s->schedule->compiled_memories); s->schedule->compiled_memories = NULL;
    free(s->schedule->compiled_entries); s->schedule->compiled_entries = NULL;
    s->schedule->compiled_handle = NULL;
    s->schedule->compiled_bulk_fused=false;
    s->schedule->offers_ready=false;
}
int rds_use_compiled(rds_sim *s, const char *path) {
    if (!s->schedule || !path) return rds_fail(s, "compiled code requires a scheduled model and library path");
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) return rds_fail(s, dlerror());
    uint64_t (*get_key)(void) = NULL,(*get_layout_key)(void)=NULL;
    uint32_t (*get_blocks)(void) = NULL, (*get_shapes)(void) = NULL, (*get_bulk_fused)(void)=NULL;
    rds_phase_fn (*bind)(uint32_t,uint32_t) = NULL;
    rds_phase_fn (*transition_bind)(uint32_t) = NULL;
    rds_phase_fn (*offer_bind)(uint32_t,uint32_t) = NULL;
    size_t (*object_bytes)(void)=NULL;
    size_t (*arena_words)(void)=NULL;
    uint32_t (*arena_offset)(uint32_t)=NULL,(*arena_small)(uint32_t)=NULL;
    void (*objects)(rds_compiled_context*,bool)=NULL;
#define SYMBOL(variable, name) do { void *symbol = dlsym(handle, name); memcpy(&(variable), &symbol, sizeof(variable)); } while (0)
    SYMBOL(get_layout_key,"rds_generated_layout_key");
    SYMBOL(get_key, "rds_generated_key"); SYMBOL(get_blocks, "rds_generated_blocks");
    SYMBOL(get_shapes, "rds_generated_shapes");
    SYMBOL(get_bulk_fused,"rds_generated_bulk_fused");
    SYMBOL(bind, "rds_generated_bind");
    SYMBOL(transition_bind,"rds_generated_transition_bind");
    SYMBOL(offer_bind,"rds_generated_offer_bind");
    SYMBOL(arena_words,"rds_generated_arena_words");SYMBOL(arena_offset,"rds_generated_arena_offset");
    SYMBOL(arena_small,"rds_generated_arena_small");
    SYMBOL(object_bytes,"rds_generated_object_bytes");SYMBOL(objects,"rds_generated_objects");
#undef SYMBOL
    if (!get_key || !get_blocks || !get_shapes || !bind || !object_bytes || !objects || !arena_words || !arena_offset || !arena_small || !get_layout_key || get_key() != key(s)) {
        dlclose(handle); return rds_fail(s, "compiled library does not match the execution plan");
    }
    if(s->schedule->plan_released&&get_layout_key()!=s->schedule->compiled_layout_key){
        dlclose(handle);return rds_fail(s,"compiled library has a different arena layout");}
    uint64_t compiled_key=key(s);
    size_t hot_bytes=object_bytes();
    void *hot=aligned_alloc(64,(hot_bytes+63)&~(size_t)63);
    if(!hot){dlclose(handle);return rds_fail(s,"cannot allocate compiled objects");}
    memset(hot,0,hot_bytes);
    uint64_t **memories=calloc(s->nm?s->nm:1,sizeof *memories);
    rds_phase_fn *entries=calloc(s->schedule->count*6+5,sizeof *entries);
    uint64_t *current=s->current;
    if(!current)current=rds_state_bank(s->next_words);
    if(!memories||!entries||!current){free(hot);free(entries);free(memories);if(!s->current)free(current);dlclose(handle);return rds_fail(s,"cannot allocate compiled state");}
    for(uint32_t i=0;i<s->schedule->count*3+4;++i){
        entries[i]=i<s->schedule->count*3?bind(i/3,i%3):bind(0,i-s->schedule->count*3+3);
        if(!entries[i]){free(hot);free(entries);free(memories);if(!s->current)free(current);dlclose(handle);return rds_fail(s,"compiled entry is missing");}
    }
    if(transition_bind)for(uint32_t i=0;i<s->schedule->count;++i)
        entries[s->schedule->count*3+4+i]=transition_bind(i);
    if(offer_bind){for(uint32_t i=0;i<s->schedule->count;++i)for(uint32_t phase=0;phase<2;++phase)
        entries[s->schedule->count*4+4+2*i+phase]=offer_bind(i,phase);
        entries[s->schedule->count*6+4]=offer_bind(0,2);}
    uint64_t *arena=s->arena;size_t compact_words=s->value_words;
    if(!s->schedule->plan_released){compact_words=(arena_words()+7)&~(size_t)7;
        arena=aligned_alloc(64,(compact_words?compact_words:8)*8);
        if(!arena){free(hot);free(entries);free(memories);if(!s->current)free(current);dlclose(handle);return rds_fail(s,"cannot allocate compact arena");}
        memset(arena,0,(compact_words?compact_words:8)*8);
        for(uint32_t i=0;i<s->value_words;++i){uint32_t dst=arena_offset(i);if(dst!=RDS_NONE){
            if(arena_small(i))((uint8_t*)arena)[dst]=(uint8_t)s->arena[i];else arena[dst]=s->arena[i];}}}
    for(uint32_t i=0;i<s->nm;++i)memories[i]=s->mems[i].data;
    if(!s->current)for(uint32_t i=0;i<s->nv;++i)if(s->values[i].state!=SIZE_MAX)
        memcpy(current+s->values[i].state,s->arena+s->values[i].offset,s->values[i].words*8);
    s->current=current;
    if(arena!=s->arena){
        for(uint32_t i=0;i<s->nv;++i)s->values[i].offset=arena_offset((uint32_t)s->values[i].offset);
        free(s->arena);s->arena=arena;s->value_words=compact_words;
    }
    rds_compiled_sync_objects(s,true);
    size_t previous_hot=s->schedule->compiled_hot_bytes;
    rds_compiled_free(s);
    s->schedule->compiled_hot=hot;s->schedule->compiled_hot_bytes=hot_bytes;
    s->schedule->compiled_objects=objects;
    s->schedule->compiled_layout_key=get_layout_key();
    s->object_bytes=s->object_bytes-previous_hot+hot_bytes;
    s->schedule->compiled_memories=memories;
    s->schedule->compiled_entries=entries;
    s->schedule->compiled_handle = handle;
    s->schedule->compiled_bulk_fused=get_bulk_fused&&get_bulk_fused();
    s->schedule->compiled_blocks = get_blocks(); s->schedule->compiled_shapes = get_shapes();
    struct stat st; s->schedule->compiled_bytes = stat(path, &st) ? 0 : (size_t)st.st_size;
    if(!s->schedule->plan_released){
        rds_schedule *p=s->schedule;p->compiled_key=compiled_key;
        p->scheduled_operations=s->batches?p->batches[s->batches-1].first+p->batches[s->batches-1].count:0;
        /* The generated phase entries own execution. Retain only API-visible
         * value bindings and state allocation metadata, not an interpreter plan. */
        rds_value *ports=calloc(s->np?s->np:1,sizeof *ports);
        if(ports){for(uint32_t i=0;i<s->np;++i){ports[i]=s->values[s->ports[i].value];s->ports[i].value=i;}
            free(s->values);s->values=ports;s->nv=s->np;}
        free(s->ops);s->ops=NULL;free(s->args);s->args=NULL;free(s->imm);s->imm=NULL;
        free(s->regs);s->regs=NULL;free(s->writes);s->writes=NULL;free(s->reads);s->reads=NULL;
        for(uint32_t i=0;i<s->nc;++i)free(s->checks[i].name);
        free(s->checks);s->checks=NULL;
        for(uint32_t i=0;i<s->nx;++i){s->object_bytes-=s->objects[i].ni*sizeof *s->objects[i].inputs;free(s->objects[i].inputs);s->objects[i].inputs=NULL;}
        free(p->instructions);p->instructions=NULL;free(p->instruction_origins);p->instruction_origins=NULL;free(p->batches);p->batches=NULL;
        free(p->instruction_guards);p->instruction_guards=NULL;
        free(p->guard_unions);p->guard_unions=NULL;p->guard_union_count=0;
        free(p->instruction_cache);p->instruction_cache=NULL;free(p->cache_objects);p->cache_objects=NULL;free(p->cache_lanes);p->cache_lanes=NULL;free(p->cache_fields);p->cache_fields=NULL;free(p->field_masks);p->field_masks=NULL;free(p->flow_cache_objects);p->flow_cache_objects=NULL;
        free(p->constants);p->constants=NULL;
        rds_offer_free(p->offers);p->offers=NULL;
        for(uint32_t i=0;i<p->count;++i){free(p->workers[i].objects);p->workers[i].objects=NULL;}
        p->plan_released=true;
        s->descriptor_bytes=sizeof *s+s->nv*sizeof *s->values+s->np*sizeof *s->ports+s->nm*sizeof *s->mems;
        for(uint32_t i=0;i<s->np;++i)s->descriptor_bytes+=strlen(s->ports[i].name)+1;
        s->schedule_bytes=sizeof *p+p->count*sizeof *p->workers+(p->spin?p->count*sizeof *p->completions:0)
            +(p->bulk_progress?p->count*sizeof *p->bulk_progress:0)
            +(s->nm?s->nm:1)*sizeof *memories+(p->count*6+5)*sizeof *entries;
    }
    rds_compiled_sync_objects(s,false);
    s->evaluated = false; return 0;
}
rds_execution_stats rds_get_execution_stats(const rds_sim *s) {
    const rds_schedule *p = s->schedule;
    uint32_t n = p && p->plan_released ? p->scheduled_operations : p && s->batches ? p->batches[s->batches - 1].first + p->batches[s->batches - 1].count : s->no;
    return (rds_execution_stats){n, s->replicated_ops, p ? p->compiled_blocks : 0, p ? p->compiled_shapes : 0,
                                s->partition_work, s->partition_peak, p ? p->compiled_bytes : 0};
}

bool rds_is_compiled(const rds_sim *s){return s->schedule&&s->schedule->compiled_entries;}
int rds_compiled_call(rds_sim *s,uint32_t lane,uint32_t phase){
    rds_compiled_context c={s->arena,s->current,s->next,s->objects,s->schedule->compiled_memories,s->error,s->cycles,s->strict,s->schedule->compiled_hot};
    uint32_t index=phase<3?lane*3+phase:s->schedule->count*3+phase-3;
    return s->schedule->compiled_entries[index](&c);
}
void rds_compiled_sync_objects(rds_sim *s,bool publish){
    if(!s->schedule||!s->schedule->compiled_objects)return;
    rds_compiled_context c={s->arena,s->current,s->next,s->objects,s->schedule->compiled_memories,s->error,s->cycles,s->strict,s->schedule->compiled_hot};
    s->schedule->compiled_objects(&c,publish);
}
int rds_compiled_advance(rds_sim *s){
    if(!s->evaluated&&rds_eval(s))return -1;
    rds_schedule *p=s->schedule;
    rds_compiled_context c={s->arena,s->current,s->next,s->objects,p->compiled_memories,
        s->error,s->cycles,s->strict,p->compiled_hot};
    const rds_phase_fn *effects=p->compiled_entries+3*p->count;
    if(effects[0](&c))return -1;
    if(p->count==1){
        if(p->workers[0].prepared)return rds_fail(s,p->workers[0].prepare_error);
    }else if(rds_schedule_objects(s,1))return -1;
    if(effects[2](&c))return -1;
    if(effects[1](&c))return -1;
    if(p->count==1){
        if(p->compiled_entries[2](&c))return -1;
    }else if(s->parallel_publish) {
        if(rds_schedule_objects(s,2))return -1;
    } else {
        /* Readers finished at evaluation; publishing small prepared states
         * here avoids a worker epoch while preserving the snapshot boundary. */
        for(uint32_t lane=0;lane<p->count;++lane)
            if(p->compiled_entries[3*lane+2](&c))return -1;
    }
    if(s->copy_state)memcpy(s->current,s->next,s->next_words*8);
    else{uint64_t *old=s->current;s->current=s->next;s->next=old;}
    rds_finish_cycle(s);return 0;
}
