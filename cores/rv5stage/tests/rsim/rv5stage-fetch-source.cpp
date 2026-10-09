// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
extern "C" void fetch_source_bind();
extern "C" void fetch_source_sample(
    unsigned reset, unsigned active, unsigned space, unsigned clear,
    unsigned restart, std::uint64_t restart_pc, unsigned replay,
    std::uint64_t replay_pc, unsigned replay_cont, std::uint64_t replay_target,
    unsigned valid, unsigned ready, std::uint64_t pc, unsigned cont,
    std::uint64_t target, unsigned stage_valid, std::uint64_t stage_pc);
extern "C" void fetch_source_check();
extern "C" void fetch_source_finish();
void tick() {
  eval();

  fetch_source_sample(
      unsigned(reset), unsigned(active), unsigned(space), unsigned(clear),
      unsigned(restart_in.pvalid), restart_in.pbits, unsigned(replay_in.pvalid),
      replay_in.pbits.ppc, unsigned(replay_in.pbits.pcontinuation),
      replay_in.pbits.pcontinuation_utarget, unsigned(attempts_out.pvalid),
      unsigned(attempts_in.pready), attempts_out.pbits.ppc,
      unsigned(attempts_out.pbits.pcontinuation),
      attempts_out.pbits.pcontinuation_utarget, unsigned(stage1_out.pvalid),
      stage1_out.pbits.ppc);
  tick_model();
  fetch_source_check();
}
std::uint32_t rng = UINT64_C(1985229328);
std::uint32_t random_word() {

  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}
int main() {
  return run_test([] {
    reset = 1;
    active = 0;
    space = 0;
    clear = 0;
    fetch_source_bind();
    restart_in = {};
    replay_in = {};
    attempts_in = {};
    tick();
    reset = 0;

    active = 1;
    space = 1;
    attempts_in.pready = 1;
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      tick();
    restart_in.pvalid = 1;
    restart_in.pbits = UINT64_C(4098);
    active = 0;
    space = 0;
    attempts_in.pready = 0;
    tick();
    restart_in.pvalid = 0;
    active = 1;
    space = 1;
    attempts_in.pready = 1;
    tick();

    attempts_in.pready = 0;
    replay_in.pvalid = 1;
    replay_in.pbits.ppc = UINT64_C(4098);
    replay_in.pbits.pcontinuation = 1;
    replay_in.pbits.pcontinuation_utarget = UINT64_C(8192);
    tick();
    replay_in.pvalid = 0;
    attempts_in.pready = 1;
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      tick();

    for (unsigned repeat_index = 0; repeat_index < (1000); ++repeat_index) {
      std::uint32_t r = random_word();
      reset = (slice(r, 7, 0) == 0);
      active = (r & 1);
      space = ((r >> 1) & 1);
      clear = (slice(r, 5, 2) == 0);
      restart_in.pvalid = (slice(r, 8, 6) == 0);
      restart_in.pbits = UINT64_C(4098);
      replay_in.pvalid = (slice(r, 10, 9) == 0);
      replay_in.pbits.ppc = UINT64_C(4098);
      replay_in.pbits.pcontinuation = ((r >> 11) & 1);
      replay_in.pbits.pcontinuation_utarget = UINT64_C(8192);
      attempts_in.pready = ((r >> 12) & 1);
      tick();
    }
    reset = 0;
    restart_in.pvalid = 0;
    replay_in.pvalid = 0;
    clear = 0;
    active = 1;
    space = 1;
    attempts_in.pready = 1;
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      tick();
    fetch_source_finish();
  });
}
