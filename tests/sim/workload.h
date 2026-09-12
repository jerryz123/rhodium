/* Supplies identical deterministic stimuli and checksums to both simulator drivers. */
#ifndef RDS_TEST_WORKLOAD_H
#define RDS_TEST_WORKLOAD_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static inline uint32_t workload_amount(uint64_t cycle) { return (uint32_t)cycle * UINT32_C(0xBADCAF); }
static inline uint32_t workload_reset(uint64_t cycle) {
    return cycle < 2 || cycle % 4093 == 0;
}
static inline uint64_t workload_hash(uint64_t h, uint32_t a, uint32_t b) {
    return (h ^ ((uint64_t)a << 32 | b)) * UINT64_C(1099511628211);
}
static inline double workload_time(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}
#endif
