// Checks two-client FP service throughput, arithmetic, opaque tags, stalls,
// fairness, and reset.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

#ifndef FP_SCHEDULED
#define FP_SCHEDULED 0
#endif
constexpr bool scheduled = FP_SCHEDULED;
using request_port_t = std::remove_cvref_t<decltype(first_in)>;
using request_t = std::remove_cvref_t<decltype(first_in.pbits)>;
using result_port_t = std::remove_cvref_t<decltype(first_result_out)>;
using result_t = std::remove_cvref_t<decltype(first_result_out.pbits)>;
bool pending[2][256]{};
request_t accepted[2][256]{};
int accepted_cycle[2][256]{};
result_port_t held[2]{};
bool stalled[2]{};
int sent[2]{}, received[2]{}, cycles = 0, consecutive = 0, best_run = 0,
                              last_client = -1;
int fixed_before_division = 0, blocked_offers = 0, held_cycles = 0,
    overlap_cycles = 0;
bool throughput_mode = 0, fixed_stream = 0, division_seen = 0;

request_t operation(int client, int token, bool fixed_only) {
  request_t return_value{};

  request_t req;
  req = {};
  req.ptag = ((token)&low_mask(8));
  req.pcontrol.pregisters.pdestination = UINT64_C(2);
  req.pcontrol.pregisters.puses_ufrs1 = 1;
  req.pcontrol.pregisters.puses_ufrs2 = 1;
  req.pcontrol.pexecution.punit = UINT64_C(2);
  req.pcontrol.pexecution.psource_uprecision = UINT64_C(2);
  req.pcontrol.pexecution.pdestination_uprecision = UINT64_C(2);
  req.pleft_uprecision = UINT64_C(2);
  req.pright_uprecision = UINT64_C(2);
  req.pthird_uprecision = UINT64_C(2);
  req.pleft = UINT64_C(4607182418800017408);
  req.pright = UINT64_C(4611686018427387904);

  if (!fixed_only)
    switch ((token + client * 5) % 12) {
    case 0: {
      req.pcontrol.pexecution.punit = UINT64_C(4);
      req.pleft = UINT64_C(4618441417868443648);
    }

    break;
    case 1: {
    }

    break;
    case 2: {
      req.pcontrol.pexecution.punit = UINT64_C(3);
      req.pleft = UINT64_C(4611686018427387904);
      req.pright = UINT64_C(4613937818241073152);
    }

    break;
    case 3: {
      req.pcontrol.pexecution.punit = UINT64_C(4);
      req.pcontrol.pexecution.pdivide_uoperation = 1;
      req.pleft = UINT64_C(4616189618054758400);
    }

    break;
    case 4: {
      req.pcontrol.pexecution.punit = UINT64_C(1);
      req.pleft = UINT64_C(4611686018427387904);
      req.pright = UINT64_C(4613937818241073152);
      req.pthird = UINT64_C(4607182418800017408);
    }

    break;
    case 5: {
      req.pcontrol.pexecution.psource_uprecision = UINT64_C(1);
      req.pcontrol.pexecution.pdestination_uprecision = UINT64_C(1);
      req.pleft_uprecision = UINT64_C(1);
      req.pright_uprecision = UINT64_C(1);
      req.pthird_uprecision = UINT64_C(1);
      req.pleft = UINT64_C(18446744070479937536);
      req.pright = UINT64_C(18446744070488326144);
    }

    break;
    case 6: {
      req.pcontrol.pexecution.punit = UINT64_C(4);
      req.pright = 0;
    }

    break;
    case 7: {
      req.pcontrol.pexecution.punit = UINT64_C(4);
      req.pcontrol.pexecution.pdivide_uoperation = 1;
      req.pleft = UINT64_C(13830554455654793216);
    }

    break;
    case 8: {
      req.pcontrol.pexecution.punit = UINT64_C(9);
      req.pcontrol.pregisters.pdestination = UINT64_C(1);
      req.pleft = UINT64_C(13830554455654793216);
    }

    break;
    case 9: {
      req.pcontrol.pexecution.psource_uprecision = UINT64_C(1);
      req.pcontrol.pexecution.pdestination_uprecision = UINT64_C(1);
      req.pleft_uprecision = UINT64_C(1);
      req.pright_uprecision = UINT64_C(1);
      req.pthird_uprecision = UINT64_C(1);
      req.pleft = UINT64_C(1065353216);
      req.pright = UINT64_C(18446744070488326144);
    }

    break;
    case 10: {
      req.pright = UINT64_C(4368491638549381120);
      req.prounding_umode = UINT64_C(3);
    }

    break;
    case 11: {
      req.pcontrol.pexecution.punit = UINT64_C(7);
      req.pcontrol.pexecution.pcomparison = UINT64_C(1);
      req.pcontrol.pregisters.pdestination = UINT64_C(1);
    }

    break;
    }
  return req;

  return return_value;
}

void check_result(int client, result_t value) {
  std::uint64_t expected;
  std::uint8_t flags;
  bool flags_valid, integer_result;
  int op;
  CHECK(pending[client][value.ptag]);
  if (scheduled &&
      accepted[client][value.ptag].pcontrol.pexecution.punit != UINT64_C(4)) {
    request_t request = accepted[client][value.ptag];
    int latency = (request.pcontrol.pexecution.punit >= 1 &&
                   request.pcontrol.pexecution.punit <= 3)
                      ? (request.pcontrol.pexecution.pdestination_uprecision ==
                                 UINT64_C(2)
                             ? 4
                             : 3)
                      : 2;
    CHECK(cycles == accepted_cycle[client][value.ptag] + latency);
  }
  expected = UINT64_C(4613937818241073152);
  flags = 0;
  flags_valid = 1;
  integer_result = 0;
  op = (throughput_mode || (fixed_stream && client == 1))
           ? 1
           : (int(value.ptag) + client * 5) % 12;
  switch (op) {
  case 2:
    expected = UINT64_C(4618441417868443648);

    break;
  case 3:
    expected = UINT64_C(4611686018427387904);

    break;
  case 4:
    expected = UINT64_C(4619567317775286272);

    break;
  case 5:
    expected = UINT64_C(18446744070492520448);

    break;
  case 6: {
    expected = UINT64_C(9218868437227405312);
    flags = UINT64_C(8);
  }

  break;
  case 7: {
    expected = UINT64_C(9221120237041090560);
    flags = UINT64_C(16);
  }

  break;
  case 8: {
    expected = UINT64_C(13830554455654793216);
    flags_valid = 0;
    integer_result = 1;
  }

  break;
  case 9:
    expected = UINT64_C(18446744071557873664);

    break;
  case 10: {
    expected = UINT64_C(4607182418800017409);
    flags = UINT64_C(1);
  }

  break;
  case 11: {
    expected = 1;
    integer_result = 1;
  }

  break;
  default: {
  }

  break;
  }
  CHECK((integer_result ? value.pinteger_uvalue : value.pfp_uvalue) ==
        expected);
  CHECK(value.pexception_uflags == flags &&
        value.pexception_uflags_uvalid == flags_valid);
  if (!throughput_mode && client == 0 && value.ptag == 0) {
    division_seen = 1;
    if (fixed_stream)
      CHECK(received[1] < 120);
  }
  if (!throughput_mode && !division_seen &&
      accepted[client][value.ptag].pcontrol.pexecution.punit != UINT64_C(4))
    fixed_before_division++;
  pending[client][value.ptag] = 0;
  received[client]++;
}

void sample() {
  request_port_t offers[2];
  result_port_t replies[2];
  bool ready[2], sink_ready[2];
  offers[0] = first_in;
  offers[1] = second_in;
  ready[0] = first_out.pready;
  ready[1] = second_out.pready;
  replies[0] = first_result_out;
  replies[1] = second_result_out;
  sink_ready[0] = first_result_in.pready;
  sink_ready[1] = second_result_in.pready;
  CHECK(!(offers[0].pvalid && ready[0] && offers[1].pvalid && ready[1]));
  for (int client = 0; client < 2; client++) {
    if (offers[client].pvalid && ready[client]) {
      if (throughput_mode && offers[0].pvalid && offers[1].pvalid) {
        CHECK(last_client != client);
        last_client = client;
      }
      CHECK(!pending[client][offers[client].pbits.ptag]);
      pending[client][offers[client].pbits.ptag] = 1;
      accepted[client][offers[client].pbits.ptag] = offers[client].pbits;
      accepted_cycle[client][offers[client].pbits.ptag] = cycles;
      sent[client]++;
    }
    if (offers[client].pvalid && !ready[client])
      blocked_offers++;
    if (stalled[client]) {
      CHECK(replies[client].pvalid == held[client].pvalid &&
            replies[client].pbits.ptag == held[client].pbits.ptag &&
            replies[client].pbits.pinteger_uvalue ==
                held[client].pbits.pinteger_uvalue &&
            replies[client].pbits.pfp_uvalue == held[client].pbits.pfp_uvalue &&
            replies[client].pbits.pexception_uflags ==
                held[client].pbits.pexception_uflags &&
            replies[client].pbits.pexception_uflags_uvalid ==
                held[client].pbits.pexception_uflags_uvalid);
      held_cycles++;
    }
    stalled[client] = replies[client].pvalid && !sink_ready[client];
    held[client] = replies[client];
    if (replies[client].pvalid && sink_ready[client])
      check_result(client, replies[client].pbits);
  }
  if ((first_in.pvalid && first_out.pready) ||
      (second_in.pvalid && second_out.pready)) {
    consecutive++;
    if (consecutive > best_run)
      best_run = consecutive;
    if ((first_result_out.pvalid && first_result_in.pready) ||
        (second_result_out.pvalid && second_result_in.pready))
      overlap_cycles++;
  } else
    consecutive = 0;
  cycles++;
}

void clear_epoch() {
  first_in = {};
  second_in = {};
  first_result_in = {};
  second_result_in = {};
  reset = 1;
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    tick_model();
  reset = 0;
  for (int c = 0; c < 2; ++c)
    for (int t = 0; t < 256; ++t)
      pending[c][t] = 0;
  for (int c = 0; c < 2; c++) {
    sent[c] = 0;
    received[c] = 0;
    stalled[c] = 0;
  }
  cycles = 0;
  consecutive = 0;
  best_run = 0;
  last_client = -1;
  division_seen = 0;
  fixed_before_division = 0;
  fixed_stream = 0;
  blocked_offers = 0;
  held_cycles = 0;
  overlap_cycles = 0;
}

void run_batch(int count, bool fixed_only) {
  throughput_mode = fixed_only;
  while (received[0] < count || received[1] < count) {
    first_in.pvalid = sent[0] < count;
    first_in.pbits = operation(0, sent[0], fixed_only);
    second_in.pvalid = sent[1] < count;
    second_in.pbits = operation(1, sent[1], fixed_only);
    first_result_in.pready =
        scheduled || fixed_only || (cycles > 30 && cycles % 17 < 11);
    second_result_in.pready =
        scheduled || fixed_only || (cycles > 45 && cycles % 19 < 10);
    eval();
    sample();
    tick_model();
    CHECK(cycles < 10000);
  }
  first_in.pvalid = 0;
  second_in.pvalid = 0;
  first_result_in.pready = 1;
  second_result_in.pready = 1;
  for (unsigned repeat_index = 0; repeat_index < (12); ++repeat_index) {
    eval();
    sample();
    tick_model();
  }
}

int main() {
  return run_test([] {
    clear_epoch();
    run_batch(48, 1);
    CHECK(best_run >= 32 && overlap_cycles >= 32);
    clear_epoch();
    throughput_mode = 0;
    fixed_stream = 1;
    while (received[0] < 1 || received[1] < 128) {
      first_in.pvalid = sent[0] == 0;
      first_in.pbits = operation(0, 0, 0);
      second_in.pvalid = sent[1] < 128;
      second_in.pbits = operation(1, sent[1], 1);
      first_result_in.pready = 1;
      second_result_in.pready = 1;
      eval();
      sample();
      tick_model();
      CHECK(cycles < 1000);
    }
    CHECK(division_seen && fixed_before_division > 0);
    clear_epoch();
    run_batch(96, 0);
    CHECK(division_seen && fixed_before_division > 0 &&
          (scheduled || held_cycles > 20) && blocked_offers > 20 &&
          overlap_cycles > 0);

    clear_epoch();
    throughput_mode = 0;
    for (int n = 0; n < 12; n++) {
      first_in.pvalid = sent[0] == 0;
      first_in.pbits = operation(0, 0, 0);
      second_in.pvalid = !scheduled && sent[1] < 4;
      second_in.pbits = operation(1, sent[1], 1);
      eval();
      sample();
      tick_model();
    }
    CHECK(sent[0] == 1 && (scheduled || sent[1] > 0) && received[0] == 0 &&
          received[1] == 0);
    clear_epoch();
    first_result_in.pready = 1;
    second_result_in.pready = 1;
    for (unsigned repeat_index = 0; repeat_index < (100); ++repeat_index) {
      eval();
      CHECK(!first_result_out.pvalid && !second_result_out.pvalid);
      tick_model();
    }
    run_batch(12, 0);
  });
}
