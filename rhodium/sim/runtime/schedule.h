/* Shares load-time plans and fixed worker entries with the C compiler. */
#ifndef RDS_SCHEDULE_H
#define RDS_SCHEDULE_H
#include "internal.h"
#include <pthread.h>
#include <stdatomic.h>
typedef struct { _Atomic uint64_t epoch; unsigned char padding[56]; } completion;
/* Scalar instructions use word offsets; shared batches carry opcode and width.
 * A generic instruction stores its original operation index in out. */
typedef struct { uint32_t out, a, b, c; } instruction;
typedef struct { uint32_t code, width, first, count; } batch;
typedef struct {
    pthread_t thread;
    rds_schedule *plan;
    rds_sim view;
    uint32_t first, count, id;
    uint32_t *objects, object_count;
    int status, prepared;
    char prepare_error[512];
} worker;
typedef int (*rds_phase_fn)(rds_compiled_context *);
typedef struct { size_t offset; uint32_t words; } constant_span;
/* Union descriptors contain flat literals, never other descriptors. */
#define RDS_GUARD_OR UINT32_C(0x40000000)
enum { RDS_GUARD_OR_LIMIT = 4096, RDS_GUARD_OR_TERMS = 4 };
typedef struct { uint32_t count, atoms[RDS_GUARD_OR_TERMS]; } rds_guard_or;
typedef struct rds_offer_plan rds_offer_plan;
struct rds_schedule {
    rds_offer_plan *offers;
    void *compiled_handle;
    void *compiled_hot;
    size_t compiled_hot_bytes;
    void (*compiled_objects)(rds_compiled_context *, bool);
    uint64_t **compiled_memories;
    rds_phase_fn *compiled_entries;
    uint64_t compiled_key, compiled_layout_key;
    uint32_t scheduled_operations;
    uint32_t tail_first, tail_count;
    uint64_t affinity_before, affinity_after;
    uint32_t affinity_swaps;
    bool plan_released;
    uint32_t compiled_blocks, compiled_shapes;
    size_t compiled_bytes;
    constant_span *constants;
    uint32_t constant_count;
    rds_sim *sim;
    instruction *instructions;
    uint32_t *instruction_origins;
    uint32_t *instruction_guards;
    rds_guard_or *guard_unions;
    uint32_t guard_union_count;
    uint32_t *instruction_cache;
    bool *cache_objects;
    bool *cache_lanes;
    bool *cache_fields;
    uint64_t *field_masks;
    bool *flow_cache_objects;
    batch *batches;
    worker *workers;
    uint32_t count, created, phase;
    pthread_barrier_t start, done;
    pthread_mutex_t gate;
    pthread_cond_t gate_changed;
    int ready;
    bool stop, synchronization, prepare_local;
    bool spin;
    _Alignas(64) _Atomic uint64_t epoch;
    unsigned char epoch_padding[56];
    completion *completions;
    completion *bulk_progress;
    _Atomic uint64_t bulk_failure_phase;
    uint64_t bulk_cycles, bulk_completed;
    bool bulk_safe, bulk_local, compiled_bulk_fused, offers_ready;
};
/* Applies only to demand guards; cache tokens belong to code generation. */
static inline const uint32_t *rds_guard_atoms(const rds_schedule *p,
                                             const uint32_t *guard, uint32_t *count) {
    if (*guard & RDS_GUARD_OR) {
        const rds_guard_or *u = &p->guard_unions[*guard & ~RDS_GUARD_OR];
        *count = u->count;
        return u->atoms;
    }
    *count = *guard != 0;
    return guard;
}
enum { SELECT = RDS_OPCODE_COUNT, SLICE_CROSS, GENERIC = UINT32_MAX };

void rds_compiled_free(rds_sim *s);
int rds_compiled_call(rds_sim *s, uint32_t lane, uint32_t phase);
int rds_semantic_regions(rds_sim *, uint32_t *, uint32_t, const uint32_t *,
                         const uint32_t *, bool *, uint32_t *);
int rds_stable_payloads(rds_sim *, uint32_t *, uint32_t, bool *, uint32_t *, bool *, bool *);
int rds_flow_cache(rds_sim *, uint32_t *, uint32_t, bool *, uint32_t *, const uint32_t *);
rds_offer_plan *rds_offer_build(const rds_sim *);
void rds_offer_free(rds_offer_plan *);
uint64_t rds_offer_key(const rds_sim *, uint64_t);
void rds_offer_report(FILE *, const rds_sim *);
#endif
