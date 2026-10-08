// Checks contested lookup, retained commit ownership, faults, and tagged completions.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    reset = 1;
    scalar_in = {};
    vector_in = {};
    memory_in = {};
    scalar_in.presponse.pready = 1;
    vector_in.presponse.pready = 1;
    scalar_pipeline_in = {};
    vector_pipeline_in = {};
    pipeline_in = {};
    tick_model();
    tick_model();
    reset = 0;

    // Scalar wins a contested lookup; vector receives replay one cycle later.
    scalar_pipeline_in.prequest = {.pvalid = 1,
                                   .pbits = {.pbyte_umask = 255,
                                             .paddress = UINT64_C(0x100),
                                             .paccess = UINT64_C(2),
                                             .pwidth = UINT64_C(3),
                                             .punsigned = 1,
                                             .pdata = UINT64_C(0x11)}};
    vector_pipeline_in.prequest = {.pvalid = 1,
                                   .pbits = {.pbyte_umask = 255,
                                             .paddress = UINT64_C(0x200),
                                             .paccess = UINT64_C(2),
                                             .pwidth = UINT64_C(3),
                                             .punsigned = 1,
                                             .pdata = UINT64_C(0x22)}};
    eval();
    CHECK(pipeline_out.prequest.pvalid &&
          pipeline_out.prequest.pbits.paddress == 0x100);
    tick_model();
    pipeline_in.presponse = {.pvalid = 1,
                             .pbits = {.poutcome = UINT64_C(2),
                                       .preason = 0,
                                       .pdata = UINT64_C(0x11)}};
    eval();
    CHECK(scalar_pipeline_out.presponse.pvalid &&
          scalar_pipeline_out.presponse.pbits.poutcome == 2);
    CHECK(vector_pipeline_out.presponse.pvalid &&
          vector_pipeline_out.presponse.pbits.poutcome == 3);
    scalar_pipeline_in.prequest.pvalid = 0;

    // The next lookup belongs to vector while the scalar store reaches commit.
    tick_model();
    pipeline_in.pcommit_uready = 1;
    scalar_pipeline_in.pcommit = 1;
    vector_pipeline_in.pcommit = 0;
    eval();
    CHECK(pipeline_out.pcommit && scalar_pipeline_out.pcommit_uready &&
          !vector_pipeline_out.pcommit_uready);
    CHECK(vector_pipeline_out.presponse.pvalid &&
          vector_pipeline_out.presponse.pbits.poutcome == 2);
    vector_pipeline_in.prequest.pvalid = 0;
    tick_model();
    scalar_pipeline_in.pcommit = 0;
    vector_pipeline_in.pcommit = 1;
    eval();
    CHECK(pipeline_out.pcommit && vector_pipeline_out.pcommit_uready &&
          !scalar_pipeline_out.pcommit_uready);
    vector_pipeline_in.pcommit = 0;
    tick_model();
    eval();
    CHECK(!pipeline_out.pcommit && !scalar_pipeline_out.pcommit_uready &&
          !vector_pipeline_out.pcommit_uready);

    // An absent pipeline responder means Slow, never a fabricated hit.
    vector_pipeline_in.prequest.pvalid = 1;
    pipeline_in.presponse.pvalid = 0;
    tick_model();
    eval();
    CHECK(vector_pipeline_out.presponse.pvalid &&
          vector_pipeline_out.presponse.pbits.poutcome == 0);
    vector_pipeline_in.prequest.pvalid = 0;

    scalar_in.prequest = {.pvalid = 1, .pbits = {}};
    scalar_in.prequest.pbits.paddress = 0x300;
    vector_in.prequest = {.pvalid = 1, .pbits = {}};
    vector_in.prequest.pbits.paddress = 0x400;
    memory_in.prequest.pready = 0;
    memory_in.prequest_ufault = 1;
    eval();
    CHECK(memory_out.prequest.pvalid &&
          memory_out.prequest.pbits.paddress == 0x400 &&
          vector_out.prequest_ufault && !scalar_out.prequest_ufault &&
          !scalar_out.prequest.pready);
    vector_in.prequest.pvalid = 0;
    memory_in.prequest_ufault = 0;
    memory_in.prequest_uaccess_ufault = 1;
    eval();
    CHECK(memory_out.prequest.pbits.paddress == 0x300 &&
          scalar_out.prequest_uaccess_ufault &&
          !vector_out.prequest_uaccess_ufault);
    scalar_in.prequest.pvalid = 0;
    vector_in.prequest.pvalid = 1;
    memory_in.prequest_uaccess_ufault = 0;
    memory_in.prequest.pready = 1;
    eval();
    CHECK(vector_out.prequest.pready && !scalar_out.prequest.pready);
    tick_model();
    vector_in.prequest.pvalid = 0;

    // Response ownership comes from the accepted tag, not the current winner.
    memory_in.presponse = {
        .pvalid = 1,
        .pbits = {.paccess_ufault = 0,
                  .pdata = UINT64_C(0x5678),
                  .pcontext = {.pwriteback = ((3u << 7) | 3u), .porigin = 0}}};
    eval();
    CHECK(vector_out.presponse.pvalid && !scalar_out.presponse.pvalid &&
          vector_out.presponse.pbits.pdata == 0x5678);
    memory_in.presponse.pbits.pcontext.pwriteback = ((1u << 7) | 9u);
    eval();
    CHECK(scalar_out.presponse.pvalid && !vector_out.presponse.pvalid);
    scalar_in.presponse.pready = 0;
    eval();
    CHECK(!memory_out.presponse.pready && scalar_out.presponse.pvalid);
    memory_in.presponse.pbits.pcontext.pwriteback = ((3u << 7) | 3u);
    eval();
    CHECK(memory_out.presponse.pready && vector_out.presponse.pvalid);
    scalar_in.presponse.pready = 1;
    memory_in.pdrained = 1;
    memory_in.preservation_uvalid = 1;
    eval();
    CHECK(scalar_out.pdrained && vector_out.pdrained &&
          scalar_out.preservation_uvalid && vector_out.preservation_uvalid);
  });
}
