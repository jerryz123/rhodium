/* Evaluates fixed-width operations and publishes checked snapshot transitions. */
#include "internal.h"
#include "alu.h"
RDS_ALU_CODE

static bool bit(const uint64_t *p, uint32_t i) { return (p[i / 64] >> (i % 64)) & 1; }
static void put(uint64_t *p, uint32_t i, bool v) {
    uint64_t mask = UINT64_C(1) << (i % 64);
    p[i / 64] = (p[i / 64] & ~mask) | (v ? mask : 0);
}
/* A word-oriented bit copy also handles unaligned record/vector projections. */
void rds_copy_bits(uint64_t *dst, uint32_t d, const uint64_t *src, uint32_t b, uint32_t width) {
    while (width) {
        uint32_t take = 64 - d % 64;
        if (take > width) take = width;
        uint64_t v = src[b / 64] >> (b % 64);
        if (b % 64 && take > 64 - b % 64) v |= src[b / 64 + 1] << (64 - b % 64);
        uint64_t mask = take == 64 ? UINT64_MAX : (UINT64_C(1) << take) - 1;
        dst[d / 64] = (dst[d / 64] & ~(mask << (d % 64))) | ((v & mask) << (d % 64));
        d += take; b += take; width -= take;
    }
}
bool rds_index(rds_sim *s, uint32_t id, uint64_t limit, uint64_t *index) {
    uint64_t *p = rds_data(s, id);
    for (uint32_t i = 1; i < s->values[id].words; ++i) if (p[i]) return false;
    *index = p[0]; return *index < limit;
}
static int compare(const uint64_t *a, const uint64_t *b, uint32_t words) {
    for (uint32_t i = words; i-- > 0;) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
int rds_execute(rds_sim *s, const rds_op *o) {
    if (o->code == RDS_CONTRACT) return rds_kernel_execute(s,o);
    if (o->code == RDS_BYTE_MERGE) {
        uint64_t a=rds_data(s,s->args[o->args])[0], b=rds_data(s,s->args[o->args+1])[0];
        uint64_t mask=rds_data(s,s->args[o->args+2])[0], result=0;
        for (uint32_t byte=0;byte<s->values[o->out].width/8;++byte)
            result |= ((mask>>byte&1?b:a)>>(byte*8)&255)<<(byte*8);
        rds_data(s,o->out)[0]=result; return 0;
    }
    if (o->code == RDS_ALU) { rds_data(s,o->out)[0]=rds_alu(s->values[o->out].width,rds_data(s,s->args[o->args])[0],rds_data(s,s->args[o->args+1])[0],rds_data(s,s->args[o->args+2])[0]); return 0; }
    if (o->code == RDS_OBJECT_QUERY) return rds_object_query(s, o);
    uint32_t *args = s->args + o->args;
    uint64_t *im = s->imm + o->imm;
    uint64_t *d = rds_data(s, o->out);
    uint64_t *a = o->nargs ? rds_data(s, args[0]) : NULL;
    uint64_t *b = o->nargs > 1 ? rds_data(s, args[1]) : NULL;
    uint32_t w = s->values[o->out].width, n = s->values[o->out].words;
    switch (o->code) {
    case RDS_CONST: memcpy(d, im, (size_t)n * 8); break;
    case RDS_COPY: memcpy(d, a, (size_t)n * 8); break;
    case RDS_NOT: for (uint32_t i = 0; i < n; ++i) d[i] = ~a[i]; break;
    case RDS_AND: for (uint32_t i = 0; i < n; ++i) d[i] = a[i] & b[i]; break;
    case RDS_OR: for (uint32_t i = 0; i < n; ++i) d[i] = a[i] | b[i]; break;
    case RDS_XOR: for (uint32_t i = 0; i < n; ++i) d[i] = a[i] ^ b[i]; break;
    case RDS_SET_CLEAR: {
        uint64_t *clear = rds_data(s, args[2]);
        s->set_clear(d, a, b, clear, n);
        break;
    }
    case RDS_BALANCE: {
        uint64_t decrement = rds_data(s, args[2])[0], carry = b[0] != decrement;
        if (!carry) memcpy(d, a, (size_t)n * 8);
        else for (uint32_t i = 0; i < n; ++i) {
            d[i] = decrement ? a[i] - carry : a[i] + carry;
            carry = decrement ? a[i] < carry : d[i] < a[i];
        }
        break;
    }
    case RDS_COUNTER_STEP:
        d[0] = !b[0] ? a[0] : a[0] == rds_data(s, args[2])[0] ? 0 : a[0] + 1;
        break;
    case RDS_ADD: {
        uint64_t carry = 0;
        for (uint32_t i = 0; i < n; ++i) {
            uint64_t t = a[i] + b[i], u = t + carry;
            carry = (t < a[i]) | (u < t); d[i] = u;
        }
        break;
    }
    case RDS_SUB: {
        uint64_t borrow = 0;
        for (uint32_t i = 0; i < n; ++i) {
            uint64_t t = a[i] - b[i], u = t - borrow;
            borrow = (a[i] < b[i]) | (t < borrow); d[i] = u;
        }
        break;
    }
    case RDS_MUL:
        memset(d, 0, (size_t)n * 8);
        for (uint32_t i = 0; i < n; ++i) {
            uint64_t carry = 0;
            for (uint32_t j = 0; j < n - i; ++j) {
                __uint128_t product = (__uint128_t)a[i] * b[j] + d[i + j] + carry;
                d[i + j] = (uint64_t)product; carry = (uint64_t)(product >> 64);
            }
        }
        break;
    case RDS_SHL: case RDS_SHRU: case RDS_SHRS: {
        memset(d, 0, (size_t)n * 8);
        uint64_t shift;
        bool negative = o->code == RDS_SHRS && bit(a, w - 1);
        if (!rds_index(s, args[1], w, &shift)) {
            if (negative) memset(d, 255, (size_t)n * 8);
        } else if (o->code == RDS_SHL) {
            rds_copy_bits(d, (uint32_t)shift, a, 0, w - (uint32_t)shift);
        } else {
            rds_copy_bits(d, 0, a, (uint32_t)shift, w - (uint32_t)shift);
            if (negative) for (uint32_t i = w - (uint32_t)shift; i < w; ++i) put(d, i, true);
        }
        break;
    }
    case RDS_EQ: case RDS_ULT: case RDS_SLT: {
        uint32_t aw = s->values[args[0]].width;
        int cmp = compare(a, b, s->values[args[0]].words);
        if (o->code == RDS_SLT && bit(a, aw - 1) != bit(b, aw - 1)) cmp = bit(a, aw - 1) ? -1 : 1;
        d[0] = o->code == RDS_EQ ? cmp == 0 : cmp < 0; break;
    }
    case RDS_MUX: {
        uint32_t selected = 1;
        uint32_t an = s->values[args[0]].words;
        for (uint32_t i = 0; i < o->nargs - 2; ++i)
            if (compare(a, im + (size_t)i * an, an) == 0) { selected = i + 2; break; }
        memcpy(d, rds_data(s, args[selected]), (size_t)n * 8); break;
    }
    case RDS_ONEHOT: {
        memset(d, 0, (size_t)n * 8);
        uint32_t selected = RDS_NONE;
        for (uint32_t i = 0; i < o->nargs - 1; ++i) if (bit(a, i)) {
            if (selected != RDS_NONE) {
                if (s->strict) return rds_fail(s, "onehot_mux selector is multi-hot");
                return 0;
            }
            selected = i + 1;
        }
        if (selected == RDS_NONE) return s->strict ? rds_fail(s, "onehot_mux selector is zero-hot") : 0;
        memcpy(d, rds_data(s, args[selected]), (size_t)n * 8); break;
    }
    case RDS_ONEHOT_VIEW: {
        memset(d,0,(size_t)n*8);uint64_t sel=a[0];
        if(!sel||(sel&(sel-1)))return s->strict?rds_fail(s,sel?"onehot_mux selector is multi-hot":"onehot_mux selector is zero-hot"):0;
        rds_copy_bits(d,0,rds_data(s,args[1]),(uint32_t)im[__builtin_ctzll(sel)],w);break;
    }
    case RDS_SLICE: rds_copy_bits(d, 0, a, (uint32_t)im[0], w); break;
    case RDS_ZEXT: case RDS_SEXT: {
        memset(d, 0, (size_t)n * 8);
        uint32_t aw = s->values[args[0]].width;
        rds_copy_bits(d, 0, a, 0, aw);
        if (o->code == RDS_SEXT && bit(a, aw - 1)) for (uint32_t i = aw; i < w; ++i) put(d, i, true);
        break;
    }
    case RDS_PACK: {
        uint32_t pos = 0;
        for (uint32_t i = 0; i < o->nargs; ++i) {
            uint32_t aw = s->values[args[i]].width;
            rds_copy_bits(d, pos, rds_data(s, args[i]), 0, aw); pos += aw;
        }
        break;
    }
    case RDS_INDEX: case RDS_INJECT: {
        memset(d, 0, (size_t)n * 8);
        uint64_t index;
        if (!rds_index(s, args[1], im[0], &index)) return s->strict ? rds_fail(s, "vector index out of range") : 0;
        if (o->code == RDS_INDEX) rds_copy_bits(d, 0, a, (uint32_t)(index * im[1]), w);
        else {
            memcpy(d, a, (size_t)n * 8);
            rds_copy_bits(d, (uint32_t)(index * im[1]), rds_data(s, args[2]), 0, (uint32_t)im[1]);
        }
        break;
    }
    case RDS_WRITE_SET: {
        memcpy(d, a, (size_t)n * 8);
        uint64_t *indices = rds_data(s, args[2]), *data = rds_data(s, args[3]);
        for (uint32_t i = 0; i < im[2]; ++i) if (bit(b, i)) {
            uint64_t index = 0;
            rds_copy_bits(&index, 0, indices, (uint32_t)(i * im[3]), (uint32_t)im[3]);
            if (index >= im[0]) {
                memset(d, 0, (size_t)n * 8);
                return s->strict ? rds_fail(s, "vector write index out of range") : 0;
            }
            for (uint32_t j = 0; j < i; ++j) if (bit(b, j)) {
                uint64_t previous = 0;
                rds_copy_bits(&previous, 0, indices, (uint32_t)(j * im[3]), (uint32_t)im[3]);
                if (index == previous) {
                    memset(d, 0, (size_t)n * 8);
                    return s->strict ? rds_fail(s, "vector write collision") : 0;
                }
            }
            rds_copy_bits(d, (uint32_t)(index * im[1]), data, (uint32_t)(i * im[1]), (uint32_t)im[1]);
        }
        break;
    }
    case RDS_READ: {
        rds_mem *m = &s->mems[im[0]]; uint64_t index;
        if (!rds_index(s, args[0], m->depth, &index)) {
            memset(d, 0, (size_t)n * 8);
            return s->strict ? rds_fail(s, "memory read address out of range") : 0;
        }
        memcpy(d, m->data + index * m->words, (size_t)n * 8); break;
    }
    case RDS_DECODE: {
        uint32_t an = s->values[args[0]].words;
        memcpy(d, im + 1, (size_t)n * 8);
        const uint64_t *row = im + 1 + n;
        for (uint64_t i = 0; i < im[0]; ++i, row += 2 * an + n) {
            bool match = true;
            for (uint32_t j = 0; j < an; ++j) if ((a[j] & row[an + j]) != row[j]) match = false;
            if (match) { memcpy(d, row + 2 * an, (size_t)n * 8); break; }
        }
        break;
    }
    default: return rds_fail(s, "unknown operation");
    }
    rds_trim(d, w); return 0;
}
int rds_eval(rds_sim *s) {
    s->error[0] = 0; s->evaluated = false;
    if (s->schedule) {
        if (rds_schedule_eval(s)) return -1;
    } else for (uint32_t i = 0; i < s->no; ++i) if (rds_execute(s, &s->ops[i])) return -1;
    s->evaluated = true; return 0;
}
static bool active(rds_sim *s, uint32_t value) { return rds_data(s, value)[0] != 0; }
int rds_advance(rds_sim *s) {
    if (rds_is_compiled(s)) return rds_compiled_advance(s);
    if (!s->evaluated && rds_eval(s)) return -1;
    for (uint32_t i = 0; i < s->nc; ++i) {
        rds_assert *c = &s->checks[i];
        if (!active(s, c->reset) && active(s, c->guard) && !active(s, c->condition)) {
            snprintf(s->error, sizeof s->error, "assertion at cycle %llu: %s", (unsigned long long)s->cycles, c->name);
            return -1;
        }
    }
    /* Validate all effects before mutating any hardware state. */
    for (uint32_t i = 0; i < s->nw; ++i) {
        rds_write *r = &s->writes[i]; uint64_t index;
        if (!active(s, r->enable)) continue;
        if (!rds_index(s, r->address, s->mems[r->mem].depth, &index)) return rds_fail(s, "memory write address out of range");
        for (uint32_t j = 0; j < i; ++j) {
            rds_write *other = &s->writes[j];
            if (other->mem == r->mem && active(s, other->enable) && rds_data(s, other->address)[0] == index)
                return rds_fail(s, "memory write collision");
        }
    }
    for (uint32_t i = 0; i < s->nr; ++i) {
        rds_reg *r = &s->regs[i];
        uint32_t source = r->reset != RDS_NONE && active(s, r->reset) ? r->reset_value : r->d;
        memcpy(s->next + r->next, rds_data(s, source), (size_t)s->values[r->q].words * 8);
    }
    for (uint32_t i = 0; i < s->ns; ++i) {
        rds_read *r = &s->reads[i]; rds_mem *m = &s->mems[r->mem];
        uint64_t index;
        const uint64_t *source = rds_data(s, r->q);
        if (active(s, r->enable) && (r->write == RDS_NONE || !active(s, r->write))) {
            if (!rds_index(s, r->address, m->depth, &index)) return rds_fail(s, "synchronous read address out of range");
            for (uint32_t j = 0; j < s->nw; ++j) {
                rds_write *write = &s->writes[j];
                if (write->mem == r->mem && active(s, write->enable) && rds_data(s, write->address)[0] == index)
                    return rds_fail(s, "synchronous memory read/write collision");
            }
            source = m->data + index * m->words;
        }
        memcpy(s->next + r->next, source, (size_t)m->words * 8);
    }
    if (rds_objects_prepare(s)) return -1;
    /* Deferred writes still read the old value arena, including register data. */
    for (uint32_t i = 0; i < s->nw; ++i) {
        rds_write *r = &s->writes[i];
        if (!active(s, r->enable)) continue;
        rds_mem *m = &s->mems[r->mem];
        uint64_t *dst = m->data + rds_data(s, r->address)[0] * m->words;
        uint64_t *src = rds_data(s, r->data);
        if (r->mask == RDS_NONE) memcpy(dst, src, (size_t)m->words * 8);
        else for (uint32_t j = 0; j < m->width / r->granule; ++j)
            if (bit(rds_data(s, r->mask), j)) rds_copy_bits(dst, j * r->granule, src, j * r->granule, r->granule);
    }
    rds_objects_commit(s);
    for (uint32_t i = 0; i < s->nr; ++i) {
        rds_reg *r = &s->regs[i];
        memcpy(rds_data(s, r->q), s->next + r->next, (size_t)s->values[r->q].words * 8);
    }
    for (uint32_t i = 0; i < s->ns; ++i) {
        rds_read *r = &s->reads[i];
        memcpy(rds_data(s, r->q), s->next + r->next, (size_t)s->values[r->q].words * 8);
    }
    rds_finish_cycle(s); return 0;
}
void rds_finish_cycle(rds_sim *s) {
    if (s->object_trace) rds_compiled_sync_objects(s,true);
    if (s->object_trace) for (uint32_t i=0;i<s->nx;++i) {
        rds_object *o=&s->objects[i];
        s->object_trace(s->trace_context,s->cycles,o->name,o->kind,o->count,o->enqueue,o->dequeue);
    }
    ++s->cycles; s->evaluated = false;
}
