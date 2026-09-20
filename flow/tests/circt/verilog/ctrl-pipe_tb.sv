// Exercises latency, throughput, and backpressure in a two-stage token pipe.
// SPDX-License-Identifier: Apache-2.0
module ctrl_pipe_tb;
  typedef struct packed { logic valid; } forward_t;
  typedef struct packed { logic ready; } reverse_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  forward_t ingress_in;
  reverse_t egress_in;
  reverse_t ingress_out;
  forward_t egress_out;

  CtrlPipe dut (.*);
  always #5 clock = ~clock;
  task automatic tick; @(posedge clock); #1; endtask

  initial begin
    ingress_in = '{valid: 1'b0};
    egress_in = '{ready: 1'b0};
    tick();
    assert (!egress_out.valid && ingress_out.ready)
      else $fatal(1, "CtrlPipe reset did not empty both stages");

    reset = 1'b0;
    egress_in.ready = 1'b1;
    ingress_in.valid = 1'b1;
    tick();
    assert (!egress_out.valid)
      else $fatal(1, "CtrlPipe produced a token one cycle too early");
    tick();
    assert (egress_out.valid)
      else $fatal(1, "CtrlPipe lost the first token");

    egress_in.ready = 1'b0;
    #1;
    assert (!ingress_out.ready && egress_out.valid)
      else $fatal(1, "stalled full CtrlPipe did not apply backpressure");
    tick();
    assert (egress_out.valid)
      else $fatal(1, "stalled CtrlPipe withdrew its token");

    ingress_in.valid = 1'b0;
    egress_in.ready = 1'b1;
    tick();
    assert (egress_out.valid)
      else $fatal(1, "CtrlPipe lost its second token while draining");
    tick();
    assert (!egress_out.valid)
      else $fatal(1, "CtrlPipe did not become empty");
    $finish;
  end
endmodule
