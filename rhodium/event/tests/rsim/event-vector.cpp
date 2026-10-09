// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
extern "C" void vector_trace_bind();
extern "C" unsigned vector_trace_response();
extern "C" void vector_trace_reorder_pair();
extern "C" void
vector_trace_sample(unsigned rst, unsigned launch, unsigned insn,
                    unsigned length, unsigned issue, unsigned tag, unsigned mem,
                    unsigned enable, unsigned commit, unsigned status,
                    unsigned delayed, unsigned response, unsigned response_tag,
                    unsigned cancel, unsigned issue_done, unsigned sequenced);
extern "C" void vector_trace_check();
extern "C" void vector_trace_finish();
constexpr unsigned TAG_BITS = 2;
bool sampled_launch{}, sampled_commit{};
void tick() {
  eval();
  sampled_launch = request_valid && request_ready;
  sampled_commit = committed;
  vector_trace_sample(((reset)&low_mask(32)), ((sampled_launch)&low_mask(32)),
                      instruction, ((vl)&low_mask(32)), ((issued)&low_mask(32)),
                      ((issue_tag)&low_mask(32)), ((memory)&low_mask(32)),
                      ((enabled)&low_mask(32)), ((committed)&low_mask(32)),
                      ((disposition)&low_mask(32)), ((slow)&low_mask(32)),
                      ((response_valid && response_ready) & low_mask(32)),
                      ((response_tag)&low_mask(32)), ((cancel)&low_mask(32)),
                      ((issue_done)&low_mask(32)), ((sequenced)&low_mask(32)));
  tick_model();
  vector_trace_check();
}
void responses() {
  std::uint32_t result;
  result = vector_trace_response();
  response_valid = ((result >> 8) & 1);
  response_tag = (result & 3);
}
void run_back_to_back_compute(std::uint32_t insn, int length) {
  int launches;
  bool finished;
  instruction = insn;
  vl = ((length)&low_mask(64));
  request_valid = 1;
  slow = 0;
  launches = 0;
  finished = 0;
  disposition = 0;
  issue_ready = 1;
  response_valid = 0;
  for (int step = 0; step < 600 && !finished; ++step) {
    issue_ready = step % 7 != 2 && step % 7 != 3;
    responses();
    tick();
    if (sampled_launch) {
      ++launches;
      if (launches == 2)
        request_valid = 0;
    }
    if (launches == 2 && !active)
      finished = 1;
  }
  CHECK(finished);
  response_valid = 0;
  issue_ready = 1;
  for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
    tick();
}
void run_macro(std::uint32_t insn, int length, bool delayed = 0,
               int recovery = 0, bool reset_tail = 0) {
  int accepted;
  bool recovered, finished;
  instruction = insn;
  vl = ((length)&low_mask(64));
  request_valid = 1;
  slow = delayed;
  accepted = 0;
  recovered = 0;
  finished = 0;
  disposition = 0;
  issue_ready = 1;
  response_valid = 0;
  do
    tick();
  while (!sampled_launch);
  request_valid = 0;
  for (int step = 0; step < 600 && !finished; ++step) {
    issue_ready = step % 7 != 2 && step % 7 != 3;
    disposition = (!recovered && recovery > 0 && recovery < 4 && accepted == 1)
                      ? ((recovery)&low_mask(2))
                      : 0;
    cancel = !recovered && recovery == 4 && accepted == 1;
    responses();
    tick();
    if (cancel)
      recovered = 1;
    if (sampled_commit) {
      if (disposition != 0)
        recovered = 1;
      else
        ++accepted;
    }
    if (reset_tail && accepted == 2) {
      reset = 1;
      response_valid = 0;
      disposition = 0;
      tick();
      reset = 0;
      finished = 1;
    } else if (!active)
      finished = 1;
  }
  CHECK(finished);
  disposition = 0;
  response_valid = 0;
  issue_ready = 1;
  cancel = 0;
  for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
    tick();
}
int main() {
  return run_test([] {
    reset = 1;
    instruction = 0;
    vl = 0;
    vstart = 0;
    vtype = 24;
    request_valid = 0;
    issue_ready = 1;
    cancel = 0;
    slow = 0;
    response_valid = 0;
    disposition = 0;
    response_tag = 0;
    vector_trace_bind();
    tick();
    reset = 0;

    vtype = 27;
    run_back_to_back_compute(UINT64_C(41944151), 16);
    run_back_to_back_compute(UINT64_C(41944151), 1);
    run_macro(UINT64_C(41944151), 0);
    vector_trace_reorder_pair();
    run_macro(UINT64_C(33584135), 16, 1);
    run_macro(UINT64_C(33584135), 8, 1, 1);
    run_macro(UINT64_C(33584135), 8, 1, 2);
    run_macro(UINT64_C(33584135), 8, 1, 4);
    run_macro(UINT64_C(50361351), 8, 0, 3);
    run_macro(UINT64_C(33584167), 8);
    run_macro(UINT64_C(33584135), 8, 1, 0, 1);
    run_macro(UINT64_C(41944151), 16);
    vtype = 24;
    run_macro(UINT64_C(1577070679), 2);
    vtype = 27;
    run_macro(UINT64_C(8389719), 8);
    vector_trace_finish();
  });
}
