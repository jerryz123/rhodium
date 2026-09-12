/* Loads versioned little-endian models and implements native port ownership. */
#include "internal.h"
#include <limits.h>

typedef struct { FILE *file; bool ok; size_t remaining; uint64_t hash; } reader;
static void source_bytes(reader *r,const void *data,size_t count) {
    const unsigned char *p=data;
    for(size_t i=0;i<count;++i)r->hash=(r->hash^p[i])*UINT64_C(1099511628211);
}
static uint32_t u32(reader *r) {
    unsigned char b[4];
    if (!r->ok || r->remaining < 4 || fread(b, 1, 4, r->file) != 4) {
        r->ok = false; return 0;
    }
    r->remaining -= 4;
    source_bytes(r,b,4);
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
static uint64_t u64(reader *r) {
    uint64_t lo = u32(r), hi = u32(r);
    return lo | hi << 32;
}
static char *name(reader *r) {
    uint32_t n = u32(r);
    if (!r->ok || n > r->remaining || n > 1048576) { r->ok = false; return NULL; }
    char *p = calloc((size_t)n + 1, 1);
    if (!p) { r->ok = false; return NULL; }
    if (fread(p, 1, n, r->file) != n || memchr(p, 0, n)) r->ok = false;
    if(r->ok)source_bytes(r,p,n);
    r->remaining -= n;
    return p;
}
int rds_fail(rds_sim *s, const char *message) {
    snprintf(s->error, sizeof s->error, "%s", message);
    return -1;
}
const char *rds_error(const rds_sim *s) { return s->error; }
void rds_set_strict(rds_sim *s, int enabled) { s->strict = enabled != 0; s->evaluated = false; }
void rds_free(rds_sim *s) {
    if (!s) return;
    rds_schedule_free(s);
    free(s->partition_owners);
    rds_objects_free(s);
    if (s->mems) for (uint32_t i = 0; i < s->nm; ++i) free(s->mems[i].data);
    if (s->checks) for (uint32_t i = 0; i < s->nc; ++i) free(s->checks[i].name);
    if (s->ports) for (uint32_t i = 0; i < s->np; ++i) free(s->ports[i].name);
    free(s->current);
    free(s->values); free(s->ops); free(s->args); free(s->imm);
    free(s->regs); free(s->mems); free(s->writes); free(s->reads);
    free(s->checks); free(s->ports); free(s->arena); free(s->next); free(s);
}
static bool valid_id(rds_sim *s, uint32_t id) { return id < s->nv; }
static bool bit_id(rds_sim *s, uint32_t id) {
    return valid_id(s, id) && s->values[id].width == 1;
}
static bool equal_width(rds_sim *s, uint32_t a, uint32_t b) {
    return valid_id(s, a) && valid_id(s, b) && s->values[a].width == s->values[b].width;
}
/* Validate dataflow before execution: each input/state value is a source and
 * each computed value has exactly one definition after all its operands. */
bool rds_validate_graph(rds_sim *s, uint32_t inputs) {
    bool *defined = calloc(s->nv ? s->nv : 1, sizeof *defined);
    if (!defined) return false;
    bool ok = true;
    if(inputs>s->nv){free(defined);return false;}
    for(uint32_t i=0;i<inputs;++i)defined[i]=true;
    for (uint32_t i = 0; i < s->np && ok; ++i) {
        rds_port *p = &s->ports[i];
        ok = p->direction <= 2 && valid_id(s, p->value) && p->name;
        if (ok && p->direction != 1) {
            ok = !defined[p->value]; defined[p->value] = true;
        }
        for (uint32_t j = 0; j < i && ok; ++j) ok = strcmp(p->name, s->ports[j].name) != 0;
    }
    for (uint32_t i = 0; i < s->nr && ok; ++i) {
        rds_reg *r = &s->regs[i];
        ok = equal_width(s, r->q, r->d) && !defined[r->q];
        if (ok && r->reset != RDS_NONE)
            ok = bit_id(s, r->reset) && equal_width(s, r->q, r->reset_value);
        if (ok) defined[r->q] = true;
    }
    for (uint32_t i = 0; i < s->ns && ok; ++i) {
        rds_read *r = &s->reads[i];
        ok = r->mem < s->nm && valid_id(s, r->address) && bit_id(s, r->enable)
             && valid_id(s, r->q) && !defined[r->q]
             && (r->write == RDS_NONE || bit_id(s, r->write));
        if (ok) { ok = s->values[r->q].width == s->mems[r->mem].width; defined[r->q] = true; }
    }
    for (uint32_t i = 0; i < s->no && ok; ++i) {
        rds_op *o = &s->ops[i];
        ok = o->code < RDS_OPCODE_COUNT && valid_id(s, o->out) && !defined[o->out]
             && o->args <= s->na && o->nargs <= s->na - o->args
             && o->imm <= s->ni && o->nimm <= s->ni - o->imm;
        if (!ok) break;
        for (uint32_t j = 0; j < o->nargs && ok; ++j) {
            uint32_t a = s->args[o->args + j]; ok = valid_id(s, a) && defined[a];
        }
        if (!ok) break;
        uint32_t *a = s->args + o->args;
        uint64_t *m = s->imm + o->imm;
        uint32_t w = s->values[o->out].width;
        switch (o->code) {
        case RDS_CONTRACT: ok = rds_kernel_validate(s,o); break;
        case RDS_CONST: ok = !o->nargs && o->nimm == s->values[o->out].words; break;
        case RDS_COPY: case RDS_NOT:
            ok = o->nargs == 1 && equal_width(s, a[0], o->out); break;
        case RDS_AND: case RDS_OR: case RDS_XOR: case RDS_ADD: case RDS_SUB: case RDS_MUL:
            ok = o->nargs == 2 && equal_width(s, a[0], a[1]) && equal_width(s, a[0], o->out); break;
        case RDS_SET_CLEAR:
            ok = o->nargs == 3 && equal_width(s, a[0], a[1]) && equal_width(s, a[0], a[2]) && equal_width(s, a[0], o->out); break;
        case RDS_BALANCE:
            ok = o->nargs == 3 && equal_width(s, a[0], o->out) && bit_id(s, a[1]) && bit_id(s, a[2]); break;
        case RDS_COUNTER_STEP:
            ok = o->nargs == 3 && w <= 64 && equal_width(s, a[0], o->out) && bit_id(s, a[1]) && equal_width(s, a[0], a[2]); break;
        case RDS_OBJECT_QUERY:
            ok = o->nimm == 2 && m[0] < s->nx; break;
        case RDS_BYTE_MERGE:
            ok = o->nargs == 3 && !o->nimm && w <= 64 && w % 8 == 0 && equal_width(s,a[0],o->out) && equal_width(s,a[1],o->out) && s->values[a[2]].width == w / 8; break;
        case RDS_ALU:
            ok = o->nargs == 3 && !o->nimm && (w == 32 || w == 64) && equal_width(s,a[0],o->out) && equal_width(s,a[1],o->out) && s->values[a[2]].width == 23; break;
        case RDS_SHL: case RDS_SHRU: case RDS_SHRS:
            ok = o->nargs == 2 && equal_width(s, a[0], o->out); break;
        case RDS_EQ: case RDS_ULT: case RDS_SLT:
            ok = o->nargs == 2 && w == 1 && equal_width(s, a[0], a[1]); break;
        case RDS_MUX:
            ok = o->nargs >= 2 && (uint64_t)o->nimm == (uint64_t)(o->nargs - 2) * s->values[a[0]].words;
            for (uint32_t j = 1; j < o->nargs && ok; ++j) ok = equal_width(s, a[j], o->out);
            break;
        case RDS_ONEHOT:
            ok = o->nargs >= 2 && s->values[a[0]].width == o->nargs - 1;
            for (uint32_t j = 1; j < o->nargs && ok; ++j) ok = equal_width(s, a[j], o->out);
            break;
        case RDS_ONEHOT_VIEW:
            ok=o->nargs==2 && s->values[a[0]].width<=64 && o->nimm==s->values[a[0]].width;
            for(uint32_t j=0;j<o->nimm&&ok;++j)ok=m[j]<=s->values[a[1]].width && (uint64_t)w+m[j]<=s->values[a[1]].width;
            break;
        case RDS_SLICE:
            ok = o->nargs == 1 && o->nimm == 1 && m[0] <= s->values[a[0]].width
                 && w <= s->values[a[0]].width - m[0]; break;
        case RDS_ZEXT: case RDS_SEXT:
            ok = o->nargs == 1 && w >= s->values[a[0]].width; break;
        case RDS_PACK: {
            uint64_t total = 0;
            for (uint32_t j = 0; j < o->nargs; ++j) total += s->values[a[j]].width;
            ok = o->nargs > 0 && total == w; break;
        }
        case RDS_INDEX: case RDS_INJECT:
            ok = o->nargs == (o->code == RDS_INDEX ? 2u : 3u) && o->nimm == 2
                 && m[0] > 0 && m[1] > 0 && m[0] <= UINT32_MAX / m[1]
                 && s->values[a[0]].width == m[0] * m[1];
            if (ok) ok = o->code == RDS_INDEX ? w == m[1] : w == m[0] * m[1] && s->values[a[2]].width == m[1];
            break;
        case RDS_WRITE_SET:
            ok = o->nargs == 4 && o->nimm == 4 && m[0] && m[1] && m[2] && m[3]
                 && m[0] <= UINT32_MAX / m[1] && m[2] <= UINT32_MAX / m[3]
                 && m[2] <= UINT32_MAX / m[1] && w == m[0] * m[1]
                 && s->values[a[0]].width == w && s->values[a[1]].width == m[2]
                 && s->values[a[2]].width == m[2] * m[3] && s->values[a[3]].width == m[2] * m[1]
                 && m[3] <= 64; break;
        case RDS_READ:
            ok = o->nargs == 1 && o->nimm == 1 && m[0] < s->nm && w == s->mems[m[0]].width; break;
        case RDS_DECODE:
            ok = o->nargs == 1 && o->nimm >= 1;
            if (ok) {
                uint64_t row = 2u * (uint64_t)s->values[a[0]].words + s->values[o->out].words;
                ok = m[0] <= (UINT32_MAX - 1u - s->values[o->out].words) / row
                     && o->nimm == 1u + s->values[o->out].words + m[0] * row;
            }
            break;
        default: ok = false;
        }
        defined[o->out] = true;
    }
    for (uint32_t i = 0; i < s->nv && ok; ++i) ok = defined[i];
    for (uint32_t i = 0; i < s->nw && ok; ++i) {
        rds_write *r = &s->writes[i];
        ok = r->mem < s->nm && valid_id(s, r->address) && valid_id(s, r->data) && bit_id(s, r->enable);
        if (ok) ok = s->values[r->data].width == s->mems[r->mem].width;
        if (ok && r->mask != RDS_NONE)
            ok = valid_id(s, r->mask) && r->granule && s->mems[r->mem].width % r->granule == 0
                 && s->values[r->mask].width == s->mems[r->mem].width / r->granule;
    }
    for (uint32_t i = 0; i < s->nc && ok; ++i) {
        rds_assert *c = &s->checks[i];
        ok = bit_id(s, c->condition) && bit_id(s, c->reset) && bit_id(s, c->guard) && c->name;
    }
    free(defined); return ok;
}
rds_sim *rds_load(const char *path, char *error, size_t error_size) {
    return rds_load_with_options(path, NULL, error, error_size);
}
rds_sim *rds_load_with_options(const char *path, const rds_options *options, char *error, size_t error_size) {
    const char *message = "invalid or truncated Rhodium simulation model";
    rds_sim *s = calloc(1, sizeof *s);
    FILE *f = fopen(path, "rb");
    if (!s || !f) { message = "cannot open model or allocate simulator"; goto fail; }
    if (fseek(f, 0, SEEK_END)) goto fail;
    long size = ftell(f);
    if (size < 52 || (uint64_t)size > UINT32_MAX || fseek(f, 0, SEEK_SET)) goto fail;
    reader r = { f, true, (size_t)size, UINT64_C(14695981039346656037) };
    unsigned char magic[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "RHDMSIM\0", 8)) goto fail;
    r.remaining -= 8;
    source_bytes(&r,magic,8);
    uint32_t version = u32(&r);
    if (version < 1 || version > RDS_VERSION) { message = "unsupported Rhodium model version"; goto fail; }
    s->nv = u32(&r); s->no = u32(&r); s->na = u32(&r); s->ni = u32(&r);
    s->nr = u32(&r); s->nm = u32(&r); s->nw = u32(&r); s->ns = u32(&r);
    s->nc = u32(&r); s->np = u32(&r);
    s->nx = version >= 2 ? u32(&r) : 0;
    uint64_t minimum = 4ull*s->nv + 24ull*s->no + 4ull*s->na + 8ull*s->ni
        + 16ull*s->nr + 8ull*s->nm + 24ull*s->nw + 20ull*s->ns + 16ull*s->nc + 12ull*s->np + 24ull*s->nx;
    if (!r.ok || minimum > r.remaining) goto fail;
#define ALLOC(field, count) do { s->field = calloc(s->count ? s->count : 1, sizeof *s->field); if (!s->field) goto fail; } while (0)
    ALLOC(values,nv); ALLOC(ops,no); ALLOC(args,na); ALLOC(imm,ni); ALLOC(regs,nr);
    ALLOC(mems,nm); ALLOC(writes,nw); ALLOC(reads,ns); ALLOC(checks,nc); ALLOC(ports,np);
    ALLOC(objects,nx);
#undef ALLOC
    for (uint32_t i = 0; i < s->nv; ++i) {
        uint32_t w = u32(&r);
        if (!w || w > UINT32_MAX - 63) goto fail;
        s->values[i].width = w; s->values[i].words = (w + 63) / 64;
        s->values[i].offset = s->value_words; s->values[i].state = SIZE_MAX; s->values[i].origin = i;
        if (s->values[i].words > SIZE_MAX/8 - s->value_words) goto fail;
        s->value_words += s->values[i].words;
    }
    for (uint32_t i = 0; i < s->no; ++i) {
        rds_op *o = &s->ops[i];
        o->code = u32(&r); o->out = u32(&r); o->args = u32(&r);
        o->nargs = u32(&r); o->imm = u32(&r); o->nimm = u32(&r);
    }
    for (uint32_t i = 0; i < s->na; ++i) s->args[i] = u32(&r);
    for (uint32_t i = 0; i < s->ni; ++i) s->imm[i] = u64(&r);
    for (uint32_t i = 0; i < s->nr; ++i) {
        rds_reg *v = &s->regs[i];
        v->q = u32(&r); v->d = u32(&r); v->reset = u32(&r); v->reset_value = u32(&r);
        v->owner=RDS_NONE;
    }
    for (uint32_t i = 0; i < s->nm; ++i) {
        rds_mem *m = &s->mems[i]; m->width = u32(&r); m->depth = u32(&r);
        if (!m->width || m->width > UINT32_MAX - 63 || !m->depth) goto fail;
        m->words = (m->width + 63) / 64;
        if ((uint64_t)m->words * m->depth > SIZE_MAX/8 - s->memory_bytes/8) goto fail;
        s->memory_bytes += (size_t)m->words * m->depth * 8;
    }
    for (uint32_t i = 0; i < s->nw; ++i) {
        rds_write *v = &s->writes[i];
        v->mem = u32(&r); v->address = u32(&r); v->data = u32(&r);
        v->enable = u32(&r); v->mask = u32(&r); v->granule = u32(&r);
    }
    for (uint32_t i = 0; i < s->ns; ++i) {
        rds_read *v = &s->reads[i];
        v->mem = u32(&r); v->address = u32(&r); v->enable = u32(&r);
        v->q = u32(&r); v->write = u32(&r);
    }
    for (uint32_t i = 0; i < s->nc; ++i) {
        rds_assert *c = &s->checks[i];
        c->condition = u32(&r); c->reset = u32(&r); c->guard = u32(&r); c->name = name(&r);
    }
    for (uint32_t i = 0; i < s->np; ++i) {
        s->ports[i].direction = u32(&r); s->ports[i].value = u32(&r); s->ports[i].name = name(&r);
    }
    for (uint32_t i = 0; i < s->nx; ++i) {
        rds_object *o = &s->objects[i];
        o->kind = u32(&r); o->width = u32(&r); o->depth = u32(&r); o->flags = u32(&r); o->ni = u32(&r);
        if (!r.ok || o->ni > r.remaining / 4) goto fail;
        o->inputs = calloc(o->ni ? o->ni : 1, sizeof *o->inputs);
        if (!o->inputs) goto fail;
        for (uint32_t j = 0; j < o->ni; ++j) {
            o->inputs[j] = u32(&r);
            if (o->inputs[j] != RDS_NONE && o->inputs[j] >= s->nv) goto fail;
        }
        o->name = name(&r);
    }
    if (!r.ok || r.remaining || !rds_validate_graph(s,0)) goto fail;
    for (uint32_t i = 0; i < s->nr; ++i) {
        s->regs[i].next = s->next_words; s->next_words += s->values[s->regs[i].q].words;
    }
    for (uint32_t i = 0; i < s->ns; ++i) {
        s->reads[i].next = s->next_words; s->next_words += s->values[s->reads[i].q].words;
    }
    s->arena = calloc(s->value_words ? s->value_words : 1, 8);
    s->next = rds_state_bank(s->next_words);
    if (!s->arena || !s->next) goto fail;
    for (uint32_t i = 0; i < s->nm; ++i) {
        s->mems[i].data = calloc((size_t)s->mems[i].words * s->mems[i].depth, 8);
        if (!s->mems[i].data) goto fail;
    }
    s->model_bytes = (size_t)size;
    if (rds_objects_init(s)) { message = "invalid native object descriptor"; goto fail; }
    s->source_nv = s->nv; s->source_no = s->no; s->source_key=r.hash;
    if (rds_schedule_build(s, options)) { message = "cannot build native execution schedule"; goto fail; }
    for (uint32_t i = 0; i < s->nr; ++i) s->values[s->regs[i].q].state = s->regs[i].next;
    for (uint32_t i = 0; i < s->ns; ++i) s->values[s->reads[i].q].state = s->reads[i].next;
    s->descriptor_bytes = sizeof *s + (size_t)s->nv*sizeof *s->values + (size_t)s->no*sizeof *s->ops
        + (size_t)s->na*sizeof *s->args + (size_t)s->ni*sizeof *s->imm + (size_t)s->nr*sizeof *s->regs
        + (size_t)s->nm*sizeof *s->mems + (size_t)s->nw*sizeof *s->writes + (size_t)s->ns*sizeof *s->reads
        + (size_t)s->nc*sizeof *s->checks + (size_t)s->np*sizeof *s->ports;
    for (uint32_t i = 0; i < s->np; ++i) s->descriptor_bytes += strlen(s->ports[i].name) + 1;
    for (uint32_t i = 0; i < s->nc; ++i) s->descriptor_bytes += strlen(s->checks[i].name) + 1;
    fclose(f); return s;
fail:
    if (error && error_size) snprintf(error, error_size, "%s", message);
    if (f) fclose(f);
    rds_free(s); return NULL;
}
int rds_find_port(const rds_sim *s, const char *n) {
    for (uint32_t i = 0; i < s->np; ++i) if (!strcmp(n, s->ports[i].name)) return (int)i;
    return -1;
}
uint32_t rds_port_width(const rds_sim *s, uint32_t p) { return p < s->np ? s->values[s->ports[p].value].width : 0; }
int rds_port_is_input(const rds_sim *s, uint32_t p) { return p < s->np && s->ports[p].direction == 0; }
int rds_set(rds_sim *s, uint32_t p, const uint64_t *v, size_t n) {
    if (!rds_port_is_input(s, p)) return rds_fail(s, "port is not a writable data input");
    uint32_t id = s->ports[p].value;
    if (!v || n != s->values[id].words) return rds_fail(s, "input word count differs from port width");
    memcpy(rds_data(s, id), v, n * 8); rds_trim(rds_data(s, id), s->values[id].width);
    s->evaluated = false; s->error[0] = 0; return 0;
}
int rds_get(rds_sim *s, uint32_t p, uint64_t *v, size_t n) {
    if (p >= s->np || !v) return rds_fail(s, "invalid port read");
    if (!s->evaluated) return rds_fail(s, "evaluate before reading outputs");
    uint32_t id = s->ports[p].value;
    if (n != s->values[id].words) return rds_fail(s, "output word count differs from port width");
    memcpy(v, rds_data(s, id), n * 8); return 0;
}
int rds_set_u64(rds_sim *s, uint32_t p, uint64_t v) { return rds_set(s, p, &v, 1); }
int rds_get_u64(rds_sim *s, uint32_t p, uint64_t *v) { return rds_get(s, p, v, 1); }
rds_stats rds_get_stats(const rds_sim *s) {
    return (rds_stats){s->model_bytes, s->value_words * 8, s->memory_bytes, s->next_words * 8 * (s->current ? 2 : 1),
                       s->source_nv, s->source_no, s->nr, s->nm, s->np, s->cycles,
                       s->descriptor_bytes, s->schedule_bytes, s->workers, s->components, s->batches,
                       s->nx, s->object_bytes, s->payload_bytes};
}
