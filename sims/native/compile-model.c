/* Emits bounded residual C blocks for the harness's requested execution plan. */
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: compile-model model.rsim output.c\n"); return 2; }
    rds_options options = {
        getenv("RDS_WORKERS") ? (uint32_t)strtoul(getenv("RDS_WORKERS"), NULL, 10) : 1,
        getenv("RDS_FLAGS") ? (uint32_t)strtoul(getenv("RDS_FLAGS"), NULL, 0) : 0
    };
    char error[512];
    rds_sim *sim = rds_load_with_options(argv[1], &options, error, sizeof error);
    if (!sim) { fprintf(stderr, "%s\n", error); return 1; }
    uint32_t shapes = getenv("RDS_MAX_SHAPES") ? (uint32_t)strtoul(getenv("RDS_MAX_SHAPES"), NULL, 10) : 0;
    int result = rds_emit_c(sim, argv[2], shapes);
    const char *report = getenv("RDS_PLAN_REPORT");
    if (!result && report) result = rds_emit_plan(sim, report);
    if (result) fprintf(stderr, "%s\n", rds_error(sim));
    rds_free(sim); return result ? 1 : 0;
}
