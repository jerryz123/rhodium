// Preserves the rv5stage-fetch-prediction cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
#include <deque>
extern "C" void fallback_trace_bind();
extern "C" void fallback_trace_expect(std::uint64_t source_word,
                                      std::uint64_t target_pc,
                                      std::uint64_t minimum_delay);
extern "C" void fallback_trace_sample(unsigned reset, unsigned flush,
                                      unsigned kill, unsigned fire,
                                      unsigned response_valid, unsigned fault,
                                      std::uint64_t request_address);
extern "C" void fallback_trace_check();
extern "C" void fallback_trace_finish();
using update_t = std::remove_cvref_t<decltype(control_in.pbranch_uupdate)>;
using response_bits_t =
    std::remove_cvref_t<decltype(memory_in.presponse.pbits.presponse)>;
// Checks prediction behavior and exact S2-to-S0 fallback lineage through stalls and recovery.

bool active = 0, flush = 0, restart_valid = 0;

bool invalidate_all = 0, predictor_flush = 0;

std::uint64_t restart_pc = 0;

update_t branch_update_in = {};

struct response_t {
  std::uint8_t pvalid{};
  response_bits_t pbits{};
};
response_t response{}, s2_response{};

bool request_ready = 1, output_ready = 1;

std::uint64_t fault_address = UINT64_MAX;

int mode = 0, cycle = 0, previous_request = -1, previous_output = -1;

int request_checks = 0, output_checks = 0, local_flushes = 0;

bool continuous_requests = 0, continuous_outputs = 0;
std::deque<std::uint64_t> expected_requests;

std::uint32_t word_at(std::uint64_t address) {
  switch (mode) {
  case 0: {
    return UINT64_C(111); // JAL at each word boundary.
  } break;
  case 1: {
    switch (address) {
    case UINT64_C(256): {
      return UINT64_C(2684469249); // Conditional compressed branch, then C.J.
    } break;
    case UINT64_C(512): {
      return UINT64_C(2684420097); // Enter at the upper C.J.
    } break;
    default: {
      return UINT64_C(19);
    } break;
    }
  } break;
  case 2: {
    switch (address) {
    case UINT64_C(768): {
      return UINT64_C(7274497); // 32-bit JAL starts at +2.
    } break;
    case UINT64_C(772): {
      return UINT64_C(65536); // Continuation, upper half must be discarded.
    } break;
    case UINT64_C(1024): {
      return UINT64_C(2684420097);
    } break;
    default: {
      return UINT64_C(19);
    } break;
    }
  } break;
  case 4: {
    switch (address) {
    case UINT64_C(256):
    case UINT64_C(260): {
      return UINT64_C(239); // Predicted JAL x1 call sites.
    } break;
    case UINT64_C(512):
    case UINT64_C(516): {
      return UINT64_C(32871); // BTB-missed JALR x0, x1 returns.
    } break;
    default: {
      return UINT64_C(19);
    } break;
    }
  } break;
  case 5: {
    switch (address) {
    case UINT64_C(256): {
      return UINT64_C(239);
    } break;
    case UINT64_C(768): {
      return UINT64_C(2156003329); // C.NOP followed by BTB-missed C.JR x1.
    } break;
    default: {
      return UINT64_C(19);
    } break;
    }
  } break;
  case 6: {
    switch (address) {
    case UINT64_C(256): {
      return UINT64_C(239);
    } break;
    case UINT64_C(1024): {
      return UINT64_C(
          2154233857); // C.NOP followed by the first half of JALR x0, x1.
    } break;
    case UINT64_C(1028): {
      return UINT64_C(
          65536); // Return continuation followed by wrong-path C.NOP.
    } break;
    default: {
      return UINT64_C(19);
    } break;
    }
  } break;
  case 7: {
    switch (address) {
    case UINT64_C(256): {
      return UINT64_C(239);
    } break;
    case UINT64_C(1280): {
      return UINT64_C(
          1245185); // C.NOP followed by the first half of 32-bit NOP.
    } break;
    case UINT64_C(1284): {
      return UINT64_C(
          2156003328); // NOP continuation followed by BTB-missed C.JR x1.
    } break;
    default: {
      return UINT64_C(19);
    } break;
    }
  } break;
  case 8: {
    switch (address) {
    case UINT64_C(256): {
      return UINT64_C(239);
    } break;
    case UINT64_C(1536): {
      return UINT64_C(2156052481); // C.BEQZ followed by C.JR x1.
    } break;
    case UINT64_C(1792): {
      return UINT64_C(6488065); // C.NOP followed by the first half of BEQ.
    } break;
    case UINT64_C(1796): {
      return UINT64_C(2156003328); // BEQ continuation followed by C.JR x1.
    } break;
    default: {
      return UINT64_C(19);
    } break;
    }
  } break;
  case 9: {
    switch (address) {
    case UINT64_C(2048): {
      return UINT64_C(268435695); // JAL x1, +256.
    } break;
    case UINT64_C(2052): {
      return UINT64_C(4292866159); // JAL x0, -4.
    } break;
    case UINT64_C(2304): {
      return UINT64_C(32871); // JALR x0, x1, 0.
    } break;
    default: {
      return UINT64_C(19);
    } break;
    }
  } break;
  case 10: {
    switch (address) {
    case UINT64_C(2560): {
      return UINT64_C(106513); // C.J +4 followed by C.NOP.
    } break;
    case UINT64_C(2564): {
      return UINT64_C(114677); // C.J -4 followed by C.NOP.
    } break;
    default: {
      return UINT64_C(19);
    } break;
    }
  } break;
  case 11: {
    switch (address) {
    case UINT64_C(2816): {
      return UINT64_C(
          7274497); // C.NOP followed by the first half of JAL x0, +256.
    } break;
    case UINT64_C(2820): {
      return UINT64_C(69632); // JAL continuation followed by C.NOP.
    } break;
    default: {
      return UINT64_C(19);
    } break;
    }
  } break;
  case 12: {
    switch (address) {
    case UINT64_C(3328): {
      return UINT64_C(2685468673); // C.NOP followed by C.J +4.
    } break;
    default: {
      return UINT64_C(19);
    } break;
    }
  } break;
  case 13:
  case 14:
  case 15: {
    switch (address) {
    case UINT64_C(256): {
      return UINT64_C(268435695); // Outer call to 0x200.
    } break;
    case UINT64_C(512): {
      return mode == 15 ? UINT64_C(270532847)
                        : UINT64_C(268435695); // Short inner call.
    } break;
    case UINT64_C(516): {
      return UINT64_C(32871); // Return to the outer caller.
    } break;
    case UINT64_C(768): {
      return mode == 13   ? UINT64_C(98434)
             : mode == 14 ? UINT64_C(32871)
                          : UINT64_C(2154233857);
    } break;
    case UINT64_C(772): {
      return UINT64_C(65536); // Straddling return continuation.
    } break;
    default: {
      return UINT64_C(19);
    } break;
    }
  } break;
  default: {
    return UINT64_C(19);
  } break;
  }
}

void initialize(int next_mode) {
  falling();
  reset = 1;
  active = 0;
  mode = next_mode;
  output_ready = 1;
  request_ready = 1;
  continuous_requests = 0;
  continuous_outputs = 0;
  fault_address = UINT64_MAX;
  expected_requests.clear();
  previous_request = -1;
  previous_output = -1;
  request_checks = 0;
  output_checks = 0;
  local_flushes = 0;
  for (int repeat_index = 0; repeat_index < (2); ++repeat_index)
    falling();
  reset = 0;
}
void train(std::uint64_t pc, std::uint64_t target, std::uint8_t compressed,
           std::uint8_t conditional = 0, std::uint8_t taken = 1,
           std::uint8_t ras_action = 0) {
  branch_update_in = {UINT64_C(1),
                      {pc, target, UINT64_C(1), conditional, taken, compressed,
                       ras_action, ras_action, pc + (compressed ? 2 : 4)}};
  falling();
  branch_update_in = {};
}
void start(std::uint64_t pc) {
  restart_valid = 1;
  restart_pc = pc;
  settle();
  CHECK(memory_out.pflush && memory_out.prequest.pvalid &&
        memory_out.prequest.pbits.paddress == (pc & ~UINT64_C(3)));
  falling();
  restart_valid = 0;
  active = 1;
}
void expect_pc(std::uint64_t pc, std::uint64_t npc, std::uint8_t fault = 0,
               std::uint8_t ras_action = 0,
               std::uint64_t fault_pc = UINT64_C(772)) {
  falling();
  while (!fetched_out.pvalid)
    falling();
  CHECK(fetched_out.pbits.ppc == pc &&
        fetched_out.pbits.ppredicted_unext_upc == npc &&
        fetched_out.pbits.ppredicted_uras_uaction == ras_action &&
        fetched_out.pbits.pinstruction_upage_ufault == fault);
  if (fault)
    CHECK(fetched_out.pbits.pinstruction_ufault_uaddress == fault_pc);
}

void drive() {
  {
    control_in.pactive = active;
    control_in.pflush.pvalid = flush;
    control_in.prestart.pvalid = restart_valid;
    control_in.prestart.pbits = restart_pc;
    control_in.pinvalidate_uall.pvalid = invalidate_all;
    control_in.ppredictor_uflush.pvalid = predictor_flush;
    control_in.pbranch_uupdate = branch_update_in;
    memory_in.prequest.pready = request_ready;
    memory_in.presponse = {s2_response.pvalid,
                           {s2_response.pbits, UINT64_C(0)}};
    fetched_in.pready = output_ready;
  }
}

void observe() {
  {
    fallback_trace_sample(
        int(reset), int(memory_out.pflush), int(memory_out.ps1_ukill),
        int(memory_out.prequest.pvalid && memory_in.prequest.pready),
        int(memory_in.presponse.pvalid),
        int(memory_in.presponse.pbits.presponse.ppage_ufault ||
            memory_in.presponse.pbits.presponse.paccess_ufault),
        memory_out.prequest.pbits.paddress);
  }
  {
    cycle = cycle + 1;
    if (reset) {
      defer(response, std::remove_cvref_t<decltype(response)>{});
      defer(s2_response, std::remove_cvref_t<decltype(s2_response)>{});
    } else {
      // Flush discards the prior epoch, but a request accepted on this edge is
      // the replacement epoch's S0 request and must still reach S2.
      defer(s2_response, memory_out.pflush || memory_out.ps1_ukill
                             ? response_t{}
                             : response);
      defer(response.pvalid, 0);
      if (memory_out.prequest.pvalid && memory_in.prequest.pready)
        defer(response,
              std::remove_cvref_t<decltype(response)>{
                  UINT64_C(1),
                  {word_at(memory_out.prequest.pbits.paddress),
                   memory_out.prequest.pbits.paddress == fault_address,
                   UINT64_C(0)}});
    }
    if (!reset && memory_out.pflush && !restart_valid && !flush)
      local_flushes = local_flushes + 1;
    if (!reset && memory_out.prequest.pvalid && memory_in.prequest.pready &&
        expected_requests.size() != 0) {
      std::uint64_t expected = expected_requests.front();
      expected_requests.pop_front();
      CHECK(memory_out.prequest.pbits.paddress == expected);
      if (continuous_requests && previous_request >= 0)
        CHECK(cycle == previous_request + 1);
      previous_request = cycle;
      request_checks = request_checks + 1;
    }
    if (!reset && fetched_out.pvalid && fetched_in.pready &&
        continuous_outputs) {
      if (previous_output >= 0)
        CHECK(cycle == previous_output + 1);
      previous_output = cycle;
      output_checks = output_checks + 1;
    }
  }
}

void falling_update() {}

void stimulus() {
  reset = 1;
  {
    fallback_trace_bind();
    initialize(0);
    train(UINT64_C(256), UINT64_C(512), 0);
    train(UINT64_C(512), UINT64_C(256), 0);
    for (int repeat_index = 0; repeat_index < (8); ++repeat_index) {
      expected_requests.push_back(UINT64_C(256));
      expected_requests.push_back(UINT64_C(512));
    }
    continuous_requests = 1;
    continuous_outputs = 1;
    start(UINT64_C(256));
    for (int repeat_index = 0; repeat_index < (8); ++repeat_index) {
      expect_pc(UINT64_C(256), UINT64_C(512));
      expect_pc(UINT64_C(512), UINT64_C(256));
    }
    CHECK(request_checks == 16 && output_checks >= 15 && local_flushes == 0);
    continuous_outputs = 0;
    continuous_requests = 0;

    // Ready stalls hold the request address even while the table is trained.
    request_ready = 0;
    output_ready = 0;
    {
      std::uint64_t held = memory_out.prequest.pbits.paddress;
      train(UINT64_C(256), UINT64_C(2048), 0);
      for (int repeat_index = 0; repeat_index < (4); ++repeat_index) {
        falling();
        CHECK(memory_out.prequest.pbits.paddress == held);
      }
    }
    // Already accepted occurrences still carry their original target.
    output_ready = 1;
    expect_pc(UINT64_C(256), UINT64_C(512));

    // Resolve the accepted PC in S1, then freeze its prediction in S2 rather
    // than looking up the table again when Decode consumes the packet.
    initialize(0);
    train(UINT64_C(256), UINT64_C(512), 0);
    request_ready = 0;
    output_ready = 0;
    start(UINT64_C(256));
    train(UINT64_C(256), UINT64_C(768),
          0); // An unaccepted offer sees live predictor training.
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    CHECK(memory_out.prequest.pvalid &&
          memory_out.prequest.pbits.paddress == UINT64_C(256));
    expected_requests = {UINT64_C(256)};
    request_ready = 1;
    falling();
    request_ready = 0;
    train(UINT64_C(256), UINT64_C(1024),
          0); // This must not change the accepted occurrence.
    expect_pc(UINT64_C(256), UINT64_C(768));
    CHECK(request_checks == 1 &&
          memory_out.prequest.pbits.paddress == UINT64_C(768));

    initialize(1);
    train(UINT64_C(256), UINT64_C(2304), 1, 1);
    train(UINT64_C(256), UINT64_C(2304), 1, 1, 0);
    train(UINT64_C(258), UINT64_C(514), 1);
    train(UINT64_C(514), UINT64_C(256), 1);
    expected_requests = {UINT64_C(256), UINT64_C(512), UINT64_C(256),
                         UINT64_C(512)};
    start(UINT64_C(256));
    for (int repeat_index = 0; repeat_index < (2); ++repeat_index) {
      expect_pc(UINT64_C(256), UINT64_C(258));
      expect_pc(UINT64_C(258), UINT64_C(514));
      expect_pc(UINT64_C(514), UINT64_C(256));
    }
    CHECK(request_checks == 4 && local_flushes == 0);

    initialize(2);
    train(UINT64_C(770), UINT64_C(1026), 0);
    train(UINT64_C(1026), UINT64_C(770), 1);
    // Repeated three-word loops exercise residual-halfword assembly while subsequent
    // occurrences retain their own prediction and continuation metadata.
    for (int repeat_index = 0; repeat_index < (10); ++repeat_index) {
      expected_requests.push_back(UINT64_C(768));
      expected_requests.push_back(UINT64_C(772));
      expected_requests.push_back(UINT64_C(1024));
    }
    start(UINT64_C(770));
    for (int repeat_index = 0; repeat_index < (10); ++repeat_index) {
      expect_pc(UINT64_C(770), UINT64_C(1026));
      expect_pc(UINT64_C(1026), UINT64_C(770));
    }
    CHECK(request_checks == 30 && local_flushes == 0);

    initialize(3);
    train(UINT64_C(1282), UINT64_C(1536),
          1); // Stale prediction in the middle of ADDI.
    output_ready = 0;
    start(UINT64_C(1276));
    for (int repeat_index = 0; repeat_index < (15); ++repeat_index)
      falling();
    // S2 repairs the younger cut even while an older packet is held at Decode.
    // The older instruction must survive this speculative correction.
    CHECK(fetched_out.pvalid && fetched_out.pbits.ppc == UINT64_C(1276));
    CHECK(local_flushes == 1);
    output_ready = 1;
    expect_pc(UINT64_C(1280), UINT64_C(1284));
    CHECK(local_flushes == 1);
    expect_pc(UINT64_C(1284), UINT64_C(1288));

    initialize(2);
    train(UINT64_C(770), UINT64_C(1026),
          1); // Stale short length must not truncate the JAL.
    start(UINT64_C(770));
    expect_pc(UINT64_C(770), UINT64_C(774));
    CHECK(local_flushes == 1);

    initialize(2);
    train(UINT64_C(772), UINT64_C(2048),
          1); // Cut in the continuation halfword.
    start(UINT64_C(770));
    expect_pc(UINT64_C(770), UINT64_C(774));
    // Corrected S2 data bypasses into Decode before registered repair kills
    // younger requests. Count that recovery after its edge, not before bypass.
    for (int repeat_index = 0; repeat_index < (2); ++repeat_index)
      falling();
    CHECK(local_flushes == 1);

    initialize(2);
    train(UINT64_C(770), UINT64_C(1026), 0);
    fault_address = UINT64_C(772);
    start(UINT64_C(770));
    expect_pc(UINT64_C(770), UINT64_C(774), 1);

    initialize(3);
    train(UINT64_C(1282), UINT64_C(1536), 1);
    start(UINT64_C(1280));
    while (!memory_out.pflush)
      falling();
    restart_valid = 1;
    restart_pc =
        UINT64_C(1792); // Architectural recovery overrides local repair.
    falling();
    restart_valid = 0;
    expect_pc(UINT64_C(1792), UINT64_C(1796));

    initialize(4);
    train(UINT64_C(256), UINT64_C(512), 0, 0, 1, UINT64_C(1));
    train(UINT64_C(260), UINT64_C(512), 0, 0, 1, UINT64_C(1));
    fallback_trace_expect(UINT64_C(512), UINT64_C(260), 1);
    start(UINT64_C(256));
    expect_pc(UINT64_C(256), UINT64_C(512), 0, UINT64_C(1));
    expect_pc(UINT64_C(512), UINT64_C(260), 0, UINT64_C(2));
    expect_pc(UINT64_C(260), UINT64_C(512), 0, UINT64_C(1));
    expect_pc(UINT64_C(512), UINT64_C(264), 0, UINT64_C(2));
    expect_pc(UINT64_C(264), UINT64_C(268));
    CHECK(local_flushes == 0);

    initialize(5);
    train(UINT64_C(256), UINT64_C(770), 0, 0, 1, UINT64_C(1));
    fallback_trace_expect(UINT64_C(768), UINT64_C(260), 1);
    start(UINT64_C(256));
    expect_pc(UINT64_C(256), UINT64_C(770), 0, UINT64_C(1));
    expect_pc(UINT64_C(770), UINT64_C(260), 0, UINT64_C(2));
    expect_pc(UINT64_C(260), UINT64_C(264));

    initialize(6);
    train(UINT64_C(256), UINT64_C(1026), 0, 0, 1, UINT64_C(1));
    fallback_trace_expect(UINT64_C(1028), UINT64_C(260), 1);
    start(UINT64_C(256));
    expect_pc(UINT64_C(256), UINT64_C(1026), 0, UINT64_C(1));
    expect_pc(UINT64_C(1026), UINT64_C(260), 0, UINT64_C(2));
    expect_pc(UINT64_C(260), UINT64_C(264));

    initialize(7);
    train(UINT64_C(256), UINT64_C(1282), 0, 0, 1, UINT64_C(1));
    fallback_trace_expect(UINT64_C(1284), UINT64_C(260), 1);
    start(UINT64_C(256));
    expect_pc(UINT64_C(256), UINT64_C(1282), 0, UINT64_C(1));
    expect_pc(UINT64_C(1282), UINT64_C(1286));
    expect_pc(UINT64_C(1286), UINT64_C(260), 0, UINT64_C(2));
    expect_pc(UINT64_C(260), UINT64_C(264));

    // A return later in the packet cannot bypass an earlier conditional CFI.
    initialize(8);
    train(UINT64_C(256), UINT64_C(1536), 0, 0, 1, UINT64_C(1));
    start(UINT64_C(256));
    expect_pc(UINT64_C(256), UINT64_C(1536), 0, UINT64_C(1));
    expect_pc(UINT64_C(1536), UINT64_C(1538));
    expect_pc(UINT64_C(1538), UINT64_C(1540));

    initialize(8);
    train(UINT64_C(256), UINT64_C(1794), 0, 0, 1, UINT64_C(1));
    start(UINT64_C(256));
    expect_pc(UINT64_C(256), UINT64_C(1794), 0, UINT64_C(1));
    expect_pc(UINT64_C(1794), UINT64_C(1798));
    expect_pc(UINT64_C(1798), UINT64_C(1800));

    // Direct jumps use the same S2 fallback, learn in the BTB, and preserve
    // call/return RAS actions across repeated execution.
    initialize(9);
    fallback_trace_expect(UINT64_C(2048), UINT64_C(2304), 1);
    start(UINT64_C(2048));
    for (int repeat_index = 0; repeat_index < (2); ++repeat_index) {
      expect_pc(UINT64_C(2048), UINT64_C(2304), 0, UINT64_C(1));
      expect_pc(UINT64_C(2304), UINT64_C(2052), 0, UINT64_C(2));
      expect_pc(UINT64_C(2052), UINT64_C(2048));
    }

    // A warm return lookup overlaps the inner call's S2 push, so its S1
    // snapshot is the outer address. S2 must refresh it before popping once.
    for (int return_mode = 13; return_mode <= 15; return_mode++) {
      std::uint64_t return_pc =
          return_mode == 15 ? UINT64_C(770) : UINT64_C(768);
      initialize(return_mode);
      train(UINT64_C(256), UINT64_C(512), 0, 0, 1, UINT64_C(1));
      train(UINT64_C(512), return_pc, 0, 0, 1, UINT64_C(1));
      train(return_pc, UINT64_C(57005), return_mode == 13, 0, 1, UINT64_C(2));
      train(UINT64_C(516), UINT64_C(260), 0, 0, 1, UINT64_C(2));
      fallback_trace_expect(return_mode == 15 ? UINT64_C(772) : UINT64_C(768),
                            UINT64_C(516), 1);
      start(UINT64_C(256));
      expect_pc(UINT64_C(256), UINT64_C(512), 0, UINT64_C(1));
      expect_pc(UINT64_C(512), return_pc, 0, UINT64_C(1));
      expect_pc(return_pc, UINT64_C(516), 0, UINT64_C(2));
      expect_pc(UINT64_C(516), UINT64_C(260), 0, UINT64_C(2));
      expect_pc(UINT64_C(260), UINT64_C(264));
      CHECK(local_flushes == 0);
    }
    for (int return_mode = 14; return_mode <= 15; return_mode++) {
      std::uint64_t return_pc =
          return_mode == 15 ? UINT64_C(770) : UINT64_C(768);
      initialize(return_mode);
      train(UINT64_C(256), UINT64_C(512), 0, 0, 1, UINT64_C(1));
      train(UINT64_C(512), return_pc, 0, 0, 1, UINT64_C(1));
      train(return_pc, UINT64_C(57005), 0, 0, 1, UINT64_C(2));
      fault_address = return_mode == 15 ? UINT64_C(772) : UINT64_C(768);
      start(UINT64_C(256));
      expect_pc(UINT64_C(256), UINT64_C(512), 0, UINT64_C(1));
      expect_pc(UINT64_C(512), return_pc, 0, UINT64_C(1));
      expect_pc(return_pc, return_pc + 4, 1, 0, fault_address);
    }

    initialize(10);
    fallback_trace_expect(UINT64_C(2560), UINT64_C(2564), 1);
    start(UINT64_C(2560));
    for (int repeat_index = 0; repeat_index < (2); ++repeat_index) {
      expect_pc(UINT64_C(2560), UINT64_C(2564));
      expect_pc(UINT64_C(2564), UINT64_C(2560));
    }

    initialize(11);
    fallback_trace_expect(UINT64_C(2820), UINT64_C(3074), 1);
    start(UINT64_C(2818));
    expect_pc(UINT64_C(2818), UINT64_C(3074));
    for (int repeat_index = 0; repeat_index < (2); ++repeat_index)
      falling(); // Let the registered fallback target transfer before reset.

    initialize(12);
    fallback_trace_expect(UINT64_C(3328), UINT64_C(3334), 1);
    start(UINT64_C(3328));
    expect_pc(UINT64_C(3328), UINT64_C(3330));
    expect_pc(UINT64_C(3330), UINT64_C(3334));
    for (int repeat_index = 0; repeat_index < (2); ++repeat_index)
      falling();

    initialize(9);
    fault_address = UINT64_C(2048);
    start(UINT64_C(2048));
    expect_pc(UINT64_C(2048), UINT64_C(2052), 1, 0, UINT64_C(2048));

    initialize(4);
    start(UINT64_C(512));
    expect_pc(UINT64_C(512),
              UINT64_C(516)); // An empty RAS cannot supply a fallback target.

    initialize(4);
    train(UINT64_C(256), UINT64_C(512), 0, 0, 1, UINT64_C(1));
    fault_address = UINT64_C(512);
    start(UINT64_C(256));
    expect_pc(UINT64_C(256), UINT64_C(512), 0, UINT64_C(1));
    expect_pc(UINT64_C(512), UINT64_C(516), 1, 0,
              UINT64_C(512)); // A faulting return neither redirects nor pops.
    fault_address = UINT64_MAX;
    expect_pc(UINT64_C(516), UINT64_C(260), 0, UINT64_C(2));
    // The continuation triggers a fallback while its S0 target cannot transfer.
    // Retaining that offer must retain the exact S2 cause, not the younger S1.
    initialize(11);
    output_ready = 0;
    fallback_trace_expect(UINT64_C(2820), UINT64_C(3074), 3);
    start(UINT64_C(2818));
    while (!(memory_in.presponse.pvalid &&
             memory_in.presponse.pbits.presponse.pword == UINT64_C(69632)))
      falling();
    request_ready = 0;
    for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
      falling();
    request_ready = 1;
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    output_ready = 1;
    for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
      falling();
    fallback_trace_finish();
    ;
    throw Finished{};
  }
}

int main() {
  return run_test([] {
    cycle_limit = 5002;
    tick_extra = [] { fallback_trace_check(); };
    try {
      stimulus();
      for (;;)
        rising();
    } catch (const Finished &) {
    }
  });
}
