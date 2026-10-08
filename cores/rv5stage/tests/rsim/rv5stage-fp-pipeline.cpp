// Checks FP LSU bridges, hazards, backpressure, and F/D/Zfh/Zfa execution.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
template <class F> void await_value(F ready) {
  eval();
  while (!ready())
    tick_model();
}
template <class F> void accept(F ready) {
  await_value(ready);
  tick_model();
}
void load_register(std::uint8_t rd, std::uint64_t data,
                   std::uint8_t precision) {
  {
    eval();
    load_reserve_in.pvalid = 1;
    load_reserve_in.pbits.pcontext = rd;
    load_reserve_in.pbits.pprecision = precision;
    load_reserve_in.pbits.prd = rd;
    accept([&] { return load_reserve_out.pready; });
    eval();
    load_reserve_in.pvalid = 0;
    load_completion_in.pvalid = 1;
    load_completion_in.pbits.pcontext = rd;
    load_completion_in.pbits.pprecision = precision;
    load_completion_in.pbits.prd = rd;
    load_completion_in.pbits.pdata = data;
    eval();
    CHECK(state_update_out.pvalid);
    CHECK(state_update_out.pbits == 0);
    tick_model();
    load_completion_in.pvalid = 0;
  }
}

void consume_completion() {
  {
    completion_in.pready = 1;
    tick_model();
    completion_in.pready = 0;
  }
}

int main() {
  return run_test([] {
    reset = 1;
    snapshot_read_address = 0;
    issue_in = {};
    completion_in = {};
    load_reserve_in = {};
    load_completion_in = {};
    load_hit_in = {};
    store_request_in = {};
    vector_reserve_in = {};
    vector_write_in = {};
    for (unsigned i = 0; i < 2; ++i)
      tick_model();
    eval();
    reset = 0;

    // A scalar WB hit writes without allocating a deferred reservation.
    eval();
    load_hit_in = {.pvalid = 1,
                   .pbits = {.pcontext = UINT64_C(0x33),
                             .pprecision = 1,
                             .prd = 3,
                             .pdata = UINT64_C(0x3f800000)}};
    store_request_in = {
        .pvalid = 1,
        .pbits = {.pcontext = UINT64_C(0x33), .pprecision = 2, .prs = 3}};
    eval();
    CHECK(state_update_out.pvalid && busy == 0 && drained);
    tick_model();
    load_hit_in = {};
    store_request_in = {};
    eval();
    CHECK(store_response_out.pvalid &&
          store_response_out.pbits.pdata == UINT64_C(0xffffffff3f800000) &&
          busy == 0);

    load_register(1, UINT64_C(0x3ff0000000000000), 2);
    load_register(2, 1, 2);
    CHECK(busy == 0);

    // A reserved load destination blocks a dependent arithmetic request.
    eval();
    load_reserve_in.pvalid = 1;
    load_reserve_in.pbits.pcontext = UINT64_C(0x44);
    load_reserve_in.pbits.pprecision = 2;
    load_reserve_in.pbits.prd = 4;
    tick_model();
    load_reserve_in.pvalid = 0;
    issue_in.pvalid = 1;
    issue_in.pbits = {};
    issue_in.pbits.pcontrol.pregisters.puses_ufrs1 = 1;
    issue_in.pbits.pcontrol.pregisters.pdestination = 2;
    issue_in.pbits.pcontrol.pexecution.punit = 2;
    issue_in.pbits.pcontrol.pexecution.psource_uprecision = 2;
    issue_in.pbits.pcontrol.pexecution.pdestination_uprecision = 2;
    issue_in.pbits.pfrs1 = 4;
    issue_in.pbits.pfrs2 = 2;
    issue_in.pbits.pfrd = 5;
    eval();
    CHECK(!issue_out.pready);
    issue_in.pvalid = 0;
    load_completion_in.pvalid = 1;
    load_completion_in.pbits.pcontext = UINT64_C(0x44);
    load_completion_in.pbits.pprecision = 2;
    load_completion_in.pbits.prd = 4;
    load_completion_in.pbits.pdata = UINT64_C(0x4010000000000000);
    tick_model();
    load_completion_in.pvalid = 0;

    // Issue an inexact FADD.D and hold its completion under backpressure.
    issue_in.pvalid = 1;
    issue_in.pbits = {};
    issue_in.pbits.pcontext = UINT64_C(0xa5);
    issue_in.pbits.pcontrol.pregisters.puses_ufrs1 = 1;
    issue_in.pbits.pcontrol.pregisters.puses_ufrs2 = 1;
    issue_in.pbits.pcontrol.pregisters.pdestination = 2;
    issue_in.pbits.pcontrol.pexecution.punit = 2;
    issue_in.pbits.pcontrol.pexecution.psource_uprecision = 2;
    issue_in.pbits.pcontrol.pexecution.pdestination_uprecision = 2;
    issue_in.pbits.pcontrol.pexecution.puses_urounding_umode = 1;
    issue_in.pbits.pfrs1 = 1;
    issue_in.pbits.pfrs2 = 2;
    issue_in.pbits.pfrd = 3;
    issue_in.pbits.prounding_umode = 0;
    accept([&] { return issue_out.pready; });
    eval();
    issue_in.pvalid = 0;
    await_value([&] { return completion_out.pvalid; });
    eval();
    CHECK(completion_out.pbits.pcontext == UINT64_C(0xa5));
    CHECK(completion_out.pbits.pdestination == 2);
    CHECK(completion_out.pbits.prd == 3);
    CHECK(completion_out.pbits.pfp_uvalue == UINT64_C(0x3ff0000000000000));
    CHECK(completion_out.pbits.pexception_uflags == 1);
    CHECK(completion_out.pbits.pexception_uflags_uvalid);
    CHECK(((busy >> 3) & 1));
    for (unsigned i = 0; i < 2; ++i)
      tick_model();
    eval();
    CHECK(completion_out.pvalid);
    CHECK(completion_out.pbits.pfp_uvalue == UINT64_C(0x3ff0000000000000));
    consume_completion();
    eval();
    CHECK(!((busy >> 3) & 1));

    // A later FP load dirties FP state but must not replay stale arithmetic flags.
    load_register(2, UINT64_C(0x4000000000000000), 2);

    // The variable-latency divide lane uses the same buffered completion path.
    issue_in.pvalid = 1;
    issue_in.pbits = {};
    issue_in.pbits.pcontext = UINT64_C(0xd1);
    issue_in.pbits.pcontrol.pregisters.puses_ufrs1 = 1;
    issue_in.pbits.pcontrol.pregisters.puses_ufrs2 = 1;
    issue_in.pbits.pcontrol.pregisters.pdestination = 2;
    issue_in.pbits.pcontrol.pexecution.punit = 4;
    issue_in.pbits.pcontrol.pexecution.psource_uprecision = 2;
    issue_in.pbits.pcontrol.pexecution.pdestination_uprecision = 2;
    issue_in.pbits.pcontrol.pexecution.puses_urounding_umode = 1;
    issue_in.pbits.pcontrol.pexecution.pdivide_uoperation = 0;
    issue_in.pbits.pfrs1 = 4;
    issue_in.pbits.pfrs2 = 2;
    issue_in.pbits.pfrd = 6;
    issue_in.pbits.prounding_umode = 0;
    accept([&] { return issue_out.pready; });
    eval();
    issue_in.pvalid = 0;
    await_value([&] { return completion_out.pvalid; });
    eval();
    CHECK(completion_out.pbits.pcontext == UINT64_C(0xd1));
    CHECK(completion_out.pbits.pfp_uvalue == UINT64_C(0x4000000000000000));
    CHECK(completion_out.pbits.pexception_uflags == 0);
    CHECK(((busy >> 6) & 1));
    for (unsigned i = 0; i < 4; ++i) {
      tick_model();
      CHECK(completion_out.pvalid &&
            completion_out.pbits.pcontext == UINT64_C(0xd1) &&
            completion_out.pbits.pfp_uvalue == UINT64_C(0x4000000000000000) &&
            completion_out.pbits.pexception_uflags == 0 && ((busy >> 6) & 1));
    }
    consume_completion();
    eval();
    CHECK(!((busy >> 6) & 1));

    // The LSU store bridge observes the value written by the FP completion.
    store_request_in.pvalid = 1;
    store_request_in.pbits.pcontext = UINT64_C(0x3c);
    store_request_in.pbits.pprecision = 2;
    store_request_in.pbits.prs = 3;
    tick_model();
    store_request_in.pvalid = 0;
    eval();
    CHECK(store_response_out.pvalid);
    CHECK(store_response_out.pbits.pcontext == UINT64_C(0x3c));
    CHECK(store_response_out.pbits.pdata == UINT64_C(0x3ff0000000000000));
    CHECK(drained);
    tick_model();
    eval();
    CHECK(drained);

    // Integer-source operations select their FP lane by destination precision.
    issue_in.pvalid = 1;
    issue_in.pbits = {};
    issue_in.pbits.pcontext = UINT64_C(0xc1);
    issue_in.pbits.pcontrol.pregisters.puses_uinteger_urs1 = 1;
    issue_in.pbits.pcontrol.pregisters.pdestination = 2;
    issue_in.pbits.pcontrol.pexecution.punit = 8;
    issue_in.pbits.pcontrol.pexecution.pdestination_uprecision = 2;
    issue_in.pbits.pcontrol.pexecution.pinteger_uwidth = 2;
    issue_in.pbits.pinteger_uoperand = 2;
    issue_in.pbits.pfrd = 7;
    issue_in.pbits.prounding_umode = 0;
    accept([&] { return issue_out.pready; });
    eval();
    issue_in.pvalid = 0;
    await_value([&] { return completion_out.pvalid; });
    eval();
    CHECK(completion_out.pbits.pcontext == UINT64_C(0xc1));
    CHECK(completion_out.pbits.pfp_uvalue == UINT64_C(0x4000000000000000));
    CHECK(completion_out.pbits.pexception_uflags == 0);
    consume_completion();

    issue_in.pvalid = 1;
    issue_in.pbits = {};
    issue_in.pbits.pcontext = UINT64_C(0xc2);
    issue_in.pbits.pcontrol.pregisters.puses_uinteger_urs1 = 1;
    issue_in.pbits.pcontrol.pregisters.pdestination = 2;
    issue_in.pbits.pcontrol.pexecution.punit = 9;
    issue_in.pbits.pcontrol.pexecution.pdestination_uprecision = 2;
    issue_in.pbits.pinteger_uoperand = UINT64_C(0x3ff0000000000000);
    issue_in.pbits.pfrd = 8;
    accept([&] { return issue_out.pready; });
    eval();
    issue_in.pvalid = 0;
    await_value([&] { return completion_out.pvalid; });
    eval();
    CHECK(completion_out.pbits.pcontext == UINT64_C(0xc2));
    CHECK(completion_out.pbits.pfp_uvalue == UINT64_C(0x3ff0000000000000));
    CHECK(completion_out.pbits.pexception_uflags == 0);
    consume_completion();

    // FP-to-integer conversion retains the selected source lane and its flags.
    load_register(9, UINT64_C(0xc1e65a0bc0000000), 2);
    issue_in.pvalid = 1;
    issue_in.pbits = {};
    issue_in.pbits.pcontext = UINT64_C(0xc3);
    issue_in.pbits.pcontrol.pregisters.puses_ufrs1 = 1;
    issue_in.pbits.pcontrol.pregisters.pdestination = 1;
    issue_in.pbits.pcontrol.pexecution.punit = 8;
    issue_in.pbits.pcontrol.pexecution.psource_uprecision = 2;
    issue_in.pbits.pcontrol.pexecution.pinteger_uwidth = 2;
    issue_in.pbits.pfrs1 = 9;
    issue_in.pbits.pfrd = 10;
    issue_in.pbits.prounding_umode = 1;
    accept([&] { return issue_out.pready; });
    eval();
    issue_in.pvalid = 0;
    await_value([&] { return completion_out.pvalid; });
    eval();
    CHECK(completion_out.pbits.pcontext == UINT64_C(0xc3));
    CHECK(completion_out.pbits.pinteger_uvalue == UINT64_C(0xffffffff80000000));
    CHECK(completion_out.pbits.pexception_uflags == 16);
    CHECK(completion_out.pbits.pexception_uflags_uvalid);
    completion_in.pready = 1;
    eval();
    CHECK(state_update_out.pvalid);
    CHECK(state_update_out.pbits == 16);
    tick_model();
    completion_in.pready = 0;

    // FP-to-integer moves copy raw low bits rather than applying NaN unboxing.
    load_register(10, UINT64_C(0x7fffffff11111111), 2);
    issue_in.pvalid = 1;
    issue_in.pbits = {};
    issue_in.pbits.pcontext = UINT64_C(0xc4);
    issue_in.pbits.pcontrol.pregisters.puses_ufrs1 = 1;
    issue_in.pbits.pcontrol.pregisters.pdestination = 1;
    issue_in.pbits.pcontrol.pexecution.punit = 9;
    issue_in.pbits.pcontrol.pexecution.psource_uprecision = 1;
    issue_in.pbits.pfrs1 = 10;
    issue_in.pbits.pfrd = 11;
    accept([&] { return issue_out.pready; });
    eval();
    issue_in.pvalid = 0;
    await_value([&] { return completion_out.pvalid; });
    eval();
    CHECK(completion_out.pbits.pcontext == UINT64_C(0xc4));
    CHECK(completion_out.pbits.pinteger_uvalue == UINT64_C(0x0000000011111111));
    CHECK(!completion_out.pbits.pexception_uflags_uvalid);
    consume_completion();

    // Zfh arithmetic consumes and produces NaN-boxed binary16 values.
    load_register(12, UINT64_C(0x0000000000003c00), 0);
    issue_in.pvalid = 1;
    issue_in.pbits = {};
    issue_in.pbits.pcontext = 22;
    issue_in.pbits.pcontrol.pregisters.puses_ufrs1 = 1;
    issue_in.pbits.pcontrol.pregisters.puses_ufrs2 = 1;
    issue_in.pbits.pcontrol.pregisters.pdestination = 2;
    issue_in.pbits.pcontrol.pexecution.punit = 2;
    issue_in.pbits.pcontrol.pexecution.psource_uprecision = 0;
    issue_in.pbits.pcontrol.pexecution.pdestination_uprecision = 0;
    issue_in.pbits.pcontrol.pexecution.puses_urounding_umode = 1;
    issue_in.pbits.pfrs1 = 12;
    issue_in.pbits.pfrs2 = 12;
    issue_in.pbits.pfrd = 13;
    issue_in.pbits.prounding_umode = 0;
    accept([&] { return issue_out.pready; });
    eval();
    issue_in.pvalid = 0;
    await_value([&] { return completion_out.pvalid; });
    eval();
    CHECK(completion_out.pbits.pcontext == 22);
    CHECK(completion_out.pbits.pfp_uvalue == UINT64_C(0xffffffffffff4000));
    CHECK(completion_out.pbits.pexception_uflags == 0);
    consume_completion();

    // Zfhmin H-to-D conversion reuses the cross-format conversion path.
    issue_in.pvalid = 1;
    issue_in.pbits = {};
    issue_in.pbits.pcontext = 23;
    issue_in.pbits.pcontrol.pregisters.puses_ufrs1 = 1;
    issue_in.pbits.pcontrol.pregisters.pdestination = 2;
    issue_in.pbits.pcontrol.pexecution.punit = 8;
    issue_in.pbits.pcontrol.pexecution.psource_uprecision = 0;
    issue_in.pbits.pcontrol.pexecution.pdestination_uprecision = 2;
    issue_in.pbits.pcontrol.pexecution.puses_urounding_umode = 1;
    issue_in.pbits.pfrs1 = 12;
    issue_in.pbits.pfrd = 14;
    issue_in.pbits.prounding_umode = 0;
    accept([&] { return issue_out.pready; });
    eval();
    issue_in.pvalid = 0;
    await_value([&] { return completion_out.pvalid; });
    eval();
    CHECK(completion_out.pbits.pcontext == 23);
    CHECK(completion_out.pbits.pfp_uvalue == UINT64_C(0x3ff0000000000000));
    CHECK(completion_out.pbits.pexception_uflags == 0);
    consume_completion();

    // A half-precision store exposes only the architectural low 16 bits.
    store_request_in.pvalid = 1;
    store_request_in.pbits.pcontext = 24;
    store_request_in.pbits.pprecision = 0;
    store_request_in.pbits.prs = 13;
    tick_model();
    store_request_in.pvalid = 0;
    eval();
    CHECK(store_response_out.pvalid);
    CHECK(store_response_out.pbits.pcontext == 24);
    CHECK(store_response_out.pbits.pdata == UINT64_C(0x0000000000004000));
    tick_model();
    eval();
    CHECK(drained);

    // Zfa FLI uses the encoded rs1 field as a constant selector without a
    // register dependency. Selector 18 is 1.5 in every supported format.
    issue_in.pvalid = 1;
    issue_in.pbits = {};
    issue_in.pbits.pcontext = UINT64_C(0xfa);
    issue_in.pbits.pcontrol.pregisters.pdestination = 2;
    issue_in.pbits.pcontrol.pexecution.punit = 12;
    issue_in.pbits.pcontrol.pexecution.pdestination_uprecision = 2;
    issue_in.pbits.pfrs1 = 18;
    issue_in.pbits.pfrd = 16;
    accept([&] { return issue_out.pready; });
    eval();
    issue_in.pvalid = 0;
    await_value([&] { return completion_out.pvalid; });
    eval();
    CHECK(completion_out.pbits.pfp_uvalue == UINT64_C(0x3ff8000000000000));
    CHECK(!completion_out.pbits.pexception_uflags_uvalid);
    consume_completion();

    // FROUND suppresses inexact while FROUNDNX reports it for the same result.
    issue_in.pvalid = 1;
    issue_in.pbits = {};
    issue_in.pbits.pcontext = UINT64_C(0xf0);
    issue_in.pbits.pcontrol.pregisters.puses_ufrs1 = 1;
    issue_in.pbits.pcontrol.pregisters.pdestination = 2;
    issue_in.pbits.pcontrol.pexecution.punit = 13;
    issue_in.pbits.pcontrol.pexecution.psource_uprecision = 2;
    issue_in.pbits.pcontrol.pexecution.pdestination_uprecision = 2;
    issue_in.pbits.pcontrol.pexecution.puses_urounding_umode = 1;
    issue_in.pbits.pcontrol.pexecution.pround_uraise_uinexact = 0;
    issue_in.pbits.pfrs1 = 16;
    issue_in.pbits.pfrd = 17;
    issue_in.pbits.prounding_umode = 0;
    accept([&] { return issue_out.pready; });
    eval();
    issue_in.pvalid = 0;
    await_value([&] { return completion_out.pvalid; });
    eval();
    CHECK(completion_out.pbits.pfp_uvalue == UINT64_C(0x4000000000000000));
    CHECK(completion_out.pbits.pexception_uflags == 0);
    CHECK(completion_out.pbits.pexception_uflags_uvalid);
    consume_completion();

    issue_in.pvalid = 1;
    issue_in.pbits = {};
    issue_in.pbits.pcontext = UINT64_C(0xf1);
    issue_in.pbits.pcontrol.pregisters.puses_ufrs1 = 1;
    issue_in.pbits.pcontrol.pregisters.pdestination = 2;
    issue_in.pbits.pcontrol.pexecution.punit = 13;
    issue_in.pbits.pcontrol.pexecution.psource_uprecision = 2;
    issue_in.pbits.pcontrol.pexecution.pdestination_uprecision = 2;
    issue_in.pbits.pcontrol.pexecution.puses_urounding_umode = 1;
    issue_in.pbits.pcontrol.pexecution.pround_uraise_uinexact = 1;
    issue_in.pbits.pfrs1 = 16;
    issue_in.pbits.pfrd = 17;
    issue_in.pbits.prounding_umode = 0;
    accept([&] { return issue_out.pready; });
    eval();
    issue_in.pvalid = 0;
    await_value([&] { return completion_out.pvalid; });
    eval();
    CHECK(completion_out.pbits.pfp_uvalue == UINT64_C(0x4000000000000000));
    CHECK(completion_out.pbits.pexception_uflags == 1);
    consume_completion();

    // Zfa minimum propagates a quiet NaN and quiet comparisons do not raise
    // invalid for that operand.
    load_register(18, UINT64_C(0x7ff8000000000001), 2);
    issue_in.pvalid = 1;
    issue_in.pbits = {};
    issue_in.pbits.pcontext = UINT64_C(0xf2);
    issue_in.pbits.pcontrol.pregisters.puses_ufrs1 = 1;
    issue_in.pbits.pcontrol.pregisters.puses_ufrs2 = 1;
    issue_in.pbits.pcontrol.pregisters.pdestination = 2;
    issue_in.pbits.pcontrol.pexecution.punit = 6;
    issue_in.pbits.pcontrol.pexecution.psource_uprecision = 2;
    issue_in.pbits.pcontrol.pexecution.pdestination_uprecision = 2;
    issue_in.pbits.pcontrol.pexecution.pminmax_uoperation = 0;
    issue_in.pbits.pfrs1 = 18;
    issue_in.pbits.pfrs2 = 1;
    issue_in.pbits.pfrd = 19;
    accept([&] { return issue_out.pready; });
    eval();
    issue_in.pvalid = 0;
    await_value([&] { return completion_out.pvalid; });
    eval();
    CHECK(completion_out.pbits.pfp_uvalue == UINT64_C(0x7ff8000000000000));
    CHECK(completion_out.pbits.pexception_uflags == 0);
    consume_completion();

    issue_in.pvalid = 1;
    issue_in.pbits = {};
    issue_in.pbits.pcontext = UINT64_C(0xf3);
    issue_in.pbits.pcontrol.pregisters.puses_ufrs1 = 1;
    issue_in.pbits.pcontrol.pregisters.puses_ufrs2 = 1;
    issue_in.pbits.pcontrol.pregisters.pdestination = 1;
    issue_in.pbits.pcontrol.pexecution.punit = 7;
    issue_in.pbits.pcontrol.pexecution.psource_uprecision = 2;
    issue_in.pbits.pcontrol.pexecution.pcomparison = 2;
    issue_in.pbits.pcontrol.pexecution.pcomparison_usignaling = 0;
    issue_in.pbits.pfrs1 = 18;
    issue_in.pbits.pfrs2 = 1;
    issue_in.pbits.pfrd = 20;
    accept([&] { return issue_out.pready; });
    eval();
    issue_in.pvalid = 0;
    await_value([&] { return completion_out.pvalid; });
    eval();
    CHECK(completion_out.pbits.pinteger_uvalue == 0);
    CHECK(completion_out.pbits.pexception_uflags == 0);
    consume_completion();

    // FCVTMOD.W.D returns the low 32 bits while retaining FCVT.W.D flags.
    load_register(21, UINT64_C(0x41f0000000100000), 2);
    issue_in.pvalid = 1;
    issue_in.pbits = {};
    issue_in.pbits.pcontext = UINT64_C(0xf4);
    issue_in.pbits.pcontrol.pregisters.puses_ufrs1 = 1;
    issue_in.pbits.pcontrol.pregisters.pdestination = 1;
    issue_in.pbits.pcontrol.pexecution.punit = 14;
    issue_in.pbits.pcontrol.pexecution.psource_uprecision = 2;
    issue_in.pbits.pcontrol.pexecution.pinteger_uwidth = 2;
    issue_in.pbits.pfrs1 = 21;
    issue_in.pbits.pfrd = 22;
    accept([&] { return issue_out.pready; });
    eval();
    issue_in.pvalid = 0;
    await_value([&] { return completion_out.pvalid; });
    eval();
    CHECK(completion_out.pbits.pinteger_uvalue == 1);
    CHECK(completion_out.pbits.pexception_uflags == 16);
    consume_completion();
  });
}