/* Defines the native Rhodium simulator's model, port, and cycle API. */
#ifndef RHODIUM_SIM_H
#define RHODIUM_SIM_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct rds_sim rds_sim;
typedef struct {
    size_t model_bytes, value_bytes, memory_bytes, update_bytes;
    uint32_t values, operations, registers, memories, ports;
    uint64_t cycles;
    size_t descriptor_bytes, schedule_bytes;
    uint32_t workers, components, batches;
    uint32_t objects;
    size_t object_bytes, payload_bytes;
} rds_stats;
enum { RDS_REFERENCE = 1u, RDS_NO_REUSE = 2u, RDS_NO_BATCH = 4u, RDS_USE_ASM = 8u,
       RDS_NO_REORDER = 16u, RDS_NO_PARTITION = 32u, RDS_SPIN = 64u,
       RDS_COPY_STATE = 128u, RDS_SHARED_CODE = 256u, RDS_MORE_REPLICATION = 512u, RDS_PARALLEL_PUBLISH = 1024u,
       RDS_INLINE_BODIES = 2048u, RDS_AUTO_INLINE = 4096u, RDS_LINEAR_DECODE = 8192u,
       RDS_PARALLEL_STATE = 16384u, RDS_EAGER_COMBINATIONAL = 32768u, RDS_DIRECT_STATE_PACKS = 65536u, RDS_COALESCE_SCRATCH = 131072u, RDS_WIDE_REGIONS = 262144u, RDS_SELECT_REGIONS = 524288u, RDS_SEMANTIC_REGIONS = 1048576u, RDS_STABLE_PAYLOADS = 2097152u, RDS_BALANCE_PARTITIONS = 4194304u, RDS_STABLE_FIELDS = 8388608u, RDS_FLOW_PREPARE = 16777216u, RDS_GUARDED_ONEHOT = 33554432u, RDS_UNION_DEMAND = 268435456u, RDS_FLOW_CACHE = 536870912u, RDS_COLD_REGIONS = 1073741824u, RDS_TYPED_SCRATCH = 2147483648u };
typedef struct { uint32_t workers, flags; } rds_options;
/* Fuse owner-local infallible semantic transitions with their eager producers.
 * Committed state and host effects retain the ordinary publication boundary. */
#define RDS_LIFT_TRANSITIONS 67108864u
/* Use word proposals and sparse publication for supported semantic objects. */
#define RDS_LIFT_PRIMITIVES 134217728u
/* A registered clocked host model consumes old inputs and returns next outputs. */
typedef int (*rds_host_fn)(void *context, const uint64_t *inputs, size_t input_count,
                           uint64_t *outputs, size_t output_count);
int rds_bind_host(rds_sim *sim, const char *occurrence, rds_host_fn function, void *context);
typedef void (*rds_object_trace_fn)(void *context, uint64_t cycle, const char *occurrence,
                                    uint32_t kind, uint32_t count, int enqueue, int dequeue);
void rds_set_object_trace(rds_sim *sim, rds_object_trace_fn function, void *context);

/* Errors are copied into error when supplied. Failed loads own no resources. */
rds_sim *rds_load(const char *path, char *error, size_t error_size);
/* Zero workers selects one. Options are fixed for the lifetime of a model.
 * Snapshot cones receive static ownership under bounded replication; the
 * component scheduler is the fallback when no acceptable cone plan is found. */
rds_sim *rds_load_with_options(const char *path, const rds_options *options,
                              char *error, size_t error_size);
void rds_free(rds_sim *sim);
const char *rds_error(const rds_sim *sim);
int rds_find_port(const rds_sim *sim, const char *name);
uint32_t rds_port_width(const rds_sim *sim, uint32_t port);
int rds_port_is_input(const rds_sim *sim, uint32_t port);
/* Strict mode diagnoses partial combinational operations even in unselected
 * mux branches. Default execution chooses zero for those unspecified results. */
void rds_set_strict(rds_sim *sim, int enabled);
/* Words are least-significant first; count must match ceil(port_width / 64). */
int rds_set(rds_sim *sim, uint32_t port, const uint64_t *words, size_t count);
int rds_get(rds_sim *sim, uint32_t port, uint64_t *words, size_t count);
int rds_set_u64(rds_sim *sim, uint32_t port, uint64_t value);
int rds_get_u64(rds_sim *sim, uint32_t port, uint64_t *value);
/* eval is pure with respect to hardware state. advance checks effects and
 * commits one rising edge; output reads require another eval afterwards. */
int rds_eval(rds_sim *sim);
int rds_advance(rds_sim *sim);
/* Advance consecutive edges with unchanged public inputs. Callbacks and failures
 * retain advance semantics; eligible compiled plans keep workers inside the
 * cycle loop. No caller may inspect or mutate the model during this call. */
int rds_advance_cycles(rds_sim *sim, uint64_t cycles);
rds_stats rds_get_stats(const rds_sim *sim);
/* Inspection only: emits normalized operations and fixed worker/storage bindings.
 * Each instruction includes its source value ID, including replicated copies. */
int rds_emit_plan(rds_sim *sim, const char *path);
/* Ahead-of-time compilation of operations, semantic objects, and state effects.
 * Emission never invokes a compiler. Emit before attachment: a successful first
 * attachment releases the interpreter plan. Replacement preserves current state.
 * max_shapes=0 allows 512 preferred fused shapes per worker, up to 4096;
 * other work uses direct C. Explicit shape limits cannot exceed 4096.
 * Compiled RTL state swaps two banks; RDS_COPY_STATE retains copying for comparison. */
int rds_emit_c(rds_sim *sim, const char *path, uint32_t max_shapes);
int rds_use_compiled(rds_sim *sim, const char *shared_library);
typedef struct {
    uint32_t scheduled_operations, replicated_operations, compiled_blocks, compiled_shapes;
    uint64_t estimated_work, largest_worker_work;
    size_t compiled_file_bytes;
} rds_execution_stats;
rds_execution_stats rds_get_execution_stats(const rds_sim *sim);
#ifdef __cplusplus
}
#endif
#endif
