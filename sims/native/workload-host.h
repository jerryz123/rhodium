/* Adapts word-oriented benchmark loaders to the sized-memory HTIF and boot register. */
#ifndef RHODIUM_WORKLOAD_HOST_H
#define RHODIUM_WORKLOAD_HOST_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef struct { int boot_sent, boot_request; } rds_workload_host;
typedef int (*rds_workload_tick)(void *, const uint64_t *, size_t, uint64_t *, size_t);
/* The workload adapters bind only the standard 0x1000 boot-address register.
 * Start completes on its write response, not merely request acceptance. */
static inline int rds_workload_host_tick(rds_workload_host *bridge,
    rds_workload_tick tick, void *context, const uint64_t *in, size_t ni,
    uint64_t *out, size_t no) {
    if (ni != 5 || no != 7) return -1;
    if (in[0]) memset(bridge, 0, sizeof *bridge);
    if (!in[0] && in[2] && in[4]) return -1;
    if (bridge->boot_request && in[1]) bridge->boot_sent = 1;
    int boot_complete = bridge->boot_sent && in[2];
    uint64_t old_in[5] = {in[0], in[1], in[2] && !bridge->boot_sent,
                          (uint32_t)in[3], (uint64_t)boot_complete};
    uint64_t old_out[8];
    if (tick(context, old_in, 5, old_out, 8)) return -1;
    if (boot_complete) bridge->boot_sent = 0;
    bridge->boot_request = old_out[5] && !bridge->boot_sent;
    out[0] = old_out[5] ? (uint64_t)bridge->boot_request : old_out[0];
    out[1] = old_out[5] ? 1 : old_out[1];
    out[2] = old_out[5] ? UINT64_C(0x1000) : old_out[2];
    out[3] = old_out[5] ? old_out[6] : old_out[3];
    out[4] = old_out[5] ? 8 : 4;
    out[5] = old_out[4];
    out[6] = old_out[7];
    return 0;
}
#endif
