/* Assigns update cones to workers with bounded replication of pure snapshot work. */
#include "internal.h"
#include <limits.h>

typedef struct {
    uint32_t **refs, count, object, lane;
    uint64_t *cone, cost;
} sink;

/* Costs describe execution work, not RTL area. Fixed projections are cheap;
 * wide copies, searches, and native payload queries carry their word traffic. */
uint64_t rds_operation_cost(const rds_sim *s, const rds_op *o) {
    uint64_t n = s->values[o->out].words;
    switch (o->code) {
    case RDS_CONTRACT: return rds_kernel_cost(s,o);
    case RDS_CONST: return 0;
    case RDS_SLICE: return n + 1;
    case RDS_PACK: return n + 2 * o->nargs;
    case RDS_MUL: return 3 * n * n;
    case RDS_MUX: return n + o->nargs;
    case RDS_DECODE: return !s->linear_decode && s->values[s->args[o->args]].width<=8 && s->values[o->out].width<=64 ? 3 : n + s->imm[o->imm] * 2;
    case RDS_ALU: return 12;
    case RDS_BYTE_MERGE: return 8;
    case RDS_OBJECT_QUERY: return 3 + n + o->nargs;
    case RDS_READ: return 8 + n;
    default: return n + o->nargs;
    }
}
static int descending(const void *a, const void *b) {
    const sink *x = a, *y = b;
    if (x->cost != y->cost) return x->cost > y->cost ? -1 : 1;
    return x->object < y->object ? -1 : x->object != y->object;
}
static uint32_t find(uint32_t *parent, uint32_t i) {
    while (parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; }
    return i;
}
static bool contains(const uint64_t *set, uint32_t id) { return (set[id / 64] >> (id % 64)) & 1; }
static void insert(uint64_t *set, uint32_t id) { set[id / 64] |= UINT64_C(1) << (id % 64); }

/* Merging satisfies replication budgets but can leave uneven surviving lanes.
 * Move complete sinks, retaining exact query dependencies and unique effects.
 * A bounded local search strictly improves (peak work, total work, bytes). */
static void rebalance(const rds_sim *s, sink *sinks, uint32_t count,
                      uint64_t *lanes, size_t words, uint32_t active,
                      const uint64_t *weight, uint64_t work_limit,
                      uint64_t byte_limit) {
    if ((size_t)active*s->no > (64u*1024u*1024u)/sizeof(uint32_t)) return;
    uint32_t *uses=calloc((size_t)active*(s->no?s->no:1),sizeof *uses);
    if (!uses) return;
    uint64_t loads[256]={0},work=0,bytes=0;
    for (uint32_t j=0;j<count;++j) for (uint32_t i=0;i<s->no;++i)
        if (contains(sinks[j].cone,i)) ++uses[(size_t)sinks[j].lane*s->no+i];
    for (uint32_t i=0;i<s->no;++i) {
        uint32_t copies=0;
        for (uint32_t lane=0;lane<active;++lane) if (uses[(size_t)lane*s->no+i]) {
            ++copies;loads[lane]+=weight[i];work+=weight[i];
        }
        if (copies>1) bytes+=(copies-1)*(sizeof(rds_op)+sizeof(rds_value)+4ull*s->ops[i].nargs+8ull*s->ops[i].nimm);
    }
    for (unsigned round=0;round<32;++round) {
        uint64_t peak=0;
        for (uint32_t lane=0;lane<active;++lane) if (loads[lane]>peak) peak=loads[lane];
        uint64_t best_peak=peak,best_work=work,best_bytes=bytes,best_remove=0,best_add=0;
        uint32_t chosen=RDS_NONE,target=0;
        for (uint32_t j=0;j<count;++j) {
            const sink *t=&sinks[j];uint32_t from=t->lane;
            if (loads[from]!=peak) continue;
            uint64_t removed=0,removed_bytes=0;
            for (uint32_t i=0;i<s->no;++i) if (contains(t->cone,i)&&uses[(size_t)from*s->no+i]==1) {
                removed+=weight[i];
                removed_bytes+=sizeof(rds_op)+sizeof(rds_value)+4ull*s->ops[i].nargs+8ull*s->ops[i].nimm;
            }
            if (!removed||removed==loads[from]) continue;
            for (uint32_t to=0;to<active;++to) {
                if (to==from||loads[to]>=loads[from]) continue;
                uint64_t added=0,added_bytes=0;
                for (uint32_t i=0;i<s->no;++i) if (contains(t->cone,i)&&!uses[(size_t)to*s->no+i]) {
                    added+=weight[i];
                    added_bytes+=sizeof(rds_op)+sizeof(rds_value)+4ull*s->ops[i].nargs+8ull*s->ops[i].nimm;
                }
                uint64_t next_work=work-removed+added,next_bytes=bytes+added_bytes-removed_bytes;
                if (next_work>work_limit||next_bytes>byte_limit) continue;
                uint64_t next_peak=loads[from]-removed;
                for (uint32_t lane=0;lane<active;++lane) if (lane!=from) {
                    uint64_t load=loads[lane]+(lane==to?added:0);
                    if (load>next_peak) next_peak=load;
                }
                if (next_peak<best_peak||(next_peak==best_peak&&(next_work<best_work||
                    (next_work==best_work&&next_bytes<best_bytes)))) {
                    chosen=j;target=to;best_peak=next_peak;best_work=next_work;best_bytes=next_bytes;
                    best_remove=removed;best_add=added;
                }
            }
        }
        if (chosen==RDS_NONE) break;
        sink *t=&sinks[chosen];uint32_t from=t->lane;
        for (uint32_t i=0;i<s->no;++i) if (contains(t->cone,i)) {
            if (!--uses[(size_t)from*s->no+i]) lanes[(size_t)from*words+i/64]&=~(UINT64_C(1)<<(i%64));
            ++uses[(size_t)target*s->no+i];insert(lanes+(size_t)target*words,i);
        }
        t->lane=target;loads[from]-=best_remove;loads[target]+=best_add;work=best_work;bytes=best_bytes;
    }
    free(uses);
}

/* Flow interactions are already represented by each query's exact operands:
 * reverse ready, bypass valid/data, and arbitration dependencies remain edges.
 * All update inputs of one object form one sink, but a whole named flow is
 * deliberately not an indivisible partition. Expensive overlap favors locality. */
int rds_partition(rds_sim *s, uint32_t requested, uint32_t flags) {
    s->partition_reason="disabled";
    if (requested < 2 || (flags & (RDS_REFERENCE | RDS_NO_PARTITION))) return 0;
    size_t words = ((size_t)s->no + 63) / 64;
    size_t capacity = (size_t)s->nx + s->nr + s->nw + s->ns + s->nc + s->np + 1;
    /* Bound construction scratch as well as resident replication. Large graphs
     * can retain the component scheduler instead of exhausting host memory. */
    s->partition_reason="construction-budget";
    if (!words || capacity > (64u * 1024u * 1024u) / 8 / words) return 0;
    s->partition_reason="allocation";
    sink *sinks = calloc(capacity, sizeof *sinks);
    uint64_t *sets = calloc(capacity * words, 8), *lanes = calloc(requested * words, 8);
    uint64_t *covered = calloc(words, 8), *weight = calloc(s->no ? s->no : 1, 8);
    uint32_t *producer = malloc((s->nv ? s->nv : 1) * sizeof *producer);
    uint32_t *parent = malloc((s->no ? s->no : 1) * sizeof *parent);
    uint64_t *components = calloc(s->no ? s->no : 1, 8);
    uint32_t count = 0, active = requested;
    uint64_t total = 0, base_bytes = 0, loads[256] = {0};
    int result = -1;
    if (!sinks || !sets || !lanes || !covered || !weight || !producer || !parent || !components) goto done;
    for (uint32_t i = 0; i < s->nv; ++i) producer[i] = RDS_NONE;
    for (uint32_t i = 0; i < s->no; ++i) {
        parent[i] = i; weight[i] = rds_operation_cost(s, &s->ops[i]); total += weight[i];
        base_bytes += sizeof(rds_op) + sizeof(rds_value) + 4ull * s->ops[i].nargs + 8ull * s->ops[i].nimm;
        if (s->ops[i].code != RDS_CONST) producer[s->ops[i].out] = i;
    }
#define SINK(n, object_id) do { \
    sink *t = &sinks[count]; t->count = (n); t->object = (object_id); t->cone = sets + count * words; \
    t->refs = calloc(t->count ? t->count : 1, sizeof *t->refs); \
    ++count; if (!t->refs) goto done; \
} while (0)
#define REF(j, field) sinks[count - 1].refs[j] = &(field)
    for (uint32_t i = 0; i < s->nx; ++i) {
        SINK(s->objects[i].ni, i);
        for (uint32_t j = 0; j < s->objects[i].ni; ++j) REF(j, s->objects[i].inputs[j]);
    }
    for (uint32_t i = 0; i < s->nr; ++i) {
        SINK(3, RDS_NONE); REF(0, s->regs[i].d); REF(1, s->regs[i].reset); REF(2, s->regs[i].reset_value);
    }
    for (uint32_t i = 0; i < s->nw; ++i) {
        SINK(4, RDS_NONE); REF(0, s->writes[i].address); REF(1, s->writes[i].data);
        REF(2, s->writes[i].enable); REF(3, s->writes[i].mask);
    }
    for (uint32_t i = 0; i < s->ns; ++i) {
        SINK(3, RDS_NONE); REF(0, s->reads[i].address); REF(1, s->reads[i].enable); REF(2, s->reads[i].write);
    }
    for (uint32_t i = 0; i < s->nc; ++i) {
        SINK(3, RDS_NONE); REF(0, s->checks[i].condition); REF(1, s->checks[i].reset); REF(2, s->checks[i].guard);
    }
    for (uint32_t i = 0; i < s->np; ++i) { SINK(1, RDS_NONE); REF(0, s->ports[i].value); }
    for (uint32_t j = 0; j < count; ++j) {
        sink *t = &sinks[j];
        for (uint32_t k = 0; k < t->count; ++k) {
            uint32_t v = *t->refs[k];
            if (v != RDS_NONE && producer[v] != RDS_NONE) insert(t->cone, producer[v]);
        }
        /* Serialized definitions are topological, so reverse traversal closes
         * each cone without recursion or an execution-time dependency queue. */
        for (uint32_t i = s->no; i--;) if (contains(t->cone, i)) {
            rds_op *o = &s->ops[i]; t->cost += weight[i]; insert(covered, i);
            for (uint32_t k = 0; k < o->nargs; ++k) {
                uint32_t p = producer[s->args[o->args + k]];
                if (p != RDS_NONE) insert(t->cone, p);
            }
        }
    }
    /* Keep otherwise unobserved expressions for eager strict-mode diagnostics. */
    SINK(0, RDS_NONE);
    for (uint32_t i = 0; i < s->no; ++i) if (weight[i] && !contains(covered, i)) insert(sinks[count - 1].cone, i);
    for (uint32_t i = s->no; i--;) if (contains(sinks[count - 1].cone, i)) {
        rds_op *o = &s->ops[i]; sinks[count - 1].cost += weight[i];
        for (uint32_t k = 0; k < o->nargs; ++k) {
            uint32_t p = producer[s->args[o->args + k]];
            if (p != RDS_NONE) insert(sinks[count - 1].cone, p);
        }
    }
#undef REF
#undef SINK
    /* Baseline is the existing component assignment with the same cost model. */
    for (uint32_t i = 0; i < s->no; ++i) for (uint32_t k = 0; k < s->ops[i].nargs; ++k) {
        uint32_t p = producer[s->args[s->ops[i].args + k]];
        if (p != RDS_NONE) parent[find(parent, i)] = find(parent, p);
    }
    for (uint32_t i = 0; i < s->no; ++i) components[find(parent, i)] += weight[i];
    uint64_t baseline = 0;
    for (uint32_t i = 0; i < s->no; ++i) if (components[i] > baseline) baseline = components[i];
    if (total / requested > baseline) baseline = total / requested;
    s->partition_baseline=baseline;
    qsort(sinks, count, sizeof *sinks, descending);
    for (uint32_t j = 0; j < count; ++j) {
        sink *t = &sinks[j]; uint64_t best_peak = UINT64_MAX, best_extra = UINT64_MAX;
        for (uint32_t lane = 0; lane < requested; ++lane) {
            uint64_t extra = 0;
            for (uint32_t i = 0; i < s->no; ++i)
                if (contains(t->cone, i) && !contains(lanes + lane * words, i)) extra += weight[i];
            uint64_t peak = loads[lane] + extra;
            for (uint32_t other = 0; other < requested; ++other) if (loads[other] > peak) peak = loads[other];
            if (peak < best_peak || (peak == best_peak && extra < best_extra)) {
                best_peak = peak; best_extra = extra; t->lane = lane;
            }
        }
        for (size_t k = 0; k < words; ++k) lanes[t->lane * words + k] |= t->cone[k];
        loads[t->lane] += best_extra;
    }
    /* Merge the most overlapping pair until extra work and descriptors fit
     * the 25% budget (100% with MORE_REPLICATION), also capped at 8 MiB. These are hard
     * acceptance budgets, not a claimed speedup from a graph cost estimate. */
    uint64_t divisor = (flags & RDS_MORE_REPLICATION) ? 1 : 4;
    uint64_t extra_limit = base_bytes / divisor;
    if (extra_limit > 8u * 1024u * 1024u) extra_limit = 8u * 1024u * 1024u;
    for (;;) {
        uint64_t work = 0, bytes = 0;
        for (uint32_t i = 0; i < s->no; ++i) if (weight[i]) {
            uint32_t copies = 0;
            for (uint32_t lane = 0; lane < active; ++lane) copies += contains(lanes + lane * words, i);
            work += copies * weight[i];
            if (copies > 1) bytes += (copies - 1) * (sizeof(rds_op) + sizeof(rds_value) + 4ull * s->ops[i].nargs + 8ull * s->ops[i].nimm);
        }
        if(!s->partition_candidate_workers){s->partition_candidate_workers=active;s->partition_candidate_work=work;s->partition_candidate_bytes=bytes;}
        s->partition_remaining_workers=active;
        if (work <= total + total / divisor && bytes <= extra_limit) break;
        if (active < 2) { result = 0; goto done; }
        uint32_t left = 0, right = 1; uint64_t best = UINT64_MAX;
        for (uint32_t a = 0; a < active; ++a) for (uint32_t b = a + 1; b < active; ++b) {
            uint64_t cost = 0;
            for (uint32_t i = 0; i < s->no; ++i)
                if (contains(lanes + a * words, i) || contains(lanes + b * words, i)) cost += weight[i];
            if (cost < best) { best = cost; left = a; right = b; }
        }
        for (size_t k = 0; k < words; ++k) lanes[left * words + k] |= lanes[right * words + k];
        --active;
        if (right != active) memcpy(lanes + right * words, lanes + active * words, words * 8);
        for (uint32_t j = 0; j < count; ++j) {
            if (sinks[j].lane == right) sinks[j].lane = left;
            else if (sinks[j].lane == active) sinks[j].lane = right;
        }
    }
    if (flags&RDS_BALANCE_PARTITIONS)
        rebalance(s,sinks,count,lanes,words,active,weight,total+total/divisor,extra_limit);
    uint64_t peak = 0, work = 0;
    for (uint32_t lane = 0; lane < active; ++lane) {
        uint64_t cost = 0;
        for (uint32_t i = 0; i < s->no; ++i) if (contains(lanes + lane * words, i)) cost += weight[i];
        if (cost > peak) peak = cost;
        work += cost;
    }
    if (active < 2 || peak > baseline || (size_t)active * s->nv > (64u * 1024u * 1024u) / sizeof(uint32_t)) {
        s->partition_reason=active<2?"replication-budget":peak>baseline?"peak-work-limit":"mapping-budget";
        result = 0; goto done;
    }
    /* Each worker gets private computed values; constants and old state share
     * source IDs. Only sink bindings select a worker copy. No state is cloned. */
    uint64_t nops = 0, nargs = 0, nvalues = 0;
    for (uint32_t v = 0; v < s->nv; ++v) if (producer[v] == RDS_NONE) ++nvalues;
    for (uint32_t i = 0; i < s->no; ++i) {
        if (!weight[i]) { ++nops; nargs += s->ops[i].nargs; }
        else for (uint32_t lane = 0; lane < active; ++lane) if (contains(lanes + lane * words, i)) {
            ++nops; ++nvalues; nargs += s->ops[i].nargs;
        }
    }
    if (nops > UINT32_MAX || nargs > UINT32_MAX || nvalues > UINT32_MAX) goto done;
    rds_value *values = calloc(nvalues ? nvalues : 1, sizeof *values);
    rds_op *ops = calloc(nops ? nops : 1, sizeof *ops);
    uint32_t *args = calloc(nargs ? nargs : 1, sizeof *args), *owners = calloc(nops ? nops : 1, sizeof *owners);
    uint32_t *map = malloc((size_t)active * s->nv * sizeof *map);
    if (!values || !ops || !args || !owners || !map) { free(values); free(ops); free(args); free(owners); free(map); goto done; }
    memset(map, 255, (size_t)active * s->nv * sizeof *map);
    uint32_t vi = 0, oi = 0, ai = 0; size_t arena_words = 0;
    for (uint32_t v = 0; v < s->nv; ++v) if (producer[v] == RDS_NONE) {
        values[vi] = s->values[v]; values[vi].offset = arena_words; arena_words += values[vi].words;
        for (uint32_t lane = 0; lane < active; ++lane) map[(size_t)lane * s->nv + v] = vi;
        ++vi;
    }
    for (uint32_t i = 0; i < s->no; ++i) if (!weight[i]) { ops[oi] = s->ops[i]; ops[oi++].out = map[s->ops[i].out]; }
    for (uint32_t lane = 0; lane < active; ++lane) for (uint32_t i = 0; i < s->no; ++i) if (contains(lanes + lane * words, i)) {
        rds_op *o = &s->ops[i]; uint32_t *m = map + (size_t)lane * s->nv;
        values[vi] = s->values[o->out]; values[vi].offset = arena_words; arena_words += values[vi].words;
        m[o->out] = vi++;
        ops[oi] = *o; ops[oi].out = m[o->out]; ops[oi].args = ai; owners[oi++] = lane;
        for (uint32_t j = 0; j < o->nargs; ++j) args[ai++] = m[s->args[o->args + j]];
    }
    uint64_t *arena = calloc(arena_words ? arena_words : 1, 8);
    if (!arena) { free(values); free(ops); free(args); free(owners); free(map); goto done; }
    /* Remap source definitions once; update/output fields belong to sinks. */
    for (uint32_t i = 0; i < s->nr; ++i) s->regs[i].q = map[s->regs[i].q];
    for (uint32_t i = 0; i < s->ns; ++i) s->reads[i].q = map[s->reads[i].q];
    for (uint32_t j = 0; j < count; ++j) {
        sink *t = &sinks[j];
        for (uint32_t k = 0; k < t->count; ++k) if (*t->refs[k] != RDS_NONE)
            *t->refs[k] = map[(size_t)t->lane * s->nv + *t->refs[k]];
        if (t->object != RDS_NONE) s->objects[t->object].owner = t->lane;
    }
    s->replicated_ops = (uint32_t)nops - s->no; s->partition_work = work; s->partition_peak = peak;
    s->partition_workers = active; s->partition_owners = owners;
    free(s->values); free(s->ops); free(s->args); free(s->arena); free(map);
    s->values = values; s->ops = ops; s->args = args; s->arena = arena;
    s->nv = (uint32_t)nvalues; s->no = (uint32_t)nops; s->na = (uint32_t)nargs; s->value_words = arena_words;
    s->partition_reason="accepted";result = 1;
done:
    if (sinks) for (uint32_t j = 0; j < count; ++j) free(sinks[j].refs);
    free(sinks); free(sets); free(lanes); free(covered); free(weight); free(producer); free(parent); free(components);
    return result;
}
