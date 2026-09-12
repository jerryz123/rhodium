/* Shares binary-model records and storage helpers between native runtime units. */
#ifndef RDS_INTERNAL_H
#define RDS_INTERNAL_H
#include "include/rhodium_sim.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RDS_NONE UINT32_MAX
#define RDS_VERSION 4
enum rds_opcode {
    RDS_CONST, RDS_COPY, RDS_NOT, RDS_AND, RDS_OR, RDS_XOR,
    RDS_ADD, RDS_SUB, RDS_MUL, RDS_SHL, RDS_SHRU, RDS_SHRS,
    RDS_EQ, RDS_ULT, RDS_SLT, RDS_MUX, RDS_ONEHOT, RDS_SLICE,
    RDS_ZEXT, RDS_SEXT, RDS_PACK, RDS_INDEX, RDS_INJECT,
    RDS_WRITE_SET, RDS_READ, RDS_DECODE, RDS_SET_CLEAR, RDS_BALANCE, RDS_COUNTER_STEP, RDS_OBJECT_QUERY, RDS_ALU, RDS_BYTE_MERGE, RDS_CONTRACT, RDS_ONEHOT_VIEW, RDS_OPCODE_COUNT
};
typedef struct { uint32_t width, words; size_t offset, state; uint32_t origin; } rds_value;
typedef struct { uint32_t code, out, args, nargs, imm, nimm; } rds_op;
typedef struct { uint32_t q, d, reset, reset_value; size_t next; uint32_t owner; } rds_reg;
typedef struct { uint32_t width, depth, words; uint64_t *data; } rds_mem;
typedef struct { uint32_t mem, address, data, enable, mask, granule; } rds_write;
typedef struct { uint32_t mem, address, enable, q, write; size_t next; } rds_read;
typedef struct { uint32_t condition, reset, guard; char *name; } rds_assert;
typedef struct { uint32_t direction, value; char *name; } rds_port;
typedef struct rds_schedule rds_schedule;
#include "compiled.h"
typedef void (*rds_set_clear_fn)(uint64_t *, const uint64_t *, const uint64_t *, const uint64_t *, size_t);
struct rds_sim {
    uint32_t nv, no, na, ni, nr, nm, nw, ns, nc, np;
    uint32_t source_nv, source_no;
    uint32_t nx;
    rds_object *objects;
    size_t object_bytes, payload_bytes;
    rds_object_trace_fn object_trace;
    void *trace_context;
    rds_value *values;
    rds_op *ops;
    uint32_t *args;
    uint64_t *imm;
    rds_reg *regs;
    rds_mem *mems;
    rds_write *writes;
    rds_read *reads;
    rds_assert *checks;
    rds_port *ports;
    uint64_t *arena, *next, *current;
    bool copy_state, shared_code, parallel_publish;
    unsigned inline_bodies;
    bool linear_decode, eager_combinational, materialize_state, wide_regions, select_regions, guarded_onehot, cold_regions, flow_prepare, lift_transitions, lift_primitives;
    size_t value_words, next_words, memory_bytes, model_bytes;
    uint32_t *emit_offsets, *emit_constants, *emit_destination;
    bool *emit_used, *emit_small;
    bool emit_private; /* Emission-only SSA arrays inside a contract program. */
    size_t descriptor_bytes, schedule_bytes;
    uint32_t workers, components, batches;
    rds_schedule *schedule;
    uint32_t *partition_owners, partition_workers, replicated_ops;
    uint64_t partition_work, partition_peak;
    const char *partition_reason;
    uint64_t partition_candidate_work, partition_candidate_bytes, partition_baseline;
    uint32_t partition_candidate_workers, partition_remaining_workers;
    rds_set_clear_fn set_clear;
    uint64_t cycles;
    bool evaluated;
    bool strict;
    char error[512];
    uint64_t source_key; /* Immutable binary-image provenance; never queried per cycle. */
};
uint64_t rds_emission_key(const rds_sim *s);
static inline uint64_t *rds_data(rds_sim *s, uint32_t id) {
    rds_value *v = &s->values[id];
    return s->current && v->state != SIZE_MAX ? s->current + v->state : s->arena + v->offset;
}
int rds_compiled_advance(rds_sim *s);
bool rds_is_compiled(const rds_sim *s);
void rds_finish_cycle(rds_sim *s);
void rds_compiled_sync_objects(rds_sim *s, bool publish);
static inline uint64_t rds_mask(uint32_t width) {
    return width % 64 ? (UINT64_C(1) << (width % 64)) - 1 : UINT64_MAX;
}
static inline uint64_t *rds_state_bank(size_t words) {
    if(words>(SIZE_MAX-63)/8)return NULL;
    size_t bytes=words?(words*8+63)&~(size_t)63:64;
    uint64_t *p=aligned_alloc(64,bytes);
    if(p)memset(p,0,bytes);
    return p;
}
static inline void rds_trim(uint64_t *p, uint32_t width) {
    p[(width - 1) / 64] &= rds_mask(width);
}
int rds_fail(rds_sim *s, const char *message);
int rds_execute(rds_sim *s, const rds_op *op);
bool rds_validate_graph(rds_sim *s, uint32_t inputs);
bool rds_kernel_validate(const rds_sim *s, const rds_op *op);
int rds_kernel_execute(rds_sim *s, const rds_op *op);
uint64_t rds_kernel_cost(const rds_sim *s, const rds_op *op);
void rds_c_kernel(FILE *f, const rds_sim *s, const rds_op *op);
void rds_c_kernel_local(FILE *f, const rds_sim *s, const rds_op *op);
bool rds_c_kernel_cacheable(const rds_sim *s, const rds_op *op);
void rds_copy_bits(uint64_t *dst, uint32_t dst_bit, const uint64_t *src,
                   uint32_t src_bit, uint32_t width);
bool rds_index(rds_sim *s, uint32_t id, uint64_t limit, uint64_t *index);
int rds_schedule_build(rds_sim *s, const rds_options *options);
int rds_partition(rds_sim *s, uint32_t requested, uint32_t flags);
uint64_t rds_operation_cost(const rds_sim *s, const rds_op *op);
int rds_schedule_eval(rds_sim *s);
void rds_schedule_free(rds_sim *s);
int rds_objects_init(rds_sim *s);
int rds_object_query(rds_sim *s, const rds_op *op);
int rds_objects_prepare(rds_sim *s);
void rds_objects_commit(rds_sim *s);
void rds_objects_free(rds_sim *s);
int rds_objects_prepare_range(rds_sim *s, uint32_t first, uint32_t stride);
int rds_object_prepare(rds_sim *s, uint32_t object);
void rds_object_commit(rds_sim *s, uint32_t object);
void rds_objects_commit_range(rds_sim *s, uint32_t first, uint32_t stride);
int rds_schedule_objects(rds_sim *s, unsigned phase);
void rds_set_clear_c(uint64_t *restrict dst, const uint64_t *restrict state,
                     const uint64_t *restrict set, const uint64_t *restrict clear, size_t words);
#ifdef RDS_HAVE_X86_64_ASM
void rds_set_clear_x86_64(uint64_t *, const uint64_t *, const uint64_t *, const uint64_t *, size_t);
#endif
#endif
