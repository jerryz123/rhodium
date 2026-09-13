// Verifies fair changed-state delivery, irrevocable stalls, and local state replication.
// SPDX-License-Identifier: Apache-2.0
module state_flow_tb;
  typedef struct packed {
    logic       enabled;
    logic [2:0] mode;
  } ReplicatedControl;
  typedef struct packed {
    logic [1:0]       index;
    ReplicatedControl value;
  } IndexedState;
  typedef struct packed {
    logic        valid;
    IndexedState bits;
  } change_forward_t;
  typedef struct packed {
    logic ready;
  } ready_t;
  typedef struct packed {
    logic             valid;
    ReplicatedControl bits;
  } replica_forward_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  ReplicatedControl [2:0] desired;
  ready_t changes_in;
  replica_forward_t replica_update_in;
  change_forward_t changes_out;
  ready_t replica_update_out;
  ReplicatedControl replica;

  StateFlowExample dut (
    .clock              (clock),
    .reset              (reset),
    .desired            (desired),
    .changes_in         (changes_in),
    .replica_update_in  (replica_update_in),
    .changes_out        (changes_out),
    .replica_update_out (replica_update_out),
    .replica            (replica)
  );

  always #5 clock = ~clock;

  task automatic tick;
    @(posedge clock);
    #1;
  endtask

  initial begin
    desired[0] = '{enabled: 1'b0, mode: 3'h0};
    desired[1] = '{enabled: 1'b0, mode: 3'h0};
    desired[2] = '{enabled: 1'b0, mode: 3'h0};
    changes_in = '{ready: 1'b0};
    replica_update_in = '{valid: 1'b0, bits: '{enabled: 1'b0, mode: 3'h0}};
    tick();
    reset = 1'b0;
    tick();
    assert (!changes_out.valid && !replica.enabled && replica.mode == 3'h0)
      else $fatal(1, "state flow did not reset to its initial value");
    assert (replica_update_out.ready)
      else $fatal(1, "state replica did not remain ready");

    desired[0] = '{enabled: 1'b1, mode: 3'h1};
    desired[1] = '{enabled: 1'b0, mode: 3'h2};
    tick();
    assert (changes_out.valid && changes_out.bits.index == 2'h0 &&
            changes_out.bits.value.enabled && changes_out.bits.value.mode == 3'h1)
      else $fatal(1, "state source did not select the first dirty index");

    desired[0] = '{enabled: 1'b1, mode: 3'h3};
    tick();
    assert (changes_out.valid && changes_out.bits.index == 2'h0 &&
            changes_out.bits.value.enabled && changes_out.bits.value.mode == 3'h1)
      else $fatal(1, "stalled state update was not irrevocable");

    changes_in.ready = 1'b1;
    tick();
    assert (changes_out.valid && changes_out.bits.index == 2'h1 &&
            !changes_out.bits.value.enabled && changes_out.bits.value.mode == 3'h2)
      else $fatal(1, "state source did not rotate to the next dirty index");
    tick();
    assert (changes_out.valid && changes_out.bits.index == 2'h0 &&
            changes_out.bits.value.enabled && changes_out.bits.value.mode == 3'h3)
      else $fatal(1, "state change during a stall was lost");
    tick();
    assert (!changes_out.valid)
      else $fatal(1, "state source repeated a published update");

    desired[0] = '{enabled: 1'b0, mode: 3'h0};
    tick();
    assert (changes_out.valid && changes_out.bits.index == 2'h0 &&
            !changes_out.bits.value.enabled && changes_out.bits.value.mode == 3'h0)
      else $fatal(1, "state source did not emit a return to the initial state");
    tick();

    replica_update_in = '{valid: 1'b1, bits: '{enabled: 1'b1, mode: 3'h5}};
    tick();
    replica_update_in.valid = 1'b0;
    assert (replica.enabled && replica.mode == 3'h5)
      else $fatal(1, "state replica did not retain an accepted update");

    $finish;
  end
endmodule
