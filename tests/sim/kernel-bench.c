/* Checks wide scoreboard C/assembly equivalence and measures both kernel ABIs. */
#define _POSIX_C_SOURCE 200809L
#include "../../rhodium/sim/runtime/internal.h"
#include "workload.h"

int main(void) {
#ifndef RDS_HAVE_X86_64_ASM
    puts("assembly kernel benchmark requires System V x86-64");
    return 0;
#else
    const size_t sizes[] = {1, 3, 32, 256, 4096};
    uint64_t state[4096], set[4096], clear[4096], c[4096], assembly[4096];
    for (size_t i = 0; i < 4096; ++i) {
        state[i] = i * UINT64_C(0x9e3779b97f4a7c15);
        set[i] = ~state[i] + i; clear[i] = state[i] ^ (state[i] >> 17);
    }
    for (size_t n = 0; n <= 4096; ++n) {
        rds_set_clear_c(c,state,set,clear,n);
        rds_set_clear_x86_64(assembly,state,set,clear,n);
        if (memcmp(c,assembly,n*sizeof *c)) return 1;
    }
    puts("wide scoreboard C/assembly equivalence: lengths 0..4096 passed");
    for (size_t k = 0; k < sizeof sizes / sizeof *sizes; ++k) {
        size_t n = sizes[k], repeats = 20000000 / n;
        for (unsigned trial = 0; trial < 5; ++trial) for (unsigned j = 0; j < 2; ++j) {
            bool use_asm = ((j + trial) & 1) != 0;
            rds_set_clear_fn fn = use_asm ? rds_set_clear_x86_64 : rds_set_clear_c;
            double start = workload_time();
            for (size_t r = 0; r < repeats; ++r) fn(c,state,set,clear,n);
            double elapsed = workload_time() - start;
            printf("kernel %s words %zu ns_per_word %.3f\n", use_asm ? "assembly" : "C", n, elapsed*1e9/(n*repeats));
        }
    }
    return 0;
#endif
}
