/* Runs accumulator and pipeline workloads through configurable native execution modes. */
#define _POSIX_C_SOURCE 200809L
#include "workload.h"
#include "rhodium_sim.h"
int main(int argc, char **argv) {
    if (argc < 3) return 2;
    char error[512];
    rds_options options = {1,0};
    if (getenv("RDS_WORKERS")) options.workers = (uint32_t)strtoul(getenv("RDS_WORKERS"), NULL, 10);
    if (getenv("RDS_FLAGS")) options.flags = (uint32_t)strtoul(getenv("RDS_FLAGS"), NULL, 10);
    rds_sim *s = rds_load_with_options(argv[1], &options, error, sizeof error);
    if (!s) { fprintf(stderr, "%s\n", error); return 1; }
    if (getenv("RDS_COMPILED") && rds_use_compiled(s, getenv("RDS_COMPILED"))) goto fail;
    uint64_t cycles = strtoull(argv[2], NULL, 10), hash = 0;
    int trace = argc > 3 && !strcmp(argv[3], "trace");
    int rst = rds_find_port(s, "rst"), amount = rds_find_port(s, "amount");
    int outputs[8] = {rds_find_port(s, "first_value"), rds_find_port(s, "second_value")};
    int count = outputs[0] >= 0 ? 2 : 8;
    if (count == 8) for (int i = 0; i < count; ++i) {
        char name[16]; snprintf(name, sizeof name, "lane_%d", i); outputs[i] = rds_find_port(s, name);
    }
    if (argc > 3 && !strcmp(argv[3], "stats")) {
        rds_stats st = rds_get_stats(s);
        printf("stats %zu %zu %zu %zu %zu %zu %u %u %u\n", st.model_bytes, st.descriptor_bytes,
               st.schedule_bytes, st.value_bytes, st.memory_bytes, st.update_bytes, st.workers, st.components, st.batches);
    }
    double start = workload_time();
    for (uint64_t i = 0; i < cycles; ++i) {
        if (rds_set_u64(s, rst, workload_reset(i)) || rds_set_u64(s, amount, workload_amount(i))
            || rds_eval(s)) goto fail;
        if (trace) printf("%llu", (unsigned long long)i);
        for (int j = 0; j < count; j += 2) {
            uint64_t a, b;
            if (rds_get_u64(s, outputs[j], &a) || rds_get_u64(s, outputs[j + 1], &b)) goto fail;
            hash = workload_hash(hash, (uint32_t)a, (uint32_t)b);
            if (trace) printf(" %llu %llu", (unsigned long long)a, (unsigned long long)b);
        }
        if (trace) putchar('\n');
        if (rds_advance(s)) goto fail;
    }
    printf("result %llu %.9f\n", (unsigned long long)hash, workload_time() - start);
    rds_free(s); return 0;
fail:
    fprintf(stderr, "%s\n", rds_error(s)); rds_free(s); return 1;
}
