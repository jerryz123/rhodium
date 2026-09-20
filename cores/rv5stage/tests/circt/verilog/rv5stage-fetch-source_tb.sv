// Exercises fetch candidate priority, admission isolation, blocked replacement, continuation, and reset.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_fetch_source_tb;
  typedef struct packed { logic valid; logic [63:0] bits; } pc_t;
  typedef struct packed { logic valid; RV5StageFetchAttempt bits; } attempt_t;
  typedef struct packed { logic ready; } ready_t;
  logic clock=0, reset=1, active=0, space=0, clear=0;
  pc_t restart_in;
  attempt_t replay_in, attempts_out, stage1_out;
  ready_t attempts_in;
  EventFetchSource dut(.*);
  always #5 clock=~clock;
  import "DPI-C" function void fetch_source_bind();
  import "DPI-C" function void fetch_source_sample(int unsigned reset, active, space, clear,
      int unsigned restart, longint unsigned restart_pc, int unsigned replay, longint unsigned replay_pc,
      int unsigned replay_cont, longint unsigned replay_target, int unsigned valid, ready,
      longint unsigned pc, int unsigned cont, longint unsigned target, int unsigned stage_valid, longint unsigned stage_pc);
  import "DPI-C" function void fetch_source_check();
  import "DPI-C" function void fetch_source_finish();
  always @(posedge clock) begin
    fetch_source_sample(int'(reset),int'(active),int'(space),int'(clear),int'(restart_in.valid),restart_in.bits,
        int'(replay_in.valid),replay_in.bits.pc,int'(replay_in.bits.continuation),replay_in.bits.continuation_target,
        int'(attempts_out.valid),int'(attempts_in.ready),attempts_out.bits.pc,int'(attempts_out.bits.continuation),
        attempts_out.bits.continuation_target,int'(stage1_out.valid),stage1_out.bits.pc);
    #1; fetch_source_check();
  end
  task automatic tick; @(posedge clock); #2; endtask
  int unsigned rng=32'h76543210;
  function automatic int unsigned random_word();
    rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng;
  endfunction
  initial begin
    fetch_source_bind(); restart_in='0; replay_in='0; attempts_in='0;
    tick(); reset=0;
    // A root seed, then a blocked restart accepted after the one-cycle command disappears.
    active=1; space=1; attempts_in.ready=1; repeat(3) tick();
    restart_in.valid=1; restart_in.bits=64'h1002; active=0; space=0; attempts_in.ready=0; tick();
    restart_in.valid=0; active=1; space=1; attempts_in.ready=1; tick();
    // A nonbackpressured replay replaces a blocked candidate and carries its continuation.
    attempts_in.ready=0; replay_in.valid=1; replay_in.bits.pc=64'h1002;
    replay_in.bits.continuation=1; replay_in.bits.continuation_target=64'h2000; tick();
    replay_in.valid=0; attempts_in.ready=1; repeat(4) tick();
    // Duplicate PCs distinguish occurrence ownership from payload equality.
    repeat(1000) begin
      automatic int unsigned r=random_word();
      reset=(r[7:0]==0); active=r[0]; space=r[1]; clear=(r[5:2]==0);
      restart_in.valid=(r[8:6]==0); restart_in.bits=64'h1002;
      replay_in.valid=(r[10:9]==0); replay_in.bits.pc=64'h1002;
      replay_in.bits.continuation=r[11]; replay_in.bits.continuation_target=64'h2000;
      attempts_in.ready=r[12]; tick();
    end
    reset=0; restart_in.valid=0; replay_in.valid=0; clear=0;
    active=1; space=1; attempts_in.ready=1; repeat(4) tick();
    fetch_source_finish(); $finish;
  end
  initial begin #20000; $fatal(1,"fetch-source timeout"); end
endmodule
