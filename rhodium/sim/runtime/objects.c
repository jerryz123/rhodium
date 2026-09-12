/* Executes native flow storage, arbitration, Sv39 TLBs, counters, and host objects. */
#include "internal.h"
#include "tlb.h"
RDS_TLB_CODE

enum { FIFO=1, PIPE, OFFER, SCOREBOARD, CREDIT, ARBITER, COUNTER, HOST, BROADCAST, MATCHER, TLB };
enum { DROP=1, PIPELINE=2, FLOW=4, VALID_ONLY=8, PACKET=16, PACKED_MATCHER=32, MATCHER_GRANTS=64 };
/* Requests are nonzero; select one bit at/after priority, wrapping once. */
static uint64_t matcher_grant(uint64_t requests, uint64_t priority) {
    uint64_t upper = priority < 64 ? requests & (UINT64_MAX << priority) : 0;
    uint64_t eligible = upper ? upper : requests;
    return eligible & (~eligible + 1);
}
static uint32_t matcher_index(uint64_t grant) {
    return (uint32_t)__builtin_ctzll(grant);
}
static uint32_t next_index(uint32_t index, uint32_t depth) {
    return index+1==depth?0:index+1;
}
static uint64_t input(rds_sim *s, rds_object *o, uint32_t i) {
    return o->inputs[i] == RDS_NONE ? 0 : rds_data(s,o->inputs[i])[0];
}
static int failure(rds_sim *s, rds_object *o, const char *message) {
    snprintf(s->error,sizeof s->error,"%s: %s",o->name,message); return -1;
}
void rds_objects_free(rds_sim *s) {
    if (!s->objects) return;
    for (uint32_t i=0;i<s->nx;++i) {
        rds_object *o=&s->objects[i];
        free(o->inputs); free(o->name); free(o->data); free(o->pending); free(o->valid); free(o->changes);
    }
    free(s->objects);
}
int rds_objects_init(rds_sim *s) {
    for (uint32_t i=0;i<s->nx;++i) {
        rds_object *o=&s->objects[i];
        if (!o->name || o->kind<1 || o->kind>TLB || !o->depth || o->depth>UINT32_MAX/2 || o->width>UINT32_MAX-63) return -1;
        uint32_t allowed=o->kind==FIFO?7:o->kind==PIPE?9:o->kind==OFFER||o->kind==BROADCAST?1:o->kind==ARBITER?19:o->kind==MATCHER?PACKED_MATCHER|MATCHER_GRANTS:0;
        if ((o->flags&~allowed) || (o->kind==OFFER&&o->depth!=1)
            || (o->kind==SCOREBOARD&&o->width!=o->depth)
            || (o->kind==ARBITER&&(o->flags&PACKET)&&!(o->flags&PIPELINE))
            || (o->kind==MATCHER&&(o->flags&MATCHER_GRANTS)&&!(o->flags&PACKED_MATCHER))) return -1;
        uint64_t expected=o->kind==MATCHER?1+(uint64_t)o->width*o->depth+o->depth:o->kind<=OFFER?4:o->kind==SCOREBOARD?5:o->kind==CREDIT?3:o->kind==ARBITER?2+(uint64_t)(o->flags&PACKET?3:2)*o->depth:o->kind==COUNTER?2:o->kind==BROADCAST?3+o->depth:5;
        if(o->kind==MATCHER&&(o->flags&PACKED_MATCHER))expected=2+o->depth;
        if(o->kind==TLB)expected=17;
        if (o->ni!=expected) return -1;
        o->words=(o->width+63)/64;
        if ((o->kind==CREDIT || o->kind==COUNTER) && o->width>64) return -1;
        size_t words=0, pending=0;
        if (o->kind<=OFFER && !(o->flags&DROP)) { words=(size_t)o->words*o->depth; pending=o->words; }
        if (o->kind==BROADCAST && !(o->flags&DROP)) words=pending=o->words;
        if (o->kind==SCOREBOARD) words=o->words;
        if (o->kind==HOST || o->kind==MATCHER) words=pending=o->depth;
        if(o->kind==TLB){if(o->width!=81||o->depth<2||(o->depth&(o->depth-1)))return -1;words=(size_t)o->depth*2;pending=2;}
        if (o->kind==MATCHER && (!o->width || o->width>64 || o->depth>64)) return -1;
        if (words>SIZE_MAX/8 || pending>SIZE_MAX/8) return -1;
        if (words && !(o->data=calloc(words,8))) return -1;
        if (pending && !(o->pending=calloc(pending,8))) return -1;
        if (o->kind==PIPE || o->kind==BROADCAST) {
            o->valid=calloc(o->depth,1); o->changes=calloc(o->depth,1);
            if (!o->valid || !o->changes) return -1;
        }
        s->object_bytes+=sizeof *o+o->ni*sizeof *o->inputs+strlen(o->name)+1+(words+pending)*8+((o->kind==PIPE||o->kind==BROADCAST)?2*o->depth:0);
        if (o->kind<=OFFER || o->kind==BROADCAST) s->payload_bytes+=(words+pending)*8;
        /* Required controls must be present; omitted slots are only payload or
         * the reset of a stateless arbiter. Widths are checked before reads. */
        for (uint32_t j=0;j<o->ni;++j) {
            bool payload=((o->kind<=OFFER||o->kind==BROADCAST)&&j==2)||(o->kind==ARBITER&&j>=3&&j<2+2*o->depth&&(j&1));
            uint32_t id=o->inputs[j];
            if (id==RDS_NONE) {
                if (payload && o->width && !(o->flags&DROP)) return -1;
                if (!payload && !(o->kind==ARBITER&&j==0) && !(o->kind==PIPE&&(o->flags&VALID_ONLY)&&j==3)) return -1;
            } else {
                uint32_t w=s->values[id].width;
                if (payload && w!=o->width) return -1;
                if (!payload && w>64) return -1;
                bool index=o->kind==SCOREBOARD&&(j==2||j==4);
                if(o->kind==TLB){
                    static const unsigned widths[]={1,1,1,1,27,53,64,1,2,2,1,1,64,1,2,1,1};
                    if(w!=widths[j])return -1;
                }else if(o->kind==MATCHER&&(o->flags&PACKED_MATCHER)){
                    if(w!=(j==0?1:j<=o->depth?o->width:o->depth))return -1;
                }else if (!payload && !index && o->kind!=HOST && w!=1) return -1;
            }
        }
    }
    for (uint32_t i=0;i<s->no;++i) {
        rds_op *op=&s->ops[i];
        if (op->code!=RDS_OBJECT_QUERY) continue;
        rds_object *o=&s->objects[s->imm[op->imm]];
        uint64_t q=s->imm[op->imm+1]; uint32_t nargs=0, width=1;
        if (o->kind<=OFFER) {
            if (q>3 || (o->kind!=FIFO && q==0) || (o->kind==OFFER&&q==1)) return -1;
            if (q==0) { for(uint32_t d=o->depth;d>>=1;) ++width; }
            if (q==1) nargs=o->kind==PIPE || (o->flags&PIPELINE)?1:0;
            if (q==2) nargs=o->kind==FIFO&&(o->flags&FLOW)?1:0;
            if (q==3) { width=o->width; nargs=o->kind==FIFO&&(o->flags&FLOW)&&!(o->flags&DROP)&&o->width?1:0; }
        } else if(o->kind==TLB) {
            if(q>5)return -1;
            width=q==0||q==5?128:1;nargs=q==0||q==5?2:q==1?6:q==2?5:q==3?5:4;
        } else if(o->kind==MATCHER) {
            if(q>=2*o->depth)return -1;
            width=o->width; nargs=q>=o->depth?1+o->width:(uint32_t)(q+1)*o->width;
            if(o->flags&PACKED_MATCHER)nargs=q>=o->depth?2:(uint32_t)q+1;
        } else if (o->kind==BROADCAST) {
            if(q>=2+o->depth) return -1;
            if(q==0) nargs=o->depth;
            if(q==1) width=o->width;
        } else if (o->kind==SCOREBOARD) { if(q) return -1; width=o->width; }
        else if (o->kind==CREDIT) { if(q>2) return -1; width=q?1:o->width; }
        else if (o->kind==COUNTER) { if(q>1) return -1; width=q?1:o->width; nargs=q?1:0; }
        else if (o->kind==HOST) { if(q>=o->depth) return -1; width=s->values[op->out].width; if(width>64) return -1; }
        else {
            if(q>=3+o->depth) return -1;
            nargs=q==2?(o->flags&DROP?0:2*o->depth):o->depth+(q>=3);
            if(q==0) { for(uint32_t d=o->depth-1;d>>=1;) ++width; }
            if(q==2) width=o->width;
        }
        if(!width || op->nargs!=nargs || s->values[op->out].width!=width) return -1;
        for(uint32_t j=0;j<nargs;++j) {
            if(o->kind==TLB){
                unsigned aw=q==0||q==5?(j?1:64):q<=2?(j==0?64:j==1?1:j<(q==1?4u:3u)?2:1):j==0?128:j<(q==3?3u:2u)?2:1;
                if(s->values[s->args[op->args+j]].width!=aw)return -1;
                continue;
            }
            uint32_t aw=(q==3&&o->kind==FIFO)||(q==2&&o->kind==ARBITER&&j>=o->depth)?o->width:1;
            if(o->kind==MATCHER&&q>=o->depth&&j==0)aw=o->width;
            if(o->kind==MATCHER&&(o->flags&PACKED_MATCHER))aw=o->width;
            if(s->values[s->args[op->args+j]].width!=aw) return -1;
        }
    }
    return 0;
}
int rds_bind_host(rds_sim *s, const char *name, rds_host_fn fn, void *context) {
    rds_compiled_sync_objects(s,true);
    bool found=false;
    for (uint32_t i=0;i<s->nx;++i) if (s->objects[i].kind==HOST && (!name || !strcmp(name,s->objects[i].name))) {
        s->objects[i].host=fn; s->objects[i].context=context; found=true;
    }
    if (found) { s->evaluated = false; rds_compiled_sync_objects(s,false); }
    return found?0:rds_fail(s,"host occurrence not found");
}
void rds_set_object_trace(rds_sim *s, rds_object_trace_fn fn, void *context) {
    s->object_trace=fn; s->trace_context=context;
}
int rds_object_query(rds_sim *s, const rds_op *op) {
    rds_object *o=&s->objects[s->imm[op->imm]];
    uint32_t q=(uint32_t)s->imm[op->imm+1], *args=s->args+op->args;
    uint64_t *d=rds_data(s,op->out), v=0;
    size_t bytes=(size_t)s->values[op->out].words*8;
    memset(d,0,bytes);
#define ARG(i) rds_data(s,args[i])[0]
    switch (o->kind) {
    case TLB: {
        rds_tlb_result lookup=q==3||q==4?(rds_tlb_result){ARG(0),rds_data(s,args[0])[1]}:rds_tlb_lookup(o->data,o->depth,ARG(0),ARG(1)!=0);
        if(!q||q==5){d[0]=lookup.low;d[1]=lookup.metadata;return 0;}
        bool probe=q==2||q==4;unsigned base=q>=3?1:2;
        v=rds_tlb_fault(lookup,probe?0:ARG(base),ARG(base+!probe),ARG(base+!probe+1)!=0,ARG(base+!probe+2)!=0,probe);
        break;
    }
    case MATCHER: {
        if(q>=o->depth) {
            uint64_t requests=0;
            if(o->flags&PACKED_MATCHER)requests=ARG(1);
            else for(uint32_t row=0;row<o->width;++row)requests|=(ARG(1+row)&1)<<row;
            v=matcher_grant(requests&~ARG(0),o->data[q-o->depth]);break;
        }
        uint64_t taken=0;
        for(uint32_t col=0;col<=q;++col) {
            uint64_t requests=0;
            if(o->flags&PACKED_MATCHER)requests=ARG(col);
            else for(uint32_t row=0;row<o->width;++row) requests |= (ARG(col*o->width+row)&1) << row;
            v=matcher_grant(requests & ~taken,o->data[col]); taken |= v;
        }
        break;
    }
    case FIFO:
        if (q==0) v=o->count;
        else if (q==1) v=o->count<o->depth || ((o->flags&PIPELINE)&&ARG(0));
        else if (q==2) v=o->count>0 || ((o->flags&FLOW)&&ARG(0));
        else if (q==3 && o->data) {
            const uint64_t *p=(o->flags&FLOW)&&!o->count?rds_data(s,args[0]):o->data+(size_t)o->head*o->words;
            memcpy(d,p,bytes); return 0;
        }
        break;
    case PIPE:
        if (q==1) { v=ARG(0); for (uint32_t i=o->depth;i--;) v=!o->valid[i]||v; }
        else if (q==2) v=o->valid[o->depth-1];
        else if (q==3 && o->data) { memcpy(d,o->data+(size_t)(o->depth-1)*o->words,bytes); return 0; }
        break;
    case OFFER:
        if (q==2) v=o->count;
        else if (q==3 && o->data) { memcpy(d,o->data,bytes); return 0; }
        break;
    case BROADCAST:
        if (q==0) { v=1; for(uint32_t i=0;i<o->depth;++i) if(o->valid[i]&&!ARG(i)) { v=0; break; } }
        else if (q==1 && o->data) { memcpy(d,o->data,bytes); return 0; }
        else if(q>=2) v=o->valid[q-2];
        break;
    case SCOREBOARD: memcpy(d,o->data,bytes); return 0;
    case CREDIT: v=q==0?o->count:q==1?o->count==0:o->count==o->depth; break;
    case COUNTER: v=q==0?o->count:o->count==o->depth-1 && ARG(0); break;
    case HOST:
        if (q>=o->depth || bytes>8) return rds_fail(s,"invalid host query");
        v=o->data[q]; break;
    case ARBITER: {
        if (q==2 && (o->flags&DROP)) return 0;
        uint32_t selected=RDS_NONE;
        for (uint32_t i=0,j=o->head;i<o->depth;++i,j=next_index(j,o->depth)) {
            if (ARG(j)) { selected=j; break; }
        }
        if ((o->flags&PACKET)&&o->count) selected=o->tail;
        if (q==0) v=selected==RDS_NONE?0:selected;
        else if (q==1) v=selected!=RDS_NONE&&ARG(selected);
        else if (q==2) { memcpy(d,rds_data(s,args[o->depth+(selected==RDS_NONE?0:selected)]),bytes); return 0; }
        else v=selected==q-3 && ARG(o->depth);
        break;
    }
    }
#undef ARG
    d[0]=v; rds_trim(d,s->values[op->out].width); return 0;
}
int rds_object_prepare(rds_sim *s, uint32_t i) {
    {
        rds_object *o=&s->objects[i];
        o->reset=input(s,o,0)!=0; o->enqueue=o->dequeue=false;
        o->next_count=o->count; o->next_head=o->head; o->next_tail=o->tail;
        switch (o->kind) {
        case TLB:
            o->reset|=input(s,o,1)!=0;o->enqueue=input(s,o,2)&&!input(s,o,3);
            o->next_head=o->reset?0:o->enqueue?(o->head+1)&(o->depth-1):o->head;
            if(o->enqueue){o->pending[0]=input(s,o,5)|(UINT64_C(1)<<53);o->pending[1]=input(s,o,4);}
            break;
        case MATCHER: {
            uint64_t taken=0;
            for(uint32_t col=0;col<o->depth;++col) {
                if(!o->reset && o->data[col]>=o->width) return failure(s,o,"matcher priority out of range");
                uint64_t requests=0;
                if(o->flags&PACKED_MATCHER)requests=input(s,o,1+col);
                else for(uint32_t row=0;row<o->width;++row) requests|=(input(s,o,1+row*o->depth+col)&1)<<row;
                uint64_t grant=o->flags&MATCHER_GRANTS?requests:matcher_grant(requests & ~taken,o->data[col]); taken|=grant;
                if(!o->reset&&(o->flags&MATCHER_GRANTS)&&(grant&(grant-1)))return failure(s,o,"matcher grant must be one-hot or zero");
                bool accepted=o->flags&PACKED_MATCHER?(input(s,o,1+o->depth)>>col)&1:input(s,o,1+o->width*o->depth+col)!=0;
                o->pending[col]=o->reset?0:grant&&accepted?next_index(matcher_index(grant),o->width):o->data[col];
            }
            break;
        }
        case FIFO: {
            bool valid=input(s,o,1), ready=input(s,o,3), empty=!o->count;
            bool accept=o->count<o->depth || ((o->flags&PIPELINE)&&ready);
            o->enqueue=valid&&accept&&!((o->flags&FLOW)&&empty&&ready);
            o->dequeue=!empty&&ready;
            o->next_count=o->reset?0:o->count+o->enqueue-o->dequeue;
            o->next_head=o->reset?0:o->dequeue?next_index(o->head,o->depth):o->head;
            o->next_tail=o->reset?0:o->enqueue?next_index(o->tail,o->depth):o->tail;
            break;
        }
        case PIPE: {
            bool ready=(o->flags&VALID_ONLY)||input(s,o,3);
            for (uint32_t j=o->depth;j--;) {
                ready=!o->valid[j]||ready;
                bool valid=j?o->valid[j-1]:input(s,o,1)!=0;
                o->changes[j]=(unsigned char)((ready&&valid?2:0)|(!o->reset&&(ready?valid:o->valid[j])?1:0));
            }
            o->enqueue=(o->changes[0]&2)!=0;
            break;
        }
        case OFFER:
            o->enqueue=input(s,o,1)!=0;
            o->next_count=o->reset?0:o->enqueue?1:input(s,o,3)?0:o->count;
            break;
        case BROADCAST: {
            bool ready=true;
            for(uint32_t j=0;j<o->depth;++j) if(o->valid[j]&&!input(s,o,3+j)) ready=false;
            o->enqueue=ready&&input(s,o,1); o->next_count=0;
            for(uint32_t j=0;j<o->depth;++j) {
                bool accepted=o->valid[j]&&input(s,o,3+j);
                o->dequeue|=accepted;
                o->changes[j]=!o->reset&&(o->enqueue||(!accepted&&o->valid[j]));
                o->next_count+=o->changes[j];
            }
            break;
        }
        case SCOREBOARD: {
            bool set=input(s,o,1), clear=input(s,o,3);
            o->set=input(s,o,2); o->clear=input(s,o,4);
            if (!o->reset) {
                if (set&&o->set>=o->depth) return failure(s,o,"scoreboard_set_index_in_range");
                if (clear&&o->clear>=o->depth) return failure(s,o,"scoreboard_clear_index_in_range");
                if (set&&(o->data[o->set/64]>>(o->set%64)&1)&&!(clear&&o->clear==o->set)) return failure(s,o,"scoreboard_set_free");
                if (clear&&!(o->data[o->clear/64]>>(o->clear%64)&1)&&!(set&&o->clear==o->set)) return failure(s,o,"scoreboard_clear_busy");
            }
            o->enqueue=set; o->dequeue=clear; break;
        }
        case CREDIT: {
            bool inc=input(s,o,1), dec=input(s,o,2);
            if (!o->reset && ((dec&&!o->count)||(inc&&!dec&&o->count==o->depth))) return failure(s,o,"credit counter underflow/overflow");
            o->enqueue=inc; o->dequeue=dec;
            o->next_count=o->reset?0:o->count+inc-dec; break;
        }
        case COUNTER: o->next_count=o->reset?0:!input(s,o,1)?o->count:o->count==o->depth-1?0:o->count+1; break;
        case ARBITER:
            if (o->flags&PIPELINE) {
                uint32_t selected=RDS_NONE;
                for (uint32_t j=0,k=o->head;j<o->depth;++j,k=next_index(k,o->depth)) {
                    if (input(s,o,2+2*k)) { selected=k; break; }
                }
                if ((o->flags&PACKET)&&o->count) selected=o->tail;
                bool transfer=selected!=RDS_NONE&&input(s,o,2+2*selected)&&input(s,o,1);
                o->dequeue=transfer;
                if (o->flags&PACKET) {
                    bool last=transfer&&input(s,o,2+2*o->depth+selected);
                    o->next_count=o->reset||last?0:transfer?1:o->count;
                    o->next_tail=o->reset?0:transfer&&!o->count?selected:o->tail;
                    o->next_head=o->reset?0:last?next_index(selected,o->depth):o->head;
                } else o->next_head=o->reset?0:transfer?next_index(selected,o->depth):o->head;
            }
            break;
        case HOST: if (!o->host) return failure(s,o,"host callback is not bound"); break;
        }
        if ((o->kind<=OFFER||o->kind==BROADCAST) && o->enqueue && o->pending)
            memcpy(o->pending,rds_data(s,o->inputs[2]),(size_t)o->words*8);
    }
    return 0;
}
int rds_objects_prepare_range(rds_sim *s, uint32_t first, uint32_t stride) {
    for (uint32_t i = 0; i < s->nx; ++i)
        if ((stride==1 || s->objects[i].owner==first) && rds_object_prepare(s, i)) return -1;
    return 0;
}
int rds_objects_prepare(rds_sim *s) {
    if (!s->nx) return 0;
    if (s->schedule ? rds_schedule_objects(s,1) : rds_objects_prepare_range(s,0,1)) return -1;
    /* Host effects execute once, after all failure-capable hardware validation. */
    for (uint32_t i=0;i<s->nx;++i) {
        rds_object *o=&s->objects[i];
        if (o->kind==HOST) {
            uint64_t args[5]; for (uint32_t j=0;j<5;++j) args[j]=input(s,o,j);
            if (o->host(o->context,args,5,o->pending,o->depth)) return failure(s,o,"host callback failed");
        }
    }
    return 0;
}
void rds_objects_commit(rds_sim *s) {
    if (!s->nx) return;
    if (s->schedule) (void)rds_schedule_objects(s,2);
    else rds_objects_commit_range(s,0,1);
}
void rds_object_commit(rds_sim *s, uint32_t i) {
    {
        rds_object *o=&s->objects[i]; size_t bytes=(size_t)o->words*8;
        switch (o->kind) {
        case TLB:
            if(o->reset)memset(o->data,0,(size_t)o->depth*16);
            else if(o->enqueue)memcpy(o->data+2*o->head,o->pending,16);
            break;
        case MATCHER: memcpy(o->data,o->pending,o->depth*8); break;
        case FIFO: if (o->enqueue&&o->data) memcpy(o->data+(size_t)o->tail*o->words,o->pending,bytes); break;
        case OFFER: if (o->enqueue&&o->data) memcpy(o->data,o->pending,bytes); break;
        case PIPE:
            for (uint32_t j=o->depth;j--;) {
                if ((o->changes[j]&2)&&o->data) memcpy(o->data+(size_t)j*o->words,j?o->data+(size_t)(j-1)*o->words:o->pending,bytes);
                o->valid[j]=o->changes[j]&1;
            }
            break;
        case BROADCAST:
            if (o->enqueue&&o->data) memcpy(o->data,o->pending,bytes);
            memcpy(o->valid,o->changes,o->depth); break;
        case SCOREBOARD:
            if (o->reset) memset(o->data,0,bytes);
            else {
                if (o->enqueue) o->data[o->set/64]|=UINT64_C(1)<<(o->set%64);
                if (o->dequeue) o->data[o->clear/64]&=~(UINT64_C(1)<<(o->clear%64));
            }
            break;
        case HOST: memcpy(o->data,o->pending,(size_t)o->depth*8); break;
        }
        o->head=o->next_head; o->tail=o->next_tail; o->count=o->next_count;
    }
}

void rds_objects_commit_range(rds_sim *s, uint32_t first, uint32_t stride) {
    for (uint32_t i = 0; i < s->nx; ++i)
        if (stride==1 || s->objects[i].owner==first) rds_object_commit(s, i);
}
