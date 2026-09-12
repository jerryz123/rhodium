/* Defines the shared state-only ABI consumed by generated simulation code. */
#ifndef RDS_COMPILED_H
#define RDS_COMPILED_H
/* One field inventory defines both the runtime layout and emitted C declarations. */
#define RDS_OBJECT_FIELDS(X)                                                                                 \
    X(uint32_t, kind)                                                                                        \
    X(uint32_t, width) X(uint32_t, depth) X(uint32_t, flags) X(uint32_t, ni) X(uint32_t *, inputs)           \
        X(uint32_t, words) X(uint32_t, owner) X(char *, name) X(uint32_t, head) X(uint32_t, tail)            \
            X(uint32_t, count) X(uint32_t, next_head) X(uint32_t, next_tail) X(uint32_t, next_count)         \
                X(uint64_t *, data) X(uint64_t *, pending) X(unsigned char *, valid)                         \
                    X(unsigned char *, changes) X(bool, enqueue) X(bool, dequeue) X(bool, reset)             \
                        X(uint64_t, set) X(uint64_t, clear) X(rds_host_fn, host) X(void *, context)
#define RDS_CONTEXT_FIELDS(X)                                                                                \
    X(uint64_t *, v)                                                                                         \
    X(uint64_t *, q) X(uint64_t *, next) X(rds_object *, objects) X(uint64_t **, memories) X(char *, error)  \
        X(uint64_t, cycle) X(bool, strict) X(void *, hot)
#define RDS_DECLARE_FIELD(type, name) type name;
typedef struct {
    RDS_OBJECT_FIELDS(RDS_DECLARE_FIELD)
} rds_object;
typedef struct {
    RDS_CONTEXT_FIELDS(RDS_DECLARE_FIELD)
} rds_compiled_context;
#undef RDS_DECLARE_FIELD
#endif
