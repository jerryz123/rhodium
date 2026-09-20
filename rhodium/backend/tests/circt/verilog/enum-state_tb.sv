// Simulates enum switch transitions and recovery through the generated state machine.
// SPDX-License-Identifier: Apache-2.0
module enum_state_tb;
  logic       clock;
  logic       reset;
  logic [1:0] state_out;

  StateMachine dut (
    .clock     (clock),
    .reset     (reset),
    .state_out (state_out)
  );

  task automatic tick;
    #5 clock = 1'b1;
    #1;
    clock = 1'b0;
    #4;
  endtask

  initial begin
    clock = 1'b0;
    reset = 1'b1;
    tick();
    assert (state_out == 2'd0)
      else $fatal(1, "enum switch reset did not select Idle");

    reset = 1'b0;
    tick();
    assert (state_out == 2'd1)
      else $fatal(1, "enum switch did not select Running");
    tick();
    assert (state_out == 2'd2)
      else $fatal(1, "enum switch did not select Done");
    tick();
    assert (state_out == 2'd0)
      else $fatal(1, "enum switch did not return to Idle");
    $finish;
  end
endmodule
