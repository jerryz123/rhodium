/* Compiles semantic object queries, unique-owner transitions, and RTL state effects. */
#define _POSIX_C_SOURCE 200809L
#include "codegen.h"
#include <inttypes.h>
enum { FIFO = 1, PIPE, OFFER, SCOREBOARD, CREDIT, ARBITER, COUNTER, HOST, BROADCAST, MATCHER, TLB };
enum { DROP = 1, PIPELINE = 2, FLOW = 4, VALID_ONLY = 8, PACKET = 16, PACKED_MATCHER = 32, MATCHER_GRANTS = 64 };
/* Generated object storage contains only live state, with inline payloads.
 * The original descriptors are cold compatibility/debug state after attachment. */
static size_t object_array(const rds_object *o, unsigned field) {
    if(field>=2)return o->kind==PIPE||o->kind==BROADCAST?o->depth:0;
    if(o->kind<=OFFER && !(o->flags&DROP))return (size_t)o->words*(field?1:o->depth);
    if(o->kind==BROADCAST && !(o->flags&DROP))return o->words;
    if(o->kind==SCOREBOARD)return field?0:o->words;
    if(o->kind==HOST||o->kind==MATCHER)return o->depth;
    if(o->kind==TLB)return field?2:(size_t)o->depth*2;
    return 0;
}
static bool fifo_indices(const rds_object *o){return o->kind==FIFO&&o->depth>1&&o->data;}
/* Validated matchers have at most 64 request rows. Reset and each accepted
 * grant keep priorities in that range; snapshots and proposals fit one byte. */
static bool byte_priorities(const rds_sim *s,const rds_object *o){
    return s->lift_primitives&&s->schedule->count==1&&o->kind==MATCHER;
}
/* Update inputs are pinned through publication. Materialized single-entry FIFO
 * payloads can bypass private pending scratch; current data changes only at the
 * existing commit boundary. Direct-to-pending producers keep their own path. */
static bool arena_fifo_payload(const rds_sim *s,uint32_t id,const bool *pending){
    const rds_object *o=&s->objects[id];
    if(!(s->lift_primitives&&s->schedule->count==1&&o->kind==FIFO&&o->depth==1&&o->data&&!pending[id]))return false;
    // Public setters can change input storage, including from a host callback.
    // Produced pinned values and old register banks have no such writer.
    for(uint32_t p=0;p<s->np;++p)
        if(s->ports[p].direction==0&&s->ports[p].value==o->inputs[2])return false;
    return true;
}
static bool valid_pipe_publication(const rds_sim *s,const rds_object *o){
    return s->flow_prepare&&s->schedule->count==1&&o->kind==PIPE&&o->depth==1&&(o->flags&VALID_ONLY);
}
static bool object_name(const rds_object *o){return o->kind==SCOREBOARD||o->kind==CREDIT||o->kind==HOST||o->kind==MATCHER;}
static bool object_field(const rds_object *o, unsigned field) {
    if(o->kind==TLB)return field==0||field==2;
    if(field<4)return fifo_indices(o)||o->kind==ARBITER;
    if(field<6)return o->kind==FIFO||o->kind==OFFER||o->kind==CREDIT||o->kind==COUNTER||o->kind==ARBITER||o->kind==BROADCAST;
    return o->kind==SCOREBOARD;
}
/* Dense snapshot controls are a single-worker layout; parallel owners retain
 * separate cache-line regions and their existing object-local controls. */
static const char *count_type(const rds_object *o){
    return o->depth==1||o->kind==ARBITER||o->kind==OFFER?"bool":
        o->depth<256?"uint8_t":o->depth<65536?"uint16_t":"uint32_t";
}
void rds_c_object_view(char *out,size_t size,const rds_sim *s,uint32_t id,bool validity){
    if(s->schedule->count!=1){snprintf(out,size,"h->o%u.%s",id,validity?"valid":"count");return;}
    if(validity){snprintf(out,size,"h->valid_%u",id);return;}
    const char *type=count_type(&s->objects[id]);uint32_t index=0;
    for(uint32_t i=0;i<id;++i)if(object_field(&s->objects[i],4)&&!strcmp(count_type(&s->objects[i]),type))++index;
    snprintf(out,size,"h->counts_%s[%u]",type,index);
}
void rds_c_object_ref(FILE *f,const rds_sim *s,uint32_t id){
    const rds_object *o=&s->objects[id];char view[64];
    fprintf(f,"object_%u*o=&h->o%u;",id,id);
    if(s->schedule->count==1)fprintf(f,"prepared_%u*p=&h->p%u;",id,id);
    else fprintf(f,"object_%u*p=o;",id);
    fputs("(void)o;(void)p;",f);
    if(object_field(o,4)){rds_c_object_view(view,sizeof view,s,id,false);
        fprintf(f,"%s*occupancy=&%s;(void)occupancy;",count_type(o),view);}
    if(object_array(o,2)){rds_c_object_view(view,sizeof view,s,id,true);
        fprintf(f,"unsigned char*validity=%s;(void)validity;",view);}
}
static bool memory_view_used(const rds_sim *s,uint32_t memory){
    for(uint32_t i=0;i<s->no;++i)if(rds_c_memory_view(s,&s->ops[i])&&s->imm[s->ops[i].imm]==memory)return true;
    return false;
}
void rds_c_object_layout(FILE *f,const rds_sim *s,const bool *pending){
    uint32_t borrowed=0;size_t bytes=0;
    for(uint32_t id=0;id<s->nx;++id)if(arena_fifo_payload(s,id,pending)){++borrowed;bytes+=object_array(&s->objects[id],1)*8;}
    fprintf(f,"/* pinned FIFO payloads: %u, elided pending bytes: %zu */\n",borrowed,bytes);
    if(s->flow_prepare&&s->schedule->count==1)
        fputs("static inline bool cg_fifo_active4(const void*p){\n"
              "#if defined(__x86_64__) && defined(__SSE4_1__)\n"
              "__m128i controls;memcpy(&controls,p,16);return !_mm_testz_si128(controls,_mm_set1_epi32(-256));\n"
              "#else\nconst unsigned char*b=p;return b[1]|b[2]|b[3]|b[5]|b[6]|b[7]|b[9]|b[10]|b[11]|b[13]|b[14]|b[15];\n#endif\n}\n",f);
    if(s->schedule->flow_cache_objects)
        fputs("static inline uint64_t cg_flow_dirty(const bool*p,unsigned n){uint64_t v=0;\n"
              "#if defined(__BYTE_ORDER__) && __BYTE_ORDER__==__ORDER_LITTLE_ENDIAN__\n"
              "memcpy(&v,p,n);\n#else\nfor(unsigned j=0;j<n;++j)v|=(uint64_t)p[j]<<(8*j);\n#endif\nreturn v;}\n",f);
    const char *fields[]={"head","tail","next_head","next_tail","count","next_count","set","clear"};
    const char *arrays[]={"data","pending","valid","changes"};
    for(uint32_t i=0;i<s->nx;++i){const rds_object *o=&s->objects[i];
        if(s->schedule->count==1){
            fputs("typedef struct{",f);
            if(object_field(o,5))fprintf(f,"%s next_count;",count_type(o));
            fprintf(f,"bool enqueue,dequeue,reset;}prepared_%u;\n",i);
        }
        fputs("typedef struct{",f);if(object_name(o))fputs("const char*name;",f);
        if(s->lift_primitives&&o->kind==MATCHER)fputs("uint64_t changed;",f);
        if(s->lift_primitives&&o->kind==SCOREBOARD&&o->words==1)fputs("uint64_t proposal;",f);
        if(o->kind==HOST)fputs("rds_host_fn host;void*context;",f);
        for(unsigned j=0;j<4;++j){size_t n=object_array(o,j);
            if(j==1&&arena_fifo_payload(s,i,pending))continue;
            if(n&&!(s->schedule->count==1&&j==2))fprintf(f,"%s %s[%zu];",j<2&&!byte_priorities(s,o)?"uint64_t":"unsigned char",arrays[j],n);}
        for(unsigned j=0;j<8;++j)if(object_field(o,j)&&!(s->schedule->count==1&&(j==4||j==5))){
            const char *type=j>=6?"uint64_t":o->depth<256?"uint8_t":o->depth<65536?"uint16_t":"uint32_t";
            if(j>=4&&j<6&&(o->depth==1||o->kind==ARBITER||o->kind==OFFER))type="bool";
            fprintf(f,"%s %s;",type,fields[j]);}
        bool storage=object_name(o)||object_array(o,0)||object_array(o,1)||object_array(o,3)||object_field(o,0)||object_field(o,6);
        fprintf(f,"%s}object_%u;\n",s->schedule->count==1?(storage?"":"char unused;"):"bool enqueue,dequeue,reset;",i);
    }
    fputs("typedef struct{",f);
    if(!s->nx)fputs("char empty;",f);
    for(uint32_t i=0;i<s->nm;++i)if(memory_view_used(s,i))fprintf(f,"const uint64_t*memory_%u;",i);
    for(uint32_t i=0;i<s->no;++i)if(rds_c_kernel_cacheable(s,&s->ops[i]))fprintf(f,"uint64_t contract_cache_%u[2];",i);
    if(s->schedule->cache_objects)for(uint32_t lane=0;lane<s->schedule->count;++lane)
        fprintf(f,"%sbool payload_dirty_%u[%u];",s->schedule->count>1?"_Alignas(64) ":"",lane,s->nx?s->nx:1);
    if(s->schedule->cache_fields){
        fputs("bool fields_valid;",f);
        for(uint32_t r=0;r<s->nr;++r)if(s->schedule->cache_fields[r]){
            uint32_t words=0;for(uint32_t j=0;j<s->values[s->regs[r].q].words;++j)
                words+=s->schedule->field_masks[s->regs[r].next+j]!=0;
            fprintf(f,"uint64_t field_key_%u[%u];",r,words);
        }
    }
    if(s->schedule->count==1){
        for(uint32_t i=0;i<s->nx;++i)fprintf(f,"prepared_%u p%u;",i,i);
        const char *types[]={"bool","uint8_t","uint16_t","uint32_t"};
        for(unsigned t=0;t<4;++t){uint32_t n=0;
            for(uint32_t i=0;i<s->nx;++i)if(object_field(&s->objects[i],4)&&!strcmp(count_type(&s->objects[i]),types[t]))++n;
            if(n)fprintf(f,"%s counts_%s[%u];",types[t],types[t],n);}
        for(uint32_t i=0;i<s->nx;++i){size_t n=object_array(&s->objects[i],2);
            if(n)fprintf(f,"unsigned char valid_%u[%zu];",i,n);}
    }
    for(uint32_t lane=0;lane<s->schedule->count;++lane){bool first=true;
        for(uint32_t i=0;i<s->nx;++i){
            if(rds_c_offer_owner(s,i)!=lane)continue;
            fprintf(f,"%sobject_%u o%u;",first?"_Alignas(64) ":"",i,i);first=false;}}
    rds_c_offer_layout(f,s);
    fputs("}object_state;\nsize_t rds_generated_object_bytes(void){return sizeof(object_state); }\n"
          "void rds_generated_objects(rds_compiled_context*c,bool publish){object_state*h=c->hot;(void)c;(void)publish;(void)h;\n",f);
    for(uint32_t i=0;i<s->nm;++i)if(memory_view_used(s,i))fprintf(f,"if(!publish)h->memory_%u=c->memories[%u];\n",i,i);
    if(s->nx&&s->schedule->cache_objects)for(uint32_t lane=0;lane<s->schedule->count;++lane)
        fprintf(f,"if(!publish)memset(h->payload_dirty_%u,1,%u);\n",lane,s->nx);
    if(s->schedule->cache_fields)fputs("if(!publish)h->fields_valid=false;\n",f);
    for(uint32_t i=0;i<s->no;++i)if(rds_c_kernel_cacheable(s,&s->ops[i]))fprintf(f,"if(!publish)h->contract_cache_%u[0]=0;\n",i);
    for(uint32_t i=0;i<s->nx;++i){const rds_object *o=&s->objects[i];
        fputs("{",f);rds_c_object_ref(f,s,i);fprintf(f,"rds_object*d=c->objects+%u;",i);
        fputs("if(!publish){",f);if(object_name(o))fputs("o->name=d->name;",f);
        if(o->kind==HOST)fputs("o->host=d->host;o->context=d->context;",f);
        fputs("}\n",f);
        for(unsigned j=0;j<4;++j){size_t n=object_array(o,j);if(n){
            if(byte_priorities(s,o)&&j<2){
                if(j==0)fprintf(f,"for(unsigned j=0;j<%zu;++j){if(publish)d->data[j]=o->data[j];else o->data[j]=(unsigned char)d->data[j];}",n);
                else fprintf(f,"if(publish){for(unsigned j=0;j<%zu;++j)d->pending[j]=(o->changed>>j)&1?o->pending[j]:o->data[j];}else{for(unsigned j=0;j<%zu;++j)o->pending[j]=(unsigned char)d->pending[j];o->changed=UINT64_C(0x%016" PRIx64 ");}",n,n,rds_mask(o->depth));
                continue;
            }
            if(j==1&&arena_fifo_payload(s,i,pending)){
                // Pending is private scratch. Attachment invalidates evaluation
                // and recomputes every proposal before another publication.
                fprintf(f,"if(publish)memcpy(d->pending,o->data,%zu);",n*8);
                continue;
            }
            if(s->lift_primitives&&o->kind==MATCHER&&j==1){
                fprintf(f,"if(publish){for(unsigned j=0;j<%u;++j)d->pending[j]=(o->changed>>j)&1?o->pending[j]:o->data[j];}else{memcpy(o->pending,d->pending,%u);o->changed=UINT64_C(0x%016" PRIx64 ");}",o->depth,o->depth*8,rds_mask(o->depth));
                continue;
            }
            char view[64];if(j==2)strcpy(view,"validity");else snprintf(view,sizeof view,"o->%s",arrays[j]);
            fprintf(f,"if(publish)memcpy(d->%s,%s,%zu);else memcpy(%s,d->%s,%zu);",arrays[j],view,n*(j<2?8:1),view,arrays[j],n*(j<2?8:1));}}
        for(unsigned j=0;j<8;++j)if(object_field(o,j)){
            char view[64];if(j==4)strcpy(view,"(*occupancy)");else snprintf(view,sizeof view,"%s->%s",j==5?"p":"o",fields[j]);
            fprintf(f,"if(publish)d->%s=%s;else %s=d->%s;",fields[j],view,view,fields[j]);}
        fputs("if(publish){d->enqueue=p->enqueue;d->dequeue=p->dequeue;d->reset=p->reset;}else{p->enqueue=d->enqueue;p->dequeue=d->dequeue;p->reset=d->reset;}}\n",f);
    }
    fputs("}\n",f);
}
static void quoted(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\')
            fprintf(f, "\\%c", c);
        else if (c < 32 || c >= 127)
            fprintf(f, "\\%03o", c);
        else
            fputc(c, f);
    }
    fputc('"', f);
}
static void pointers(FILE *f, const rds_sim *s, const uint32_t *args, uint32_t n, uint32_t omit) {
    fputs("const uint64_t zero=0;const uint64_t*in[]={", f);
    for (uint32_t i = 0; i < n; ++i) {
        if (args[i] == RDS_NONE || i==omit)
            fputs("&zero", f);
        else
            rds_c_pointer(f, rds_c_ref(s, args[i]));
        fputc(',', f);
    }
    fputs("&zero};(void)in;\n", f);
}
/* Emit one statically bounded output-prefix match with packed input candidates. */
static void matcher(FILE *f, const rds_object *o, uint32_t columns, bool update, bool lifted) {
    /* No accepted output can change a priority. Preserve the pending snapshot
     * on this path; debug builds still validate every current priority. */
    if(update){
        if(lifted)fputs("o->changed=0;\n",f);
        fputs("#ifdef NDEBUG\nif(!p->reset&&!(",f);
        if(o->flags&PACKED_MATCHER)fprintf(f,"in[%u][0]",1+o->depth);
        else for(uint32_t col=0;col<columns;++col)
            fprintf(f,"%sin[%u][0]",col?"|":"",1+o->width*o->depth+col);
        if(lifted)fputs(")){}else\n#endif\n{\n",f);
        else fprintf(f,")){memcpy(o->pending,o->data,%u*8);}else\n#endif\n{\n",columns);
    }
    fputs("uint64_t taken=0,grant=0;(void)grant;\n",f);
    if(update&&(o->flags&MATCHER_GRANTS))fputs("(void)taken;\n",f);
    for(uint32_t col=0;col<columns;++col) {
        fputs("{uint64_t requests=",f);
        if(o->flags&PACKED_MATCHER)fprintf(f,"in[%u][0]",update?1+col:col);
        else for(uint32_t row=0;row<o->width;++row)
            fprintf(f,"%s((in[%u][0]&1)<<%u)",row?"|":"",update?1+row*o->depth+col:col*o->width+row,row);
        // Priorities start at zero and accepted grants wrap within 1..64 rows.
        // Masking the shift exposes that range without an extra bounds select;
        // retain the raw priority below for debug validation.
        if(update&&(o->flags&MATCHER_GRANTS)){
            fprintf(f,";uint64_t priority=o->data[%u];grant=requests;",col);
            fputs("\n#ifndef NDEBUG\nif(!p->reset&&(grant&(grant-1)))return cg_ofail(c,o->name,\"matcher grant must be one-hot or zero\");\n#endif\n",f);
        }else fprintf(f,";requests&=~taken;uint64_t priority=o->data[%u];uint64_t upper=requests&(UINT64_MAX<<(priority&63));uint64_t eligible=upper?upper:requests;grant=eligible&(~eligible+1);taken|=grant;",col);
        if(update){
            fprintf(f,"\n#ifndef NDEBUG\nif(!p->reset&&priority>=%u)return cg_ofail(c,o->name,\"matcher priority out of range\");\n#endif\nuint32_t index=grant?(uint32_t)__builtin_ctzll(grant):0;o->pending[%u]=p->reset?0:grant&&",o->width,col);
            if(o->flags&PACKED_MATCHER)fprintf(f,"((in[%u][0]>>%u)&1)",1+o->depth,col);
            else fprintf(f,"in[%u][0]",1+o->width*o->depth+col);
            fprintf(f,"?(index+1==%u?0:index+1):priority;",o->width);
            if(lifted)fprintf(f,"o->changed|=(uint64_t)(o->pending[%u]!=priority)<<%u;",col,col);
        }
        fputs("}\n",f);
    }
    if(!update)fputs("d[0]=grant;",f);
    else fputs("}\n",f);
}
void rds_c_query(FILE *f, const rds_sim *s, const rds_op *op) {
    uint32_t id = (uint32_t)s->imm[op->imm], query = (uint32_t)s->imm[op->imm + 1];
    const rds_object *o = &s->objects[id];
    fputs("{",f);rds_c_object_ref(f,s,id);fputs("uint64_t*d=",f);
    rds_c_pointer(f, rds_c_ref(s, op->out));
    fprintf(f, ";const uint32_t D=%u,W=%u,N=%u;(void)o;(void)D;(void)W;(void)N;\n", o->depth, o->words,
            s->values[op->out].words);
    pointers(f, s, s->args + op->args, op->nargs, RDS_NONE);
    switch (o->kind) {
    case TLB:
        if(query==3||query==4)fputs("rds_tlb_result lookup={in[0][0],in[0][1]};",f);
        else fputs("rds_tlb_result lookup=rds_tlb_lookup(o->data,D,in[0][0],in[1][0]!=0);",f);
        if(!query||query==5)fputs("d[0]=lookup.low;d[1]=lookup.metadata;",f);
        else{
            bool probe=query==2||query==4;unsigned base=query>=3?1:2;
            fputs("d[0]=rds_tlb_fault(lookup,",f);
            if(probe)fputs("0",f);else fprintf(f,"in[%u][0]",base);
            fprintf(f,",in[%u][0],in[%u][0]!=0,in[%u][0]!=0,%s);",base+!probe,base+!probe+1,base+!probe+2,probe?"true":"false");
        }
        break;
    case MATCHER:
        if(query<o->depth)matcher(f,o,query+1,false,false);
        else {
            fputs("uint64_t requests=",f);
            if(o->flags&PACKED_MATCHER)fputs("in[1][0]",f);
            else for(uint32_t row=0;row<o->width;++row)fprintf(f,"%s((in[%u][0]&1)<<%u)",row?"|":"",1+row,row);
            fprintf(f,";requests&=~in[0][0];uint64_t priority=o->data[%u];uint64_t upper=requests&(UINT64_MAX<<(priority&63));uint64_t eligible=upper?upper:requests;d[0]=eligible&(~eligible+1);",query-o->depth);
        }
        break;
    case FIFO:
        if (query == 0)
            fputs("d[0]=(*occupancy);", f);
        else if (query == 1)
            fprintf(f, "d[0]=(*occupancy)<D%s;", o->flags & PIPELINE ? "||in[0][0]" : "");
        else if (query == 2)
            fprintf(f, "d[0]=(*occupancy)>0%s;", o->flags & FLOW ? "||in[0][0]" : "");
        else if (!(o->flags & DROP))
            fprintf(f, "memcpy(d,%s,N*8);",
                    o->flags & FLOW ? (o->depth==1?"!(*occupancy)?in[0]:o->data":"!(*occupancy)?in[0]:o->data+(size_t)o->head*W")
                                    : (o->depth==1?"o->data":"o->data+(size_t)o->head*W"));
        else
            fputs("memset(d,0,N*8);", f);
        break;
    case PIPE:
        if (query == 1)
            fputs("bool ready=in[0][0];for(uint32_t i=D;i--;)ready=!validity[i]||ready;d[0]=ready;", f);
        else if (query == 2)
            fputs("d[0]=validity[D-1];", f);
        else if (!(o->flags & DROP))
            fputs("memcpy(d,o->data+(D-1)*W,N*8);", f);
        else
            fputs("memset(d,0,N*8);", f);
        break;
    case OFFER:
        if (query == 2)
            fputs("d[0]=(*occupancy);", f);
        else if (!(o->flags & DROP))
            fputs("memcpy(d,o->data,N*8);", f);
        else
            fputs("memset(d,0,N*8);", f);
        break;
    case BROADCAST:
        if (query == 0)
            fputs("d[0]=1;for(uint32_t i=0;i<D;++i)if(validity[i]&&!in[i][0]){d[0]=0;         break;}", f);
        else if (query == 1)
            fputs(o->flags & DROP ? "memset(d,0,N*8);" : "memcpy(d,o->data,N*8);", f);
        else
            fprintf(f, "d[0]=validity[%u];", query - 2);
        break;
    case SCOREBOARD:
        fputs("memcpy(d,o->data,N*8);", f);
        break;
    case CREDIT:
        fputs(query == 0 ? "d[0]=(*occupancy);" : query == 1 ? "d[0]=(*occupancy)==0;" : "d[0]=(*occupancy)==D;", f);
        break;
    case COUNTER:
        fputs(query == 0 ? "d[0]=(*occupancy);" : "d[0]=(*occupancy)==D-1&&in[0][0];", f);
        break;
    case HOST:
        fprintf(f, "d[0]=o->data[%u];", query);
        break;
    case ARBITER:
        if (query == 2 && (o->flags & DROP)) {
            fputs("memset(d,0,N*8);", f);
            break;
        }
        fputs("uint32_t sel=UINT32_MAX;for(uint32_t i=0,j=o->head;i<D;++i,j=j+1==D?0:j+1)if(in[j][0]){sel=j; "
              "        break;}",
              f);
        if (o->flags & PACKET)
            fputs("if((*occupancy))sel=o->tail;", f);
        if (query == 0)
            fputs("d[0]=sel==UINT32_MAX?0:sel;", f);
        else if (query == 1)
            fputs("d[0]=sel!=UINT32_MAX&&in[sel][0];", f);
        else if (query == 2)
            fputs("memcpy(d,in[D+(sel==UINT32_MAX?0:sel)],N*8);", f);
        else
            fprintf(f, "d[0]=sel==%u&&in[D][0];", query - 3);
        break;
    }
    fprintf(f, "\nd[N-1]&=UINT64_C(0x%016" PRIx64 ");}\n", rds_mask(s->values[op->out].width));
}
static void objects_range(FILE *f, const rds_sim *s, uint32_t lane, uint32_t phase, const bool *pending,
                          uint32_t first, uint32_t end, const uint32_t *prepare_at) {
    for (uint32_t id = first; id < end; ++id) {
        const rds_object *o = &s->objects[id];
        uint32_t owner = o->owner;
        if (owner != lane)
            continue;
        if(phase==1&&prepare_at&&prepare_at[id]!=RDS_NONE)continue;
        fputs("{",f);rds_c_object_ref(f,s,id);fprintf(f,"const uint32_t D=%u,W=%u;(void)D;(void)W;\n",o->depth,o->words);
        if (phase == 1) {
            pointers(f, s, o->inputs, o->ni, pending[id]?2:RDS_NONE);
            fputs("p->reset=in[0][0]!=0;p->enqueue=p->dequeue=false;", f);
            if(fifo_indices(o)||o->kind==ARBITER)fputs("o->next_head=o->head;o->next_tail=o->tail;",f);
            if(o->kind==FIFO||o->kind==OFFER||o->kind==CREDIT||o->kind==COUNTER||o->kind==ARBITER)
                fputs("p->next_count=(*occupancy);",f);
            fputc('\n',f);
            switch (o->kind) {
            case TLB:
                fputs("p->reset|=in[1][0]!=0;p->enqueue=in[2][0]&&!in[3][0];o->next_head=p->reset?0:p->enqueue?(o->head+1)&(D-1):o->head;if(p->enqueue){o->pending[0]=in[5][0]|(UINT64_C(1)<<53);o->pending[1]=in[4][0];}",f);
                break;
            case MATCHER: matcher(f,o,o->depth,true,s->lift_primitives); break;
            case FIFO:
                fprintf(f,
                        "bool valid=in[1][0],ready=in[3][0],empty=!(*occupancy);bool "
                        "accept=(*occupancy)<D%s;p->enqueue=valid&&accept%s;p->dequeue=!empty&&ready;",
                        o->flags & PIPELINE ? "||ready" : "", o->flags & FLOW ? "&&!(empty&&ready)" : "");
                fputs("p->next_count=p->reset?0:(*occupancy)+p->enqueue-p->dequeue;",f);
                if(fifo_indices(o))fputs("o->next_head=p->reset?0:p->"
                      "dequeue?(o->head+1==D?0:o->head+1):o->head;o->next_tail=p->reset?0:p->enqueue?(o->"
                      "tail+1==D?0:o->tail+1):o->tail;",
                      f);
                break;
            case PIPE:
                if(s->lift_primitives&&o->depth<=64){
                    fputs("uint64_t valid=0;",f);
                    for(uint32_t j=0;j<o->depth;++j)fprintf(f,"valid|=(uint64_t)validity[%u]<<%u;",j,j);
                    fprintf(f,"const uint64_t mask=UINT64_C(0x%016" PRIx64 ");uint64_t empty=~valid&mask;uint64_t ready=%s?mask:empty?UINT64_MAX>>__builtin_clzll(empty):0;",rds_mask(o->depth),o->flags&VALID_ONLY?"true":"in[3][0]!=0");
                    fputs("uint64_t moved=ready&((valid<<1)|(in[1][0]!=0));uint64_t next=p->reset?0:(valid&~ready)|moved;p->enqueue=(moved&1)!=0;",f);
                    for(uint32_t j=0;j<o->depth;++j)fprintf(f,"o->changes[%u]=((next>>%u)&1)|(((moved>>%u)&1)<<1);",j,j,j);
                    break;
                }
                fprintf(f,
                        "bool ready=%s;for(uint32_t j=D;j--;){ready=!validity[j]||ready;bool "
                        "valid=j?validity[j-1]:in[1][0]!=0;o->changes[j]=(unsigned "
                        "char)((ready&&valid?2:0)|(!p->reset&&(ready?valid:validity[j])?1:0));}p->enqueue=(o-"
                        ">changes[0]&2)!=0;",
                        o->flags & VALID_ONLY ? "true" : "in[3][0]!=0");
                break;
            case OFFER:
                fputs("p->enqueue=in[1][0]!=0;p->next_count=p->reset?0:p->enqueue?1:in[3][0]?0:(*occupancy);", f);
                break;
            case BROADCAST:
                if(s->lift_primitives&&o->depth<=64){
                    fputs("uint64_t valid=0,ready=0;",f);
                    for(uint32_t j=0;j<o->depth;++j)fprintf(f,"valid|=(uint64_t)validity[%u]<<%u;ready|=(uint64_t)(in[%u][0]!=0)<<%u;",j,j,3+j,j);
                    fprintf(f,"p->enqueue=!(valid&~ready)&&in[1][0];p->dequeue=(valid&ready)!=0;uint64_t next=p->reset?0:p->enqueue?UINT64_C(0x%016" PRIx64 "):valid&~ready;p->next_count=__builtin_popcountll(next);",rds_mask(o->depth));
                    for(uint32_t j=0;j<o->depth;++j)fprintf(f,"o->changes[%u]=(next>>%u)&1;",j,j);
                    break;
                }
                fputs("bool ready=true;for(uint32_t "
                      "j=0;j<D;++j)if(validity[j]&&!in[3+j][0])ready=false;p->enqueue=ready&&in[1][0];p->"
                      "next_count=0;for(uint32_t j=0;j<D;++j){bool "
                      "accepted=validity[j]&&in[3+j][0];p->dequeue|=accepted;o->changes[j]=!p->reset&&(p->"
                      "enqueue||(!accepted&&validity[j]));p->next_count+=o->changes[j];}",
                      f);
                break;
            case SCOREBOARD:
                fputs("bool "
                      "set=in[1][0],clear=in[3][0];o->set=in[2][0];o->clear=in[4][0];\n#ifndef NDEBUG\nif(!p->reset){if(set&&o-"
                      ">set>=D)return "
                      "cg_ofail(c,o->name,\"scoreboard_set_index_in_range\");if(clear&&o->clear>=D)return "
                      "cg_ofail(c,o->name,\"scoreboard_clear_index_in_range\");if(set&&(o->data[o->set/"
                      "64]>>(o->set%64)&1)&&!(clear&&o->clear==o->set))return "
                      "cg_ofail(c,o->name,\"scoreboard_set_free\");if(clear&&!(o->data[o->clear/"
                      "64]>>(o->clear%64)&1)&&!(set&&o->clear==o->set))return "
                      "cg_ofail(c,o->name,\"scoreboard_clear_busy\");}\n#endif\np->enqueue=set;p->dequeue=clear;",
                      f);
                if(s->lift_primitives&&o->words==1)
                    fputs("o->proposal=p->reset?0:(o->data[0]|(set?UINT64_C(1)<<(o->set&63):0))&~(clear?UINT64_C(1)<<(o->clear&63):0);",f);
                break;
            case CREDIT:
                fputs("bool "
                      "inc=in[1][0],dec=in[2][0];\n#ifndef NDEBUG\nif(!p->reset&&((dec&&!(*occupancy))||(inc&&!dec&&(*occupancy)==D)))"
                      "return cg_ofail(c,o->name,\"credit counter "
                      "underflow/"
                      "overflow\");\n#endif\np->enqueue=inc;p->dequeue=dec;p->next_count=p->reset?0:(*occupancy)+inc-dec;",
                      f);
                break;
            case COUNTER:
                fputs("p->next_count=p->reset?0:!in[1][0]?(*occupancy):(*occupancy)==D-1?0:(*occupancy)+1;", f);
                break;
            case ARBITER:
                if (o->flags & PIPELINE) {
                    if(s->lift_primitives&&o->depth<=64){
                        fputs("uint64_t requests=0;",f);
                        for(uint32_t j=0;j<o->depth;++j)fprintf(f,"requests|=(uint64_t)(in[%u][0]!=0)<<%u;",2+2*j,j);
                        fputs("uint64_t upper=o->head<64?requests&(UINT64_MAX<<o->head):0;uint64_t eligible=upper?upper:requests;uint32_t sel=eligible?(uint32_t)__builtin_ctzll(eligible):UINT32_MAX;",f);
                    }else
                    fputs("uint32_t sel=UINT32_MAX;for(uint32_t "
                          "i=0,k=o->head;i<D;++i,k=k+1==D?0:k+1)if(in[2+2*k][0]){sel=k;         break;}",
                          f);
                    if (o->flags & PACKET)
                        fputs("if((*occupancy))sel=o->tail;", f);
                    fputs("bool transfer=sel!=UINT32_MAX&&in[2+2*sel][0]&&in[1][0];p->dequeue=transfer;", f);
                    if (o->flags & PACKET)
                        fputs("bool "
                              "last=transfer&&in[2+2*D+sel][0];p->next_count=p->reset||last?0:transfer?1:(*occupancy);"
                              "o->next_tail=p->reset?0:transfer&&!(*occupancy)?sel:o->tail;o->next_head=p->"
                      "reset?0:last?(sel+1==D?0:sel+1):o->head;",
                              f);
                    else
                        fputs("o->next_head=p->reset?0:transfer?(sel+1==D?0:sel+1):o->head;", f);
                }
                break;
            case HOST:
                fputs("if(!o->host)return cg_ofail(c,o->name,\"host callback is not bound\");", f);
                break;
            }
            if ((o->kind <= OFFER || o->kind == BROADCAST) && o->pending && !pending[id]&&!arena_fifo_payload(s,id,pending))
                fputs("if(p->enqueue)memcpy(o->pending,in[2],W*8);", f);
        } else {
            if(s->schedule->cache_objects&&s->schedule->cache_objects[id]){
                /* Broadcast count is the number of pending recipients. Without
                 * enqueue, validity bits only clear, so any change lowers count. */
                if(s->schedule->count==1)fprintf(f,"h->payload_dirty_0[%u]|=",id);else fputs("if(",f);
                if(o->kind==MATCHER)fputs(s->lift_primitives?"o->changed!=0":"memcmp(o->data,o->pending,D*8)!=0",f);
                else if(valid_pipe_publication(s,o))fputs("p->enqueue||validity[0]",f);
                else if(o->kind==PIPE)fprintf(f,"!!(o->changes[%u]&2)||((o->changes[%u]&1)!=validity[%u])",o->depth-1,o->depth-1,o->depth-1);
                else if(fifo_indices(o))fputs("p->enqueue||o->next_head!=o->head||p->next_count!=(*occupancy)",f);
                else fputs("p->enqueue||p->next_count!=(*occupancy)",f);
                if(s->schedule->count==1)fputs(";\n",f);
                else {fputs("){",f);for(uint32_t lane=0;lane<s->schedule->count;++lane){
                    if(s->schedule->cache_lanes[(size_t)lane*s->nx+id])fprintf(f,"h->payload_dirty_%u[%u]=true;",lane,id);
                    }fputs("}\n",f);}
            }
            switch (o->kind) {
            case TLB:
                fputs("if(p->reset)memset(o->data,0,D*16);else if(p->enqueue)memcpy(o->data+2*o->head,o->pending,16);o->head=o->next_head;",f);
                break;
            case FIFO:
                if(arena_fifo_payload(s,id,pending)){
                    fputs("if(p->enqueue)memcpy(o->data,",f);
                    rds_c_pointer(f,rds_c_ref(s,o->inputs[2]));fputs(",W*8);",f);
                }else if (o->data)
                    fputs(fifo_indices(o)?"if(p->enqueue)memcpy(o->data+(size_t)o->tail*W,o->pending,W*8);":"if(p->enqueue)memcpy(o->data,o->pending,W*8);", f);
                break;
            case OFFER:
                if (o->data)
                    fputs("if(p->enqueue){memcpy(o->data,o->pending,W*8);}", f);
                break;
            case PIPE:
                if(valid_pipe_publication(s,o)){
                    /* A one-stage valid-only pipe always accepts its input.
                     * Reset clears validity but does not suppress payload copy. */
                    if(o->data)fputs("if(p->enqueue)memcpy(o->data,o->pending,W*8);",f);
                    fputs("validity[0]=p->enqueue&&!p->reset;",f);
                    break;
                }
                fputs("for(uint32_t j=D;j--;){", f);
                if (o->data)
                    fputs("if(o->changes[j]&2)memcpy(o->data+(size_t)j*W,j?o->data+(size_t)(j-1)*W:o->"
                          "pending,W*8);",
                          f);
                fputs("validity[j]=o->changes[j]&1;}", f);
                break;
            case BROADCAST:
                if (o->data)
                    fputs("if(p->enqueue){memcpy(o->data,o->pending,W*8);}", f);
                fputs("memcpy(validity,o->changes,D);", f);
                break;
            case SCOREBOARD:
                if(s->lift_primitives&&o->words==1){fputs("o->data[0]=o->proposal;",f);break;}
                fputs("if(p->reset)memset(o->data,0,W*8);else{if(p->enqueue)o->data[o->set/"
                      "64]|=UINT64_C(1)<<(o->set%64);if(p->dequeue)o->data[o->clear/"
                      "64]&=~(UINT64_C(1)<<(o->clear%64));}",
                      f);
                break;
            case MATCHER:
                if(s->lift_primitives){
                    fputs("uint64_t changed=o->changed;while(changed){unsigned j=__builtin_ctzll(changed);o->data[j]=o->pending[j];changed&=changed-1;}",f);
                    break;
                }
                /* The generic matcher and host store complete proposals. */
                fputs("memcpy(o->data,o->pending,D*8);",f);
                break;
            case HOST:
                fputs("memcpy(o->data,o->pending,D*8);", f);
                break;
            }
            if(fifo_indices(o)||o->kind==ARBITER)fputs("\no->head=o->next_head;o->tail=o->next_tail;",f);
            if(o->kind==FIFO||o->kind==OFFER||o->kind==CREDIT||o->kind==COUNTER||o->kind==ARBITER||o->kind==BROADCAST)
                fputs("\n(*occupancy)=p->next_count;",f);
        }
        fputs("}\n", f);
    }
}
static void value(FILE *f, const rds_sim *s, uint32_t id) {
    fputs("*(", f);
    rds_c_pointer(f, rds_c_ref(s, id));
    fputc(')', f);
}
bool rds_c_fifo_batchable(const rds_sim *s,uint32_t id){
    const rds_object *o=&s->objects[id];
    return s->flow_prepare&&s->schedule->count==1&&o->kind==FIFO&&o->depth==1&&
        !(o->flags&(PIPELINE|FLOW));
}
/* Consecutive one-entry queues have consecutive snapshot count bytes and
 * four-byte prepared records. All update inputs are pinned until publication,
 * so materialized payload copies can follow the group's latest producer.
 * Direct-to-pending producer stores retain their original sites. Lanes remain separate
 * hardware queues with their original current-state and publication semantics. */
void rds_c_prepare_fifo_batch(FILE *f,const rds_sim *s,uint32_t first,uint32_t count,const bool *pending){
    char view[64];rds_c_object_view(view,sizeof view,s,first,false);
    fprintf(f,"{/* FIFO control batch %u..%u */\n",first,first+count);
    fprintf(f,"_Static_assert(sizeof(prepared_%u)==4&&offsetof(object_state,p%u)-offsetof(object_state,p%u)==%u,\"FIFO control layout\");\n",first,first+count-1,first,4*(count-1));
    fprintf(f,"typedef uint8_t cv __attribute__((vector_size(%u)));cv occupied;memcpy(&occupied,&%s,%u);\n",count,view,count);
    fputs("const bool reset=",f);value(f,s,s->objects[first].inputs[0]);fputs("!=0;cv ones={",f);
    for(uint32_t i=0;i<count;++i)fprintf(f,"%s1",i?",":"");
    fputs("},resets={",f);
    for(uint32_t i=0;i<count;++i)fprintf(f,"%s(uint8_t)reset",i?",":"");
    fputs("};\ncv valid={",f);
    for(uint32_t i=0;i<count;++i){if(i)fputc(',',f);fputs("(uint8_t)(",f);value(f,s,s->objects[first+i].inputs[1]);fputs("!=0)",f);}
    fprintf(f,"};cv active=occupied|valid;if(!reset&&!memcmp(&active,&(cv){0},sizeof active)){memset((unsigned char*)h+offsetof(object_state,p%u),0,%u);}else{cv ready={",first,4*count);
    for(uint32_t i=0;i<count;++i){if(i)fputc(',',f);fputs("(uint8_t)(",f);value(f,s,s->objects[first+i].inputs[3]);fputs("!=0)",f);}
    fputs("};\n",f);
    fputs("cv enq=valid&(occupied^ones),deq=ready&occupied,next=(occupied|enq)&(deq^ones)&(resets^ones);\n",f);
    fprintf(f,"typedef uint8_t pv __attribute__((vector_size(%u)));pv ne=__builtin_shufflevector(next,enq",2*count);
    for(uint32_t i=0;i<count;++i)fprintf(f,",%u,%u",i,count+i);
    fputs("),dr=__builtin_shufflevector(deq,resets",f);
    for(uint32_t i=0;i<count;++i)fprintf(f,",%u,%u",i,count+i);
    fputs(");\n",f);
    fprintf(f,"typedef uint8_t rv __attribute__((vector_size(%u)));rv records=__builtin_shufflevector(ne,dr",4*count);
    for(uint32_t i=0;i<count;++i)fprintf(f,",%u,%u,%u,%u",2*i,2*i+1,2*count+2*i,2*count+2*i+1);
    fprintf(f,");memcpy((unsigned char*)h+offsetof(object_state,p%u),&records,%u);\n",first,4*count);
    for(uint32_t id=first;id<first+count;++id)
        if(s->objects[id].pending&&!pending[id]&&!arena_fifo_payload(s,id,pending))rds_c_prepare_fifo_payload(f,s,id);
    fputs("}}\n",f);
}
void rds_c_objects(FILE *f,const rds_sim *s,uint32_t lane,uint32_t phase,const bool *pending,const uint32_t *prepare_at){
    if(phase!=2||!s->flow_prepare||s->schedule->count!=1){
        objects_range(f,s,lane,phase,pending,0,s->nx,prepare_at);return;
    }
    for(uint32_t first=0;first<s->nx;){
        uint32_t count=0;
        while(count<4&&first+count<s->nx){const rds_object *o=&s->objects[first+count];
            if(o->kind!=FIFO||o->depth!=1||o->flags||o->owner!=lane)break;
            ++count;
        }
        if(count<4){objects_range(f,s,lane,phase,pending,first,first+1,prepare_at);++first;continue;}
        /* No enqueue, dequeue or reset means exact hold, including sticky
         * cache invalidations. Preserve every original publication on activity. */
        fprintf(f,"{/* FIFO idle publication guard %u..%u */\n",first,first+4);
        for(uint32_t j=0;j<4;++j)
            fprintf(f,"_Static_assert(sizeof(prepared_%u)==4&&offsetof(prepared_%u,enqueue)==1&&offsetof(prepared_%u,dequeue)==2&&offsetof(prepared_%u,reset)==3&&offsetof(object_state,p%u)-offsetof(object_state,p%u)==%u,\"FIFO publication control layout\");\n",first+j,first+j,first+j,first+j,first+j,first,4*j);
        fprintf(f,"if(cg_fifo_active4((const unsigned char*)h+offsetof(object_state,p%u))){\n",first);
        objects_range(f,s,lane,phase,pending,first,first+4,prepare_at);
        fputs("}}\n",f);first+=4;
    }
}
void rds_c_prepare_object(FILE *f,const rds_sim *s,uint32_t id,const bool *pending){
    objects_range(f,s,s->objects[id].owner,1,pending,id,id+1,NULL);
}
/* Bulk leaves the public simulator unevaluated. Queries observe current state;
 * ordinary stepping recomputes private proposals before they can be published.
 * Keep reset-edge enqueues and producer-to-pending sources exactly as before. */
static void transition_fifo(FILE *f,const rds_sim *s,uint32_t id,const bool *pending){
    const rds_object *o=&s->objects[id];
    fputs("{",f);rds_c_object_ref(f,s,id);fputs("(void)p;",f);
    pointers(f,s,o->inputs,o->ni,pending[id]?2:RDS_NONE);
    fprintf(f,"bool reset=in[0][0]!=0,valid=in[1][0]!=0,ready=in[3][0]!=0;bool enq=valid&((*occupancy)<%u),deq=ready&((*occupancy)!=0);%s count=reset?0:(*occupancy)+enq-deq;",o->depth,count_type(o));
    if(fifo_indices(o))fprintf(f,"unsigned head=reset?0:deq?(o->head+1==%u?0:o->head+1):o->head,tail=reset?0:enq?(o->tail+1==%u?0:o->tail+1):o->tail;",o->depth,o->depth);
    if(s->schedule->cache_objects&&s->schedule->cache_objects[id]){
        fprintf(f,"if(enq||count!=(*occupancy)%s){",fifo_indices(o)?"||head!=o->head":"");
        for(uint32_t lane=0;lane<s->schedule->count;++lane)
            if(s->schedule->cache_lanes[(size_t)lane*s->nx+id])fprintf(f,"h->payload_dirty_%u[%u]=true;",lane,id);
        fputs("}",f);
    }
    if(o->data)fprintf(f,"if(enq)memcpy(o->data%s,%s,%u);",fifo_indices(o)?"+(size_t)o->tail*W":"",pending[id]?"o->pending":"in[2]",o->words*8);
    if(fifo_indices(o))fputs("o->head=head;o->tail=tail;",f);
    fputs("(*occupancy)=count;}\n",f);
}
/* The release bulk capability proves unique ownership and infallibility.
 * Keep proposals prepared during evaluation: their sources may already be dead. */
void rds_c_transition_objects(FILE *f,const rds_sim *s,uint32_t lane,const bool *pending,const uint32_t *prepare_at){
    for(uint32_t id=0;id<s->nx;++id)if(s->objects[id].owner==lane){
        if(s->objects[id].kind==FIFO&&!s->objects[id].flags&&prepare_at[id]==RDS_NONE){
            fprintf(f,"{const unsigned W=%u;(void)W;",s->objects[id].words);
            transition_fifo(f,s,id,pending);fputs("}\n",f);continue;
        }
        objects_range(f,s,lane,1,pending,id,id+1,prepare_at);
        objects_range(f,s,lane,2,pending,id,id+1,NULL);
    }
}
void rds_c_prepare_fifo_payload(FILE *f,const rds_sim *s,uint32_t id){
    const rds_object *o=&s->objects[id];
    fputs("{",f);rds_c_object_ref(f,s,id);
    fputs("if(p->enqueue)memcpy(o->pending,",f);
    rds_c_pointer(f,rds_c_ref(s,o->inputs[2]));
    fprintf(f,",%u);}\n",o->words*8);
}
static void effects(FILE *f, const rds_sim *s, uint32_t phase, const bool *staged,bool bound_sources) {
    if (phase == 5) {
        for (uint32_t i = 0; i < s->nx; ++i)
            if (s->objects[i].kind == HOST) {
                fputs("{",f);rds_c_object_ref(f,s,i);fputs("uint64_t in[]={",f);
                for (uint32_t j = 0; j < 5; ++j) {
                    value(f, s, s->objects[i].inputs[j]);
                    fputc(',', f);
                }
                fprintf(f,
                        "};if(o->host(o->context,in,5,o->pending,%u))return cg_ofail(c,o->name,\"host callback "
                        "failed\");}\n",
                        s->objects[i].depth);
            }
        return;
    }
    if (phase == 4) {
        for (uint32_t i = 0; i < s->nw; ++i) {
            const rds_write *r = &s->writes[i];
            const rds_mem *m = &s->mems[r->mem];
            fputs("if(", f);
            value(f, s, r->enable);
            fprintf(f, "){uint64_t*d=c->memories[%u]+(", r->mem);
            value(f, s, r->address);
            uint32_t address_width=s->values[r->address].width;
            if(s->lift_primitives&&address_width<32&&(UINT64_C(1)<<address_width)<=m->depth)
                fprintf(f,"&UINT64_C(0x%016" PRIx64 ")",rds_mask(address_width));
            fprintf(f, ")*%u;const uint64_t*src=", m->words);
            if(bound_sources)fprintf(f,"write_src_%u",i);
            else rds_c_pointer(f, rds_c_ref(s, r->data));
            fputc(';', f);
            if (r->mask == RDS_NONE)
                fprintf(f, "memcpy(d,src,%u);", m->words * 8);
            else {
                fputs("const uint64_t*mask=", f);
                rds_c_pointer(f, rds_c_ref(s, r->mask));
                if (r->granule == m->width)
                    fprintf(f, ";if(mask[0]&1)memcpy(d,src,%u);",m->words*8);
                else if (r->granule == 8 && !(m->width%64)) {
                    /* Masked byte stores need no old-row load or merge. Bound
                     * the last chunk so neither source nor SRAM is overrun. */
                    fputc(';',f);
                    if(m->words>=8)fprintf(f,"for(uint32_t j=0;j<%u;++j){uint64_t en=mask[j];"
                                            "if(en)cg_byte_store(d+j*8,src+j*8,en,64);}",m->words/8);
                    if(m->words%8){
                        uint32_t word=m->words/8*8,bytes=m->words%8*8;
                        fprintf(f,"{uint64_t en=mask[%u]&UINT64_C(0x%016" PRIx64 ");"
                                  "if(en)cg_byte_store(d+%u,src+%u,en,%u);}",
                                word/8,rds_mask(bytes),word,word,bytes);
                    }
                } else if (!(r->granule%64))
                    fprintf(f,";for(uint32_t j=0;j<%u;++j)if(cg_bit(mask,j))memcpy(d+j*%u,src+j*%u,%u);",
                            m->width/r->granule,r->granule/64,r->granule/64,r->granule/8);
                else fprintf(f, ";for(uint32_t j=0;j<%u;++j)if(cg_bit(mask,j))cg_bits(d,j*%u,src,j*%u,%u);",
                             m->width / r->granule, r->granule, r->granule, r->granule);
            }
            fputs("}\n", f);
        }
        return;
    }
    fputs("\n#ifndef NDEBUG\n", f);
    for (uint32_t i = 0; i < s->nc; ++i) {
        const rds_assert *a = &s->checks[i];
        fputs("if(!", f);
        value(f, s, a->reset);
        fputs("&&", f);
        value(f, s, a->guard);
        fputs("&&!", f);
        value(f, s, a->condition);
        fputs("){snprintf(c->error,512,\"assertion at cycle %llu: %s\",(unsigned long long)c->cycle,", f);
        quoted(f, a->name);
        fputs(");return -1;}\n", f);
    }
    for (uint32_t i = 0; i < s->nw; ++i) {
        const rds_write *r = &s->writes[i];
        fputs("if(", f);
        value(f, s, r->enable);
        fputs("){uint64_t ix;if(!cg_index(", f);
        rds_c_pointer(f, rds_c_ref(s, r->address));
        fprintf(f, ",%u,%u,&ix))return cg_fail(c,\"memory write address out of range\");",
                s->values[r->address].words, s->mems[r->mem].depth);
        for (uint32_t j = 0; j < i; ++j)
            if (s->writes[j].mem == r->mem) {
                fputs("if(", f);
                value(f, s, s->writes[j].enable);
                fputs("&&", f);
                value(f, s, s->writes[j].address);
                fputs("==ix)return cg_fail(c,\"memory write collision\");", f);
            }
        fputs("}\n", f);
    }
    fputs("\n#endif\n", f);
    rds_c_registers(f,s,RDS_NONE,staged);
    for (uint32_t i = 0; i < s->ns; ++i) {
        const rds_read *r = &s->reads[i];
        const rds_mem *m = &s->mems[r->mem];
        fputs("{const uint64_t*src=", f);
        rds_c_pointer(f, rds_c_ref(s, r->q));
        fputs(";if(", f);
        value(f, s, r->enable);
        if (r->write != RDS_NONE) {
            fputs("&&!", f);
            value(f, s, r->write);
        }
        fputs("){uint64_t ix=", f);
        value(f, s, r->address);
        fputs(";\n#ifndef NDEBUG\nif(!cg_index(", f);
        rds_c_pointer(f, rds_c_ref(s, r->address));
        fprintf(f, ",%u,%u,&ix))return cg_fail(c,\"synchronous read address out of range\");\n",
                s->values[r->address].words, m->depth);
        for (uint32_t j = 0; j < s->nw; ++j)
            if (s->writes[j].mem == r->mem) {
                fputs("if(", f);
                value(f, s, s->writes[j].enable);
                fputs("&&", f);
                value(f, s, s->writes[j].address);
                fputs("==ix)return cg_fail(c,\"synchronous memory read/write collision\");", f);
            }
        fputs("\n#endif\n", f);
        fprintf(f, "src=c->memories[%u]+ix*%u;}memcpy(n+%zu,src,%u);}\n", r->mem, m->words, r->next,
                m->words * 8);
    }
}

/* Preserve the complete old base directly in the next bank, then overwrite only
 * the selected element. No full updated-vector scratch value is materialized. */
void rds_c_update_next(FILE *f,const rds_sim *s,const rds_op *op,size_t next) {
    const uint32_t *a=s->args+op->args;
    uint32_t element=(uint32_t)s->imm[op->imm+1], width=s->values[op->out].width;
    fprintf(f,"{uint64_t*d=n+%zu;memcpy(d,",next);
    rds_c_pointer(f,rds_c_ref(s,a[0]));fprintf(f,",%u);if(",s->values[op->out].words*8);
    value(f,s,a[1]);fputs("){uint64_t ix=",f);value(f,s,a[2]);
    fputs(";\n#ifndef NDEBUG\nif(!cg_index(",f);rds_c_pointer(f,rds_c_ref(s,a[2]));
    fprintf(f,",%u,%" PRIu64 ",&ix)){if(c->strict)return cg_fail(c,\"vector write index out of range\");memset(d,0,%u);}else\n#endif\n{const uint64_t*src=",
            s->values[a[2]].words,s->imm[op->imm],s->values[op->out].words*8);
    rds_c_pointer(f,rds_c_ref(s,a[3]));fputc(';',f);
    if(!(element%64))fprintf(f,"memcpy(d+ix*%u,src,%u);",element/64,element/8);
    else if(element==1)fputs("uint64_t bit=UINT64_C(1)<<(ix%64);d[ix/64]=(d[ix/64]&~bit)|((src[0]&1)<<(ix%64));",f);
    else fprintf(f,"cg_bits(d,(uint32_t)(ix*%u),src,0,%u);",element,element);
    fprintf(f,"}}d[%u]&=UINT64_C(0x%016" PRIx64 ");}\n",s->values[op->out].words-1,rds_mask(width));
}
void rds_c_effects(FILE *f,const rds_sim *s,uint32_t phase,const bool *staged){effects(f,s,phase,staged,false);}
void rds_c_bound_effects(FILE *f,const rds_sim *s){
    if(!s->nw)return;
    fputs("static inline int rds_generated_publish_bound(rds_compiled_context*c",f);
    for(uint32_t i=0;i<s->nw;++i)fprintf(f,",const uint64_t*write_src_%u",i);
    fputs("){uint64_t*restrict v=c->v,*restrict n=c->next;const uint64_t*restrict q=c->q;object_state*restrict h=c->hot;(void)v;(void)n;(void)q;(void)h;\n",f);
    effects(f,s,4,NULL,true);
    fputs("return 0;}\n",f);
}
void rds_c_registers(FILE *f,const rds_sim *s,uint32_t owner,const bool *staged){
    for (uint32_t i = 0; i < s->nr; ++i) {
        const rds_reg *r = &s->regs[i];
        if(r->owner!=owner)continue;
        if(staged[i]) {
            if(r->reset!=RDS_NONE){fputs("if(",f);value(f,s,r->reset);fprintf(f,")memcpy(n+%zu,",r->next);
                rds_c_pointer(f,rds_c_ref(s,r->reset_value));fprintf(f,",%u);\n",s->values[r->q].words*8);}
            continue;
        }
        fprintf(f, "memcpy(n+%zu,", r->next);
        if (r->reset != RDS_NONE) {
            value(f, s, r->reset);
            fputc('?', f);
            rds_c_pointer(f, rds_c_ref(s, r->reset_value));
            fputc(':', f);
        }
        rds_c_pointer(f, rds_c_ref(s, r->d));
        fprintf(f, ",%u);\n", s->values[r->q].words * 8);
    }
}
