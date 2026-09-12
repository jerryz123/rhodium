/* Writes the fixed load-time plan and value storage bindings for compiler inspection. */
#define _POSIX_C_SOURCE 200809L
#include "schedule.h"
#include <inttypes.h>

static void string(FILE *f, const char *s) {
    fputc('"', f);
    for (const unsigned char *p=(const unsigned char *)s; *p; ++p) {
        if (*p=='"' || *p=='\\') fputc('\\',f);
        if (*p<32) fprintf(f,"\\u%04x",*p); else fputc(*p,f);
    }
    fputc('"',f);
}
int rds_emit_plan(rds_sim *s, const char *path) {
    rds_schedule *p=s->schedule;
    if (!p || p->plan_released) return rds_fail(s,"emit plan before compiled attachment");
    FILE *f=fopen(path,"w");
    if(!f) return rds_fail(s,"cannot open plan report");
    fprintf(f,"{\"format\":\"rhodium-static-plan-v1\",\"source_key\":\"%016" PRIx64 "\",\"compiled_key\":\"%016" PRIx64 "\",\"replicated_operations\":%u,\"estimated_work\":%" PRIu64 ",\"peak_work\":%" PRIu64 ",\"values\":[",s->source_key,rds_emission_key(s),s->replicated_ops,s->partition_work,s->partition_peak);
    for(uint32_t i=0;i<s->nv;++i) {
        rds_value *v=&s->values[i];
        fprintf(f,"%s[%u,%zu,",i?",":"",v->width,v->offset);
        if(v->state==SIZE_MAX)fputs("null",f);else fprintf(f,"%zu",v->state);
        fprintf(f,",%u]",v->origin);
    }
    fputs("],\"operations\":[",f);
    for(uint32_t i=0;i<s->no;++i) {
        rds_op *o=&s->ops[i];fprintf(f,"%s[%u,%u,[",i?",":"",o->code,o->out);
        for(uint32_t j=0;j<o->nargs;++j)fprintf(f,"%s%u",j?",":"",s->args[o->args+j]);
        fputs("],[",f);
        for(uint32_t j=0;j<o->nimm;++j)fprintf(f,"%s%" PRIu64,j?",":"",s->imm[o->imm+j]);
        fputs("]]",f);
    }
    fputs("],\"workers\":[",f);
    for(uint32_t i=0;i<p->count;++i) {
        worker *w=&p->workers[i];fprintf(f,"%s{\"id\":%u,\"batches\":[",i?",":"",i);
        for(uint32_t j=0;j<w->count;++j) {
            batch *b=&p->batches[w->first+j];fprintf(f,"%s[%u,%u,[",j?",":"",b->code,b->width);
            for(uint32_t k=0;k<b->count;++k) {
                instruction *in=&p->instructions[b->first+k];
                fprintf(f,"%s[%u,%u,%u,%u,%u]",k?",":"",in->out,in->a,in->b,in->c,p->instruction_origins[b->first+k]);
            }
            fputs("]]",f);
        }
        fputs("]}",f);
    }
    fputs("],\"epilogue\":[",f);
    for(uint32_t i=0;i<p->tail_count;++i){
        uint32_t instruction_id=p->tail_first+i;
        fprintf(f,"%s[%u,%u]",i?",":"",p->instructions[instruction_id].out,p->instruction_origins[instruction_id]);
    }
    fputs("],\"instruction_guards\":[",f);
    uint32_t instructions=s->batches?p->batches[s->batches-1].first+p->batches[s->batches-1].count:0;
    for(uint32_t i=0;i<instructions;++i){
        if(i)fputc(',',f);
        uint32_t g=p->instruction_guards?p->instruction_guards[i]:0;
        if(!g)fputs("null",f);
        else if(g & RDS_GUARD_OR){
            const rds_guard_or *u=&p->guard_unions[g & ~RDS_GUARD_OR];
            fputs("{\"or\":[",f);
            for(uint32_t j=0;j<u->count;++j){uint32_t a=u->atoms[j];fprintf(f,"%s[%u,%u]",j?",":"",(a-s->nx-s->nw-1)/2,(a-s->nx-s->nw-1)%2);}
            fputs("]}",f);
        }else fprintf(f,"[%u,%u]",(g-s->nx-s->nw-1)/2,(g-s->nx-s->nw-1)%2);
    }
    fputs("],\"instruction_cache\":[",f);
    for(uint32_t i=0;i<instructions;++i){
        if(i)fputc(',',f);
        uint32_t object=p->instruction_cache?p->instruction_cache[i]:RDS_NONE;
        if(object==RDS_NONE)fputs("null",f);else fprintf(f,"%u",object);
    }
    fputs("],\"field_caches\":[",f);
    bool first_field=true;
    if(p->cache_fields)for(uint32_t r=0;r<s->nr;++r)if(p->cache_fields[r]){
        fprintf(f,"%s{\"register\":%u,\"domain\":%u,\"words\":[",first_field?"":",",r,s->nx+r);first_field=false;
        bool first_word=true;
        for(uint32_t j=0;j<s->values[s->regs[r].q].words;++j){
            uint64_t mask=p->field_masks[s->regs[r].next+j];if(!mask)continue;
            fprintf(f,"%s[%zu,\"%016" PRIx64 "\"]",first_word?"":",",s->values[s->regs[r].q].state+j,mask);first_word=false;
        }
        fputs("]}",f);
    }
    fputs("],\"flow_cache_objects\":[",f);
    bool first_flow=true;
    if(p->flow_cache_objects)for(uint32_t id=0;id<s->nx;++id)if(p->flow_cache_objects[id]){
        fprintf(f,"%s%u",first_flow?"":",",id);first_flow=false;
    }
    fprintf(f,"],\"partition\":{\"reason\":\"%s\",\"candidate_workers\":%u,\"remaining_workers\":%u,\"candidate_work\":%" PRIu64 ",\"candidate_bytes\":%" PRIu64 ",\"baseline_peak\":%" PRIu64 ",\"prepare_local\":%s},\"objects\":[",s->partition_reason?s->partition_reason:"disabled",s->partition_candidate_workers,s->partition_remaining_workers,s->partition_candidate_work,s->partition_candidate_bytes,s->partition_baseline,p->prepare_local?"true":"false");
    for(uint32_t i=0;i<s->nx;++i){rds_object *o=&s->objects[i];fprintf(f,"%s[%u,%u,%u,%u,%u,",i?",":"",o->kind,o->width,o->depth,o->flags,o->owner);string(f,o->name);fputc(']',f);}
    fprintf(f,"],\"affinity\":{\"before_bytes\":%" PRIu64 ",\"after_bytes\":%" PRIu64 ",\"swaps\":%u},\"registers\":[",p->affinity_before,p->affinity_after,p->affinity_swaps);
    for(uint32_t i=0;i<s->nr;++i)fprintf(f,"%s[%u,%u,%zu]",i?",":"",s->regs[i].q,s->regs[i].owner,s->regs[i].next);
    fputc(']',f);rds_offer_report(f,s);fputs("}\n",f);
    int failed=ferror(f);if(fclose(f))failed=1;
    return failed?rds_fail(s,"cannot write plan report"):0;
}
