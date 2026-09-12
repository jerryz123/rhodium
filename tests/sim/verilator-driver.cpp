// Runs matching accumulator or pipeline stimuli through a Verilator O2 model.
#ifdef RDS_PIPELINE
#include "VPipeline.h"
using Model = VPipeline;
#else
#include "VHierarchy.h"
using Model = VHierarchy;
#endif
#include "workload.h"
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    VerilatedContext context;
    Model model{&context};
    uint64_t cycles = strtoull(argv[1], nullptr, 10), hash = 0;
    bool trace = argc > 2 && !strcmp(argv[2], "trace");
    double start = workload_time();
    for (uint64_t i = 0; i < cycles; ++i) {
        model.clk = 0;
        model.rst = workload_reset(i);
        model.amount = workload_amount(i);
        model.eval();
#ifdef RDS_PIPELINE
        uint32_t outputs[] = {model.lane_0, model.lane_1, model.lane_2, model.lane_3,
                              model.lane_4, model.lane_5, model.lane_6, model.lane_7};
#else
        uint32_t outputs[] = {model.first_value, model.second_value};
#endif
        if (trace) printf("%llu", (unsigned long long)i);
        for (size_t j = 0; j < sizeof outputs / sizeof *outputs; j += 2) {
            hash = workload_hash(hash, outputs[j], outputs[j + 1]);
            if (trace) printf(" %u %u", outputs[j], outputs[j + 1]);
        }
        if (trace) putchar('\n');
        model.clk = 1;
        model.eval();
    }
    printf("result %llu %.9f\n", (unsigned long long)hash, workload_time() - start);
    model.final(); return 0;
}
