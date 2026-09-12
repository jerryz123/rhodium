/* Emits statically bound C operations without an execution-time opcode interpreter. */
#define _POSIX_C_SOURCE 200809L
#include "codegen.h"
#include "alu.h"
#include "tlb.h"
#include <inttypes.h>
/* Read-only payload views can be selected in native storage. Ring selection
 * reads only the chosen head; bypassing FIFOs retain their ordinary query. */
static bool payload_storage(const rds_sim *s,uint32_t value,uint32_t *object,uint32_t *word){
    const rds_op *query=NULL;
    uint32_t bits=s->values[value].width;uint64_t low=0;
    for(unsigned fuel=0;fuel<32;++fuel){
        query=NULL;
        for(uint32_t i=0;i<s->no;++i)if(s->ops[i].out==value){query=&s->ops[i];break;}
        if(!query)return false;
        if(query->code==RDS_COPY)value=s->args[query->args];
        else if(query->code==RDS_SLICE){low+=s->imm[query->imm];value=s->args[query->args];}
        else break;
    }
    if(!query||query->code!=RDS_OBJECT_QUERY||query->nargs)return false;
    uint32_t id=(uint32_t)s->imm[query->imm],kind=(uint32_t)s->imm[query->imm+1];
    const rds_object *o=&s->objects[id];
    if(o->flags&1)return false;
    bool fixed=(o->kind==1&&kind==3&&!(o->flags&4))||
               (o->kind==2&&kind==3)||(o->kind==3&&kind==3)||
               (o->kind==9&&kind==1);
    if(!fixed)return false;
    /* Direct ring reads improve one worker but currently regress parallel
     * locality. Keep the measured arena publication path for multiple owners. */
    if(o->kind==1&&o->depth>1&&s->schedule->count>1)return false;
    if(low%64||low+bits>o->width)return false;
    *object=id;*word=(o->kind==2?(o->depth-1)*o->words:0)+(uint32_t)(low/64);return true;
}
static bool ring_payload(const rds_sim*s,uint32_t object){return s->objects[object].kind==1&&s->objects[object].depth>1;}
static unsigned head_bytes(const rds_object*o){return o->depth<256?1:o->depth<65536?2:4;}
static void ring_payload_selection(FILE*f,const rds_sim*s,const rds_op*op){
    uint32_t first=0,word=0;
    (void)payload_storage(s,s->args[op->args+1],&first,&word);
    unsigned bytes=head_bytes(&s->objects[first]),stride=s->objects[first].words;
    bool uniform=ring_payload(s,first);
    for(uint32_t i=2;i<op->nargs;++i){uint32_t id=0;
        (void)payload_storage(s,s->args[op->args+i],&id,&word);
        uniform&=ring_payload(s,id)&&head_bytes(&s->objects[id])==bytes&&s->objects[id].words==stride;
    }
    fputs("unsigned ix=__builtin_ctzll(sel);",f);
    if(uniform){
        fputs("static const struct{size_t data,head;}refs[]={",f);
        for(uint32_t i=1;i<op->nargs;++i){uint32_t id=0;
            (void)payload_storage(s,s->args[op->args+i],&id,&word);
            fprintf(f,"{offsetof(object_state,o%u.data)+%u,offsetof(object_state,o%u.head)},",id,word*8,id);
        }
        fprintf(f,"};const unsigned char*base=(const unsigned char*)h;uint%u_t head;memcpy(&head,base+refs[ix].head,sizeof head);memcpy(d,base+refs[ix].data+(size_t)head*%u,n*8);",bytes*8,stride*8);
    }else{
        fputs("switch(ix){",f);
        for(uint32_t i=1;i<op->nargs;++i){uint32_t id=0;
            (void)payload_storage(s,s->args[op->args+i],&id,&word);
            fprintf(f,"case %u:memcpy(d,h->o%u.data+%u",i-1,id,word);
            if(ring_payload(s,id))fprintf(f,"+(size_t)h->o%u.head*%u",id,s->objects[id].words);
            fputs(",n*8);break;",f);
        }
        fputs("}",f);
    }
}
/* Select an address through a bounded balanced tree, then copy one payload. */
static void payload_address_tree(FILE *f,const rds_sim *s,const rds_op *op,uint32_t first,uint32_t end){
    if(end-first==1){uint32_t object=0,word=0;
        (void)payload_storage(s,s->args[op->args+1+first],&object,&word);
        fprintf(f,"offsetof(object_state,o%u.data)+%u",object,word*8);return;}
    uint32_t middle=first+(end-first)/2;
    fprintf(f,"(ix<%u?",middle);payload_address_tree(f,s,op,first,middle);
    fputc(':',f);payload_address_tree(f,s,op,middle,end);fputc(')',f);
}
bool rds_c_onehot_storage(const rds_sim *s,const rds_op *op){
    if(s->emit_private)return false; /* Private operands can be immutable offer snapshots. */
    if(op->code!=RDS_ONEHOT||s->values[s->args[op->args]].width>64)return false;
    for(uint32_t i=1;i<op->nargs;++i){uint32_t object,word;
        if(!payload_storage(s,s->args[op->args+i],&object,&word))return false;}
    return true;
}
uint32_t rds_c_arena_word(const rds_sim *s,uint32_t word){
    if(s->emit_used)s->emit_used[word]=true;
    return s->emit_offsets?s->emit_offsets[word]:word;
}
bool rds_c_small_word(const rds_sim *s,uint32_t word){
    return s->emit_offsets&&s->emit_small&&s->emit_small[word];
}
bool rds_c_small_value(const rds_sim *s,uint32_t id){
    return s->values[id].state==SIZE_MAX&&rds_c_small_word(s,(uint32_t)s->values[id].offset);
}
void rds_c_store_small(FILE *f,const rds_sim *s,uint32_t id){
    if(rds_c_small_value(s,id))fprintf(f,"((uint8_t*)v)[%u]=(uint8_t)small_result;",rds_c_arena_word(s,(uint32_t)s->values[id].offset));
}
uint64_t rds_c_ref(const rds_sim *s, uint32_t id) {
    if(s->emit_private)return UINT64_C(0x2000000000000000)|((uint64_t)id<<32);
    if(s->emit_destination&&*s->emit_destination==id)return UINT64_MAX;
    const rds_value *v = &s->values[id];
    if(v->state!=SIZE_MAX)return UINT64_C(0x8000000000000000)|v->state;
    if(s->emit_constants&&s->emit_constants[v->offset]!=RDS_NONE)
        return UINT64_C(0x4000000000000000)|s->emit_constants[v->offset];
    /* Pointer consumers can access the entire logical value, so reserve every word. */
    for(uint32_t j=0;j<v->words;++j)(void)rds_c_arena_word(s,(uint32_t)v->offset+j);
    /* Byte-view references retain a sub-byte IR width in bits 32..61;
     * the low 32 bits are the arena offset. Zero width denotes a full byte.
     * raw_ref excludes these temporary views from indexed pointer tables. */
    if(rds_c_small_value(s,id))return UINT64_C(0xc000000000000000)|
        ((s->lift_primitives&&v->width<8?(uint64_t)v->width:0)<<32)|rds_c_arena_word(s,(uint32_t)v->offset);
    return rds_c_arena_word(s,(uint32_t)v->offset);
}
/* Runtime-indexed pointer tables need actual uint64_t storage, not temporary
 * scalar views. Record that constraint in the first emission pass. */
static uint64_t raw_ref(const rds_sim *s,uint32_t id){
    if(s->emit_small)s->emit_small[s->values[id].offset]=false;
    return rds_c_ref(s,id);
}
void rds_c_pointer(FILE *f, uint64_t ref) {
    if(ref==UINT64_MAX){fputs("(&small_result)",f);return;}
    if(ref>>62==3){
        uint32_t width=(uint32_t)(ref>>32)&UINT32_C(0x3fffffff);
        fprintf(f,"((const uint64_t[]){((uint8_t*)v)[%" PRIu32 "]",(uint32_t)ref);
        if(width)fprintf(f,"&UINT64_C(0x%016" PRIx64 ")",rds_mask(width));
        fputs("})",f);return;
    }
    if((ref>>61)==1){fprintf(f,"cv%u+%u",(uint32_t)(ref>>32)&0x1fffffffu,(uint32_t)ref);return;}
    fprintf(f, "%s+%" PRIu64, ref >> 63 ? "q" : ref >> 62 ? "rds_constants" : "v", ref & UINT64_C(0x3fffffffffffffff));
}
void rds_c_prelude(FILE *f, const rds_sim *s) {
    fputs("/* Executes compiled Rhodium operations and snapshot state transitions. */\n"
          "#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#include <string.h>\n#include "
          "<stdio.h>\n"
          "#if defined(__x86_64__)\n#include <immintrin.h>\n#endif\n"
          "typedef int (*rds_host_fn)(void*,const uint64_t*,size_t,uint64_t*,size_t);\n",
          f);
#define FIELD(type, name) #type " " #name ";\n"
    fputs("typedef struct {\n" RDS_OBJECT_FIELDS(FIELD) "} rds_object;\n", f);
    fputs("typedef struct {\n" RDS_CONTEXT_FIELDS(FIELD) "} rds_compiled_context;\n", f);
#undef FIELD
    fputs("static const uint64_t rds_constants[]={",f);
    for(uint32_t i=0;i<s->schedule->constant_count;++i){const constant_span *v=&s->schedule->constants[i];
        for(uint32_t j=0;j<v->words;++j)fprintf(f,"UINT64_C(0x%016" PRIx64 "),",s->arena[v->offset+j]);}
    fputs("0};\n",f);
    /* Common operation/layout helpers include functions absent from some
     * models. The emitter restores diagnostics before executable regions. */
    fputs("#if defined(__GNUC__) || defined(__clang__)\n"
          "#pragma GCC diagnostic push\n"
          "#pragma GCC diagnostic ignored \"-Wunused-function\"\n#endif\n", f);
    fputs("static inline int cg_fail(rds_compiled_context*c,const "
          "char*m){snprintf(c->error,512,\"%s\",m);return -1;}\n"
          "static inline int cg_ofail(rds_compiled_context*c,const char*name,const "
          "char*m){snprintf(c->error,512,\"%s: %s\",name,m);return -1;}\n"
          "static inline const uint64_t*cg_ptr(rds_compiled_context*c,uint64_t r){return "
          "r>>63?c->q+(r&INT64_MAX):r>>62?rds_constants+(r&UINT64_C(0x3fffffffffffffff)):c->v+r;}\n"
          "static inline uint64_t cg_mask(uint32_t w){return w%64?(UINT64_C(1)<<(w%64))-1:UINT64_MAX;}\n"
          "static inline int cg_bit(const uint64_t*p,uint32_t b){return (p[b/64]>>(b%64))&1;}\n"
          "static inline bool cg_onehot_bad(uint64_t x){\n"
          "#if defined(__x86_64__) && defined(__BMI__) && defined(__GCC_ASM_FLAG_OUTPUTS__)\n"
          "uint64_t rest;bool zero,nonzero;\n"
          "/* BLSR sets CF for a zero input and clears ZF for multiple set bits. */\n"
          "__asm__(\"blsr %3,%0\":\"=r\"(rest),\"=@ccc\"(zero),\"=@ccnz\"(nonzero):\"r\"(x));\n"
          "return zero||nonzero;\n#else\nreturn !x||(x&(x-1));\n#endif\n}\n"
          "static inline void cg_put(uint64_t*p,uint32_t b){p[b/64]|=UINT64_C(1)<<(b%64);}\n"
          "static inline int cg_cmp(const uint64_t*a,const uint64_t*b,uint32_t "
          "n){while(n--)if(a[n]!=b[n])return a[n]<b[n]?-1:1;return 0;}\n"
          "static inline bool cg_index(const uint64_t*p,uint32_t n,uint64_t limit,uint64_t*x){for(uint32_t "
          "i=1;i<n;++i)if(p[i])return false;*x=p[0];return *x<limit;}\n"
          "static inline void cg_bits(uint64_t*d,uint32_t db,const uint64_t*s,uint32_t sb,uint32_t "
          "w){while(w){uint32_t n=64-db%64;if(n>w)n=w;uint64_t "
          "x=s[sb/64]>>(sb%64);if(sb%64&&n>64-sb%64)x|=s[sb/64+1]<<(64-sb%64);uint64_t "
          "m=cg_mask(n);d[db/64]=(d[db/64]&~(m<<(db%64)))|((x&m)<<(db%64));db+=n;sb+=n;w-=n;}}\n",
          f);
#define STRING_INNER(...) #__VA_ARGS__
#define STRING(...) STRING_INNER(__VA_ARGS__)
    fputs("static inline uint64_t cg_byte_merge(uint64_t a,uint64_t b,uint64_t x){"
          "\n#if defined(__x86_64__) && defined(__BMI2__)\n"
          "x=_pdep_u64(x,UINT64_C(0x0101010101010101));\n#else\n"
          "x=(x|(x<<28))&UINT64_C(0x0000000f0000000f);"
          "x=(x|(x<<14))&UINT64_C(0x0003000300030003);"
          "x=(x|(x<<7))&UINT64_C(0x0101010101010101);\n#endif\n"
          "x*=255;return (a&~x)|(b&x);}\n",f);
    fputs("static inline void cg_byte_store(uint64_t*d,const uint64_t*s,uint64_t mask,unsigned bytes){\n"
          "#if defined(__x86_64__) && defined(__AVX512BW__) && defined(__AVX512VL__)\n"
          "if(bytes<=16){__m128i x=_mm_maskz_loadu_epi8((__mmask16)mask,s);"
          "_mm_mask_storeu_epi8(d,(__mmask16)mask,x);}"
          "else if(bytes<=32){__m256i x=_mm256_maskz_loadu_epi8((__mmask32)mask,s);"
          "_mm256_mask_storeu_epi8(d,(__mmask32)mask,x);}"
          "else{__m512i x=_mm512_maskz_loadu_epi8((__mmask64)mask,s);"
          "_mm512_mask_storeu_epi8(d,(__mmask64)mask,x);}\n#else\n"
          "for(unsigned j=0;j<bytes/8;++j){uint64_t en=(mask>>(j*8))&255;"
          "if(en)d[j]=cg_byte_merge(d[j],s[j],en);}\n#endif\n}\n",f);
    fputs(STRING(RDS_ALU_CODE) "\n",f);
    fputs(STRING(RDS_TLB_CODE) "\n",f);
#undef STRING
#undef STRING_INNER
}
/* Share small masked decoders across instances and replicated worker cones. */
static bool table_decode(const rds_sim *s,const rds_op *o) {
    return !s->linear_decode && o->code==RDS_DECODE && s->values[s->args[o->args]].width<=8 && s->values[o->out].width<=64;
}
static bool tree_decode(const rds_sim *s,const rds_op *o) {
    return !s->linear_decode && o->code==RDS_DECODE && !table_decode(s,o) && s->values[s->args[o->args]].width<=64;
}
static uint32_t decode_table_id(const rds_sim *s,const rds_op *o) {
    uint32_t id=(uint32_t)(o-s->ops);
    for(uint32_t i=0;i<id;++i) {
        const rds_op *p=&s->ops[i];
        if((table_decode(s,p)||tree_decode(s,p)) && s->values[s->args[p->args]].width==s->values[s->args[o->args]].width && s->values[p->out].width==s->values[o->out].width && p->nimm==o->nimm && !memcmp(s->imm+p->imm,s->imm+o->imm,o->nimm*sizeof *s->imm))return i;
    }
    return id;
}
uint32_t rds_c_decode_table_id(const rds_sim *s,const rds_op *o) {
    return table_decode(s,o)?decode_table_id(s,o):RDS_NONE;
}
/* A Boolean decode over at most six input bits fits in an immediate word. */
bool rds_c_decode_bits(const rds_sim *s,const rds_op *o,uint64_t *bits) {
    if(s->linear_decode || o->code!=RDS_DECODE || s->values[o->out].width!=1 || s->values[s->args[o->args]].width>6)return false;
    unsigned count=1u<<s->values[s->args[o->args]].width;const uint64_t *im=s->imm+o->imm;
    *bits=0;
    for(unsigned key=0;key<count;++key){uint64_t value=im[1];
        for(uint64_t row=0;row<im[0];++row)if((key&im[3+row*3])==im[2+row*3]){value=im[4+row*3];break;}
        *bits|=(value&1)<<key;}
    return true;
}
/* Only split on bits constrained by every remaining row. This introduces no
 * duplication, keeps row priority, and bounds source growth by the input table. */
static void decode_tree(FILE *f,const uint64_t *rows,uint32_t stride,const uint32_t *ids,uint32_t count,uint64_t tested) {
    uint64_t common=~tested;
    for(uint32_t i=0;i<count;++i)common&=rows[(size_t)ids[i]*stride+1];
    if(count>1 && common) {
        fprintf(f,"switch(x&UINT64_C(0x%" PRIx64 ")){\n",common);
        uint32_t *group=malloc((size_t)count*sizeof *group);
        if(!group) { fputs("#error decode allocation failed\n",f);return; }
        for(uint32_t i=0;i<count;++i) {
            uint64_t key=rows[(size_t)ids[i]*stride]&common;
            bool seen=false;
            for(uint32_t j=0;j<i;++j)if((rows[(size_t)ids[j]*stride]&common)==key){seen=true;break;}
            if(seen)continue;
            uint32_t n=0;
            for(uint32_t j=i;j<count;++j)if((rows[(size_t)ids[j]*stride]&common)==key)group[n++]=ids[j];
            fprintf(f,"case UINT64_C(0x%" PRIx64 "):\n",key);
            decode_tree(f,rows,stride,group,n,tested|common);
            fputs("break;\n",f);
        }
        free(group);fputs("}\n",f);
    } else for(uint32_t i=0;i<count;++i) {
        const uint64_t *row=rows+(size_t)ids[i]*stride;
        fprintf(f,"if((x&UINT64_C(0x%" PRIx64 "))==UINT64_C(0x%" PRIx64 "))return %u;\n",row[1]&~tested,row[0]&~tested,ids[i]+1);
    }
}
void rds_c_decode_tables(FILE *f,const rds_sim *s) {
    for(uint32_t i=0;i<s->no;++i) {
        const rds_op *o=&s->ops[i];
        if((!table_decode(s,o)&&!tree_decode(s,o))||decode_table_id(s,o)!=i)continue;
        uint64_t membership_bits;
        if(rds_c_decode_bits(s,o,&membership_bits))continue;
        if(tree_decode(s,o)) {
            const uint64_t *im=s->imm+o->imm;
            uint32_t n=s->values[o->out].words,count=(uint32_t)im[0];
            uint32_t *ids=malloc((count?count:1)*sizeof *ids);
            if(!ids){fputs("#error decode allocation failed\n",f);continue;}
            for(uint32_t j=0;j<count;++j)ids[j]=j;
            fprintf(f,"static inline uint32_t decode_tree_%u(uint64_t x){\n",i);
            decode_tree(f,im+1+n,2+n,ids,count,0);
            fputs("return 0;}\n",f);free(ids);
            fprintf(f,"static const uint64_t decode_values_%u[%u][%u]={",i,count+1,n);
            for(uint32_t j=0;j<=count;++j) {
                const uint64_t *value=j?im+1+n+(size_t)(j-1)*(2+n)+2:im+1;
                fputc('{',f);for(uint32_t k=0;k<n;++k)fprintf(f,"UINT64_C(0x%" PRIx64 "),",value[k]&(k+1==n?rds_mask(s->values[o->out].width):UINT64_MAX));fputs("},",f);
            }
            fputs("};\n",f);continue;
        }
        unsigned width=s->values[o->out].width,size=width<=8?8:width<=16?16:width<=32?32:64;
        unsigned count=1u<<s->values[s->args[o->args]].width;
        const uint64_t *im=s->imm+o->imm;
        fprintf(f,"static const uint%u_t decode_%u[%u]={",size,i,count);
        for(unsigned key=0;key<count;++key) {
            uint64_t value=im[1];
            for(uint64_t row=0;row<im[0];++row)if((key&im[2+row*3+1])==im[2+row*3]){value=im[2+row*3+2];break;}
            if(width<64)value&=(UINT64_C(1)<<width)-1;
            fprintf(f,"UINT64_C(0x%" PRIx64 "),",value);
        }
        fputs("};\n",f);
    }
}
/* Bind selector tables once. Dense scalar keys use direct indexing; sparse keys
 * retain first-match semantics, including wide selectors and default selection. */
static void mux(FILE *f, const rds_sim *s, const rds_op *o) {
    const uint32_t *args = s->args + o->args;
    uint32_t an = s->values[args[0]].words, n = s->values[o->out].words;
    const uint64_t *im = s->imm + o->imm;
    bool dense = an == 1;
    for (uint32_t i = 0; i < o->nargs - 2; ++i)
        if (im[i] != i)
            dense = false;
    fputs("{const uint64_t*sel=", f);
    rds_c_pointer(f, rds_c_ref(s, args[0]));
    fputs(";uint32_t k=0;\n", f);
    if (dense)
        fprintf(f, "if(sel[0]<%u)k=(uint32_t)sel[0]+1;\n", o->nargs - 2);
    else {
        fputs("static const uint64_t keys[]={", f);
        for (uint32_t i = 0; i < o->nimm; ++i)
            fprintf(f, "UINT64_C(0x%016" PRIx64 "),", im[i]);
        fputs("0};\n", f);
        fprintf(f, "for(uint32_t j=0;j<%u;++j)if(!cg_cmp(sel,keys+j*%u,%u)){k=j+1;         break;}\n",
                o->nargs - 2, an, an);
    }
    if(s->emit_private){
        fputs("const uint64_t*refs[]={",f);
        for(uint32_t i=1;i<o->nargs;++i){rds_c_pointer(f,rds_c_ref(s,args[i]));fputc(',',f);}
        fputs("};memcpy(",f);rds_c_pointer(f,rds_c_ref(s,o->out));
        fprintf(f,",refs[k],%u);}\n",n*8);return;
    }
    fputs("static const uint64_t refs[]={", f);
    for (uint32_t i = 1; i < o->nargs; ++i)
        fprintf(f, "UINT64_C(0x%016" PRIx64 "),", raw_ref(s, args[i]));
    fputs("};memcpy(", f);
    rds_c_pointer(f, rds_c_ref(s, o->out));
    fprintf(f, ",cg_ptr(c,refs[k]),%u);}\n", n * 8);
}
static void operation_body(FILE *f, const rds_sim *s, const rds_op *o) {
    uint64_t membership_bits;
    if(rds_c_decode_bits(s,o,&membership_bits)) {
        fputc('(',f);rds_c_pointer(f,rds_c_ref(s,o->out));
        fprintf(f,")[0]=(UINT64_C(0x%" PRIx64 ")>>((",membership_bits);
        rds_c_pointer(f,rds_c_ref(s,s->args[o->args]));fputs(")[0]&63))&1;\n",f);return;
    }
    if(tree_decode(s,o)) {
        uint32_t id=decode_table_id(s,o);
        fputs("{uint32_t ix=decode_tree_",f);fprintf(f,"%u((",id);
        rds_c_pointer(f,rds_c_ref(s,s->args[o->args]));fputs(")[0]);memcpy(",f);
        rds_c_pointer(f,rds_c_ref(s,o->out));
        fprintf(f,",decode_values_%u[ix],%u);}\n",id,s->values[o->out].words*8);return;
    }
    if(table_decode(s,o)) {
        fputc('(',f);rds_c_pointer(f,rds_c_ref(s,o->out));
        fprintf(f,")[0]=decode_%u[(",decode_table_id(s,o));
        rds_c_pointer(f,rds_c_ref(s,s->args[o->args]));
        fprintf(f,")[0]&%u];\n",(1u<<s->values[s->args[o->args]].width)-1);return;
    }
    if (o->code == RDS_ALU || o->code == RDS_BYTE_MERGE) {
        fputc('(',f);rds_c_pointer(f,rds_c_ref(s,o->out));
        if(o->code==RDS_ALU)fprintf(f,")[0]=rds_alu(%u,",s->values[o->out].width);
        else fputs(")[0]=cg_byte_merge(",f);
        for(unsigned i=0;i<3;++i){if(i)fputc(',',f);fputc('(',f);rds_c_pointer(f,rds_c_ref(s,s->args[o->args+i]));fputs(")[0]",f);}
        fputs(");\n",f);return;
    }
    if (o->code == RDS_OBJECT_QUERY) {
        rds_c_query(f, s, o);
        return;
    }
    if (o->code == RDS_MUX) {
        mux(f, s, o);
        return;
    }
    const uint32_t *args = s->args + o->args;
    uint32_t w = s->values[o->out].width, n = s->values[o->out].words;
    fputs("{uint64_t*d=",f);rds_c_pointer(f,rds_c_ref(s,o->out));
    fputs(";\n", f);
    fputs("const uint64_t*a[]={", f);
    bool storage_select=rds_c_onehot_storage(s,o);
    for (uint32_t i = 0; i < (storage_select?1:o->nargs); ++i) {
        rds_c_pointer(f, rds_c_ref(s, args[i]));
        fputc(',', f);
    }
    fputs("0};\n", f);
    fputs("static const uint32_t aw[]={", f);
    for (uint32_t i = 0; i < o->nargs; ++i)
        fprintf(f, "%u,", s->values[args[i]].width);
    fputs("0};\n", f);
    fputs("static const uint64_t im[]={", f);
    for (uint32_t i = 0; i < o->nimm; ++i)
        fprintf(f, "UINT64_C(0x%016" PRIx64 "),", s->imm[o->imm + i]);
    fputs("0};\n", f);
    fprintf(f, "const uint32_t w=%u,n=%u,argc=%u;(void)a;(void)aw;(void)im;(void)w;(void)n;(void)argc;\n", w,
            n, o->nargs);
    switch (o->code) {
    case RDS_CONST:
        fputs("memcpy(d,im,n*8);", f);
        break;
    case RDS_COPY:
        fputs("memcpy(d,a[0],n*8);", f);
        break;
    case RDS_NOT:
        fputs("for(uint32_t i=0;i<n;++i)d[i]=~a[0][i];", f);
        break;
    case RDS_AND:
    case RDS_OR:
    case RDS_XOR:
        fprintf(f, "for(uint32_t i=0;i<n;++i)d[i]=a[0][i]%s a[1][i];",
                o->code == RDS_AND  ? "&"
                : o->code == RDS_OR ? "|"
                                    : "^");
        break;
    case RDS_SET_CLEAR:
        fputs("for(uint32_t i=0;i<n;++i)d[i]=(a[0][i]|a[1][i])&~a[2][i];", f);
        break;
    case RDS_BALANCE:
        fputs("uint64_t carry=a[1][0]!=a[2][0];for(uint32_t "
              "i=0;i<n;++i){d[i]=a[2][0]?a[0][i]-carry:a[0][i]+carry;carry=a[2][0]?a[0][i]<carry:d[i]<a[0][i]"
              ";}",
              f);
        break;
    case RDS_COUNTER_STEP:
        fputs("d[0]=!a[1][0]?a[0][0]:a[0][0]==a[2][0]?0:a[0][0]+1;", f);
        break;
    case RDS_ADD:
    case RDS_SUB:
        if (o->code == RDS_ADD)
            fputs("uint64_t carry=0;for(uint32_t i=0;i<n;++i){uint64_t "
                  "t=a[0][i]+a[1][i],u=t+carry;carry=(t<a[0][i])|(u<t);d[i]=u;}",
                  f);
        else
            fputs("uint64_t carry=0;for(uint32_t i=0;i<n;++i){uint64_t "
                  "t=a[0][i]-a[1][i],u=t-carry;carry=(a[0][i]<a[1][i])|(t<u);d[i]=u;}",
                  f);
        break;
    case RDS_MUL:
        fputs("memset(d,0,n*8);for(uint32_t i=0;i<n;++i){uint64_t carry=0;for(uint32_t "
              "j=0;j<n-i;++j){__uint128_t "
              "t=(__uint128_t)a[0][i]*a[1][j]+d[i+j]+carry;d[i+j]=(uint64_t)t;carry=(uint64_t)(t>>64);}}",
              f);
        break;
    case RDS_SHL:
    case RDS_SHRU:
    case RDS_SHRS:
        fprintf(f,
                "memset(d,0,n*8);uint64_t sh;bool "
                "neg=%s;if(!cg_index(a[1],(aw[1]+63)/64,w,&sh)){if(neg)memset(d,255,n*8);}else{",
                o->code == RDS_SHRS ? "cg_bit(a[0],w-1)" : "false");
        if (o->code == RDS_SHL)
            fputs("cg_bits(d,(uint32_t)sh,a[0],0,w-(uint32_t)sh);", f);
        else
            fputs("cg_bits(d,0,a[0],(uint32_t)sh,w-(uint32_t)sh);if(neg)for(uint32_t "
                  "i=w-(uint32_t)sh;i<w;++i)cg_put(d,i);",
                  f);
        fputc('}', f);
        break;
    case RDS_EQ:
    case RDS_ULT:
    case RDS_SLT:
        fputs("int cmp=cg_cmp(a[0],a[1],(aw[0]+63)/64);", f);
        if (o->code == RDS_SLT)
            fputs("if(cg_bit(a[0],aw[0]-1)!=cg_bit(a[1],aw[0]-1))cmp=cg_bit(a[0],aw[0]-1)?-1:1;", f);
        fprintf(f, "d[0]=cmp%s0;", o->code == RDS_EQ ? "==" : "<");
        break;
    case RDS_ONEHOT_VIEW: {
        fputs("uint64_t sel=a[0][0];if(cg_onehot_bad(sel)){memset(d,0,n*8);"
              "if(c->strict)return cg_fail(c,sel?\"onehot_mux selector is multi-hot\":"
              "\"onehot_mux selector is zero-hot\");}else{unsigned ix=__builtin_ctzll(sel);",f);
        bool linear=true,aligned=true;
        uint64_t start=s->imm[o->imm],stride=o->nimm>1?s->imm[o->imm+1]-start:0;
        for(uint32_t i=0;i<o->nimm;++i){
            linear&=s->imm[o->imm+i]==start+i*stride;
            aligned&=s->imm[o->imm+i]%64==0;
        }
        if(linear)fprintf(f,"uint64_t bit=UINT64_C(%" PRIu64 ")+ix*UINT64_C(%" PRIu64 ");",start,stride);
        else fputs("uint64_t bit=im[ix];",f);
        if(aligned)fputs("memcpy(d,a[1]+bit/64,n*8);",f);
        else if(n<=64){
            fputs("const uint64_t*p=a[1]+bit/64;uint32_t sh=bit%64;",f);
            for(uint32_t j=0;j<n;++j){
                uint32_t bits=w-j*64<64?w-j*64:64;
                fprintf(f,"d[%u]=p[%u]>>sh;if(sh>%u)d[%u]|=p[%u]<<(64-sh);",j,j,64-bits,j,j+1);
            }
        }else fputs("cg_bits(d,0,a[1],(uint32_t)bit,w);",f);
        fputs("}",f);
        break;
    }
    case RDS_ONEHOT:
        if(s->emit_private&&s->values[args[0]].width<=64){
            /* Local SSA references are C pointers, not arena offsets. */
            fputs("uint64_t sel=a[0][0];if(cg_onehot_bad(sel)){memset(d,0,n*8);"
                  "if(c->strict)return cg_fail(c,sel?\"onehot_mux selector is multi-hot\":\"onehot_mux selector is zero-hot\");}"
                  "else memcpy(d,a[1+__builtin_ctzll(sel)],n*8);",f);
            break;
        }
        if(storage_select){
            bool rings=false;
            for(uint32_t i=1;i<o->nargs;++i){uint32_t object=0,word=0;
                (void)payload_storage(s,args[i],&object,&word);rings|=ring_payload(s,object);}
            if(rings){
                fputs("uint64_t sel=a[0][0];if(cg_onehot_bad(sel)){memset(d,0,n*8);"
                      "if(c->strict)return cg_fail(c,sel?\"onehot_mux selector is multi-hot\":\"onehot_mux selector is zero-hot\");}else{",f);
                ring_payload_selection(f,s,o);fputs("}",f);break;
            }
            fputs("static const size_t refs[]={",f);
            for(uint32_t i=1;i<o->nargs;++i){uint32_t object=0,word=0;
                (void)payload_storage(s,args[i],&object,&word);
                fprintf(f,"offsetof(object_state,o%u.data)+%u,",object,word*8);}
            fputs("};(void)refs;uint64_t sel=a[0][0];if(cg_onehot_bad(sel)){memset(d,0,n*8);"
                  "if(c->strict)return cg_fail(c,sel?\"onehot_mux selector is multi-hot\":\"onehot_mux selector is zero-hot\");}else{",f);
            bool tree=s->schedule->count==1&&o->nargs<=17;
            if(tree){
                fputs("\n#if defined(__znver4__)\nunsigned ix=__builtin_ctzll(sel);(void)ix;memcpy(d,(const unsigned char*)h+",f);
                payload_address_tree(f,s,o,0,o->nargs-1);fputs(",n*8);\n#else\n",f);
            }
            fputs("memcpy(d,(const unsigned char*)h+refs[__builtin_ctzll(sel)],n*8);",f);
            if(tree)fputs("\n#endif\n",f);
            fputs("}",f);
            break;
        }
        if(s->values[args[0]].width<=64){
            /* A host word can validate and locate the selected lane without
             * scanning bits. Preserve zero/multi-hot behavior in both modes. */
            uint64_t bank=raw_ref(s,args[1])>>62;bool common=true,narrow=true;
            for(uint32_t i=1;i<o->nargs;++i){uint64_t ref=raw_ref(s,args[i]);
                if((ref>>62)!=bank)common=false;
                if((ref&UINT64_C(0x3fffffffffffffff))>UINT32_MAX)narrow=false;}
            fprintf(f,"static const %s refs[]={",common&&narrow?"uint32_t":"uint64_t");
            for(uint32_t i=1;i<o->nargs;++i){uint64_t ref=raw_ref(s,args[i]);
                fprintf(f,"UINT64_C(0x%" PRIx64 "),",common?ref&UINT64_C(0x3fffffffffffffff):ref);}
            fputs("};uint64_t sel=a[0][0];if(cg_onehot_bad(sel)){memset(d,0,n*8);"
                  "if(c->strict)return cg_fail(c,sel?\"onehot_mux selector is multi-hot\":"
                  "\"onehot_mux selector is zero-hot\");}else memcpy(d,",f);
            if(common)fprintf(f,"%s+refs[__builtin_ctzll(sel)]",bank==2?"q":bank==1?"rds_constants":"v");
            else fputs("cg_ptr(c,refs[__builtin_ctzll(sel)])",f);
            fputs(",n*8);",f);
            break;
        }
        fputs("memset(d,0,n*8);uint32_t k=UINT32_MAX;bool bad=false;for(uint32_t "
              "i=0;i<argc-1;++i)if(cg_bit(a[0],i)){if(k!=UINT32_MAX){bad=true;         "
              "break;}k=i+1;}if(bad||k==UINT32_MAX){if(c->strict)return cg_fail(c,bad?\"onehot_mux selector "
              "is multi-hot\":\"onehot_mux selector is zero-hot\");}else memcpy(d,a[k],n*8);",
              f);
        break;
    case RDS_SLICE:
        fputs("cg_bits(d,0,a[0],(uint32_t)im[0],w);", f);
        break;
    case RDS_ZEXT:
    case RDS_SEXT:
        fputs("memset(d,0,n*8);cg_bits(d,0,a[0],0,aw[0]);", f);
        if (o->code == RDS_SEXT)
            fputs("if(cg_bit(a[0],aw[0]-1))for(uint32_t i=aw[0];i<w;++i)cg_put(d,i);", f);
        break;
    case RDS_PACK:
        fputs("uint32_t pos=0;for(uint32_t i=0;i<argc;++i){cg_bits(d,pos,a[i],0,aw[i]);pos+=aw[i];}", f);
        break;
    case RDS_INDEX:
    case RDS_INJECT:
        fputs("memset(d,0,n*8);uint64_t ix=a[1][0];\n#ifndef NDEBUG\n"
              "if(!cg_index(a[1],(aw[1]+63)/64,im[0],&ix)){if(c->strict)return "
              "cg_fail(c,\"vector index out of range\");}else\n#endif\n{",
              f);
        if (o->code == RDS_INDEX) {
            /* Array geometry is static: aligned elements are ordinary loads,
             * while short packed elements need only the intersecting words. */
            if (!(w % 64)) fprintf(f, "memcpy(d,a[0]+ix*%u,%u);", n, n*8);
            else if (n <= 64) {
                fputs("uint64_t bit=ix*im[1];const uint64_t*p=a[0]+bit/64;uint32_t sh=bit%64;", f);
                for (uint32_t j=0;j<n;++j) {
                    uint32_t bits=w-j*64<64?w-j*64:64;
                    fprintf(f,"d[%u]=p[%u]>>sh;if(sh>%u)d[%u]|=p[%u]<<(64-sh);",j,j,64-bits,j,j+1);
                }
            } else fputs("cg_bits(d,0,a[0],(uint32_t)(ix*im[1]),w);", f);
        } else
            fputs("memcpy(d,a[0],n*8);cg_bits(d,(uint32_t)(ix*im[1]),a[2],0,(uint32_t)im[1]);", f);
        fputc('}', f);
        break;
    case RDS_WRITE_SET:
        /* A single enabled write has no collision search or packed port loop. */
        if (s->imm[o->imm+2]==1 && s->imm[o->imm+3]<=64) {
            fputs("memcpy(d,a[0],n*8);if(a[1][0]&1){uint64_t ix=a[2][0];\n#ifndef NDEBUG\n"
                  "if(ix>=im[0]){if(c->strict)return cg_fail(c,\"vector write index out of range\");"
                  "memset(d,0,n*8);}else\n#endif\n{",f);
            uint32_t element=(uint32_t)s->imm[o->imm+1];
            if (!(element%64)) fprintf(f,"memcpy(d+ix*%u,a[3],%u);",element/64,element/8);
            else if(element==1) fputs("uint64_t bit=UINT64_C(1)<<(ix%64);d[ix/64]=(d[ix/64]&~bit)|((a[3][0]&1)<<(ix%64));",f);
            else fputs("cg_bits(d,(uint32_t)(ix*im[1]),a[3],0,(uint32_t)im[1]);",f);
            fputs("}}",f);break;
        }
        fputs("memcpy(d,a[0],n*8);for(uint32_t i=0;i<im[2];++i)if(cg_bit(a[1],i)){",f);
        if(s->values[args[2]].width<=64)
            fputs("uint64_t ix=(a[2][0]>>(i*im[3]))&cg_mask((uint32_t)im[3]);\n",f);
        else fputs("uint64_t ix=0;cg_bits(&ix,0,a[2],(uint32_t)(i*im[3]),(uint32_t)im[3]);\n",f);
        fputs(
              "#ifndef NDEBUG\n"
              "if(ix>=im[0]){if(c->strict)return cg_fail(c,\"vector write index out of range\");"
              "memset(d,0,n*8);break;}bool bad=false;for(uint32_t j=0;j<i;++j)if(cg_bit(a[1],j)){",f);
        if(s->values[args[2]].width<=64)
            fputs("uint64_t old=(a[2][0]>>(j*im[3]))&cg_mask((uint32_t)im[3]);",f);
        else fputs("uint64_t old=0;cg_bits(&old,0,a[2],(uint32_t)(j*im[3]),(uint32_t)im[3]);",f);
        fputs(
              "if(ix==old){if(c->strict)return cg_fail(c,\"vector write collision\");bad=true;break;}}"
              "if(bad){memset(d,0,n*8);break;}\n#endif\n"
              "cg_bits(d,(uint32_t)(ix*im[1]),a[3],(uint32_t)(i*im[1]),(uint32_t)im[1]);}", f);
        break;
    case RDS_READ: {
        uint32_t m = (uint32_t)s->imm[o->imm];
        fprintf(f,
                "uint64_t ix=a[0][0];\n#ifndef NDEBUG\n"
                "if(!cg_index(a[0],(aw[0]+63)/64,%u,&ix)){memset(d,0,n*8);if(c->strict)return "
                "cg_fail(c,\"memory read address out of range\");}else\n#endif\n memcpy(d,c->memories[%u]+ix*n,n*8);",
                s->mems[m].depth, m);
        break;
    }
    case RDS_DECODE:
        fputs("memcpy(d,im+1,n*8);uint32_t an=(aw[0]+63)/64;const uint64_t*row=im+1+n;for(uint64_t "
              "i=0;i<im[0];++i,row+=2*an+n){bool match=true;for(uint32_t "
              "j=0;j<an;++j)if((a[0][j]&row[an+j])!=row[j])match=false;if(match){memcpy(d,row+2*an,n*8);     "
              "    break;}}",
              f);
        break;
    default:
        fputs("return cg_fail(c,\"unknown compiled operation\");", f);
        break;
    }
    fputs("\nd[n-1]&=cg_mask(w);}\n", f);
}
void rds_c_operation(FILE *f,const rds_sim *s,const rds_op *o){
    bool small=rds_c_small_value(s,o->out);
    if(small){fputs("{uint64_t small_result=0;",f);*s->emit_destination=o->out;}
    if(o->code==RDS_CONTRACT)rds_c_kernel(f,s,o);
    else operation_body(f,s,o);
    if(small){*s->emit_destination=RDS_NONE;rds_c_store_small(f,s,o->out);fputs("}\n",f);}
}
