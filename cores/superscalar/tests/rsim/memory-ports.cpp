// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = 1;
    scalar_pipeline_in = {};
    vector_pipeline_in = {};
    pipeline_in = {};
    scalar_memory_in = {};
    vector_memory_in = {};
    memory_in = {};
    tick_model();
    reset = 0;

    // A contested fixed-cycle lookup has one physical winner and one response.
    scalar_pipeline_in.prequest.pvalid = 1;
    vector_pipeline_in.prequest.pvalid = 1;
    scalar_pipeline_in.prequest.pbits.paddress = 0x100;
    vector_pipeline_in.prequest.pbits.paddress = 0x200;
    eval();
    CHECK(!pipeline_vector && pipeline_out.prequest.pbits.paddress == 0x100);
    tick_model();
    scalar_pipeline_in.prequest.pvalid = 0;
    pipeline_in.presponse.pvalid = 1;
    eval();
    CHECK(pipeline_vector && pipeline_out.prequest.pbits.paddress == 0x200);
    CHECK(scalar_pipeline_out.presponse.pvalid && !vector_pipeline_out.presponse.pvalid);
    tick_model();
    vector_pipeline_in.prequest.pvalid = 0;
    pipeline_in.presponse.pvalid = 0;
    pipeline_in.pcommit_uready = 1;
    scalar_pipeline_in.pcommit = 1;
    eval();
    CHECK(!scalar_pipeline_out.presponse.pvalid && !vector_pipeline_out.presponse.pvalid);
    CHECK(pipeline_out.pcommit && scalar_pipeline_out.pcommit_uready && !vector_pipeline_out.pcommit_uready);
    tick_model();
    eval();
    CHECK(!pipeline_out.pcommit && !scalar_pipeline_out.pcommit_uready && vector_pipeline_out.pcommit_uready);

    // A stalled offer keeps priority. Rotation and credit consumption follow transfer.
    scalar_memory_in.prequest.pvalid = 1;
    vector_memory_in.prequest.pvalid = 1;
    memory_in.pfault.pvalid = 1;
    memory_in.pdrained = 1;
    tick_model();
    eval();
    CHECK(!request_vector && scalar_memory_out.pfault.pvalid && !vector_memory_out.pfault.pvalid);
    memory_in.pfault.pvalid = 0;
    memory_in.prequest.pready = 1;
    for (unsigned accepted = 0; accepted < 16; ++accepted) {
      eval();
      CHECK(memory_out.prequest.pvalid && request_vector == (accepted % 2));
      CHECK(scalar_memory_out.prequest.pready != vector_memory_out.prequest.pready);
      tick_model();
    }
    eval();
    CHECK(!memory_out.prequest.pvalid && !scalar_memory_out.pdrained && !vector_memory_out.pdrained);

    // The live grant is vector, but the scalar-tagged reply waits for scalar ready.
    // A stalled reply cannot reopen the full request port.
    scalar_memory_in.prequest.pvalid = 0;
    memory_in.presponse.pvalid = 1;
    tick_model();
    eval();
    CHECK(request_vector && scalar_memory_out.presponse.pvalid && !vector_memory_out.presponse.pvalid);
    CHECK(!memory_out.presponse.pready && !memory_out.prequest.pvalid);
    scalar_memory_in.presponse.pready = 1;
    tick_model();
    eval();
    CHECK(memory_out.prequest.pvalid && vector_memory_out.prequest.pready);
  });
}
