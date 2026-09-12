/* Supplies portable wide semantic kernels with the same ABI as optional assembly. */
#include "internal.h"

void rds_set_clear_c(uint64_t *restrict dst, const uint64_t *restrict state,
                     const uint64_t *restrict set, const uint64_t *restrict clear, size_t words) {
    for (size_t i = 0; i < words; ++i) dst[i] = (state[i] | set[i]) & ~clear[i];
}
