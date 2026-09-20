// Verifies exactly-once token broadcast under independent recipient stalls.
// SPDX-License-Identifier: Apache-2.0
module ctrl_broadcast_tb;
  typedef struct packed { logic valid; } forward_t;
  typedef struct packed { logic ready; } reverse_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  forward_t ingress_in;
  reverse_t egress_0_in, egress_1_in, egress_2_in;
  reverse_t ingress_out;
  forward_t egress_0_out, egress_1_out, egress_2_out;

  CtrlBroadcast dut (.*);
  always #5 clock = ~clock;
  task automatic tick; @(posedge clock); #1; endtask

  initial begin
    ingress_in = '{valid: 1'b0};
    egress_0_in = '{ready: 1'b0};
    egress_1_in = '{ready: 1'b0};
    egress_2_in = '{ready: 1'b0};
    tick();
    reset = 1'b0;

    ingress_in.valid = 1'b1;
    assert (ingress_out.ready)
      else $fatal(1, "empty CtrlBroadcast did not accept a token");
    tick();
    ingress_in.valid = 1'b0;
    assert (egress_0_out.valid && egress_1_out.valid &&
            egress_2_out.valid && !ingress_out.ready)
      else $fatal(1, "CtrlBroadcast did not present every copy");

    egress_0_in.ready = 1'b1;
    tick();
    assert (!egress_0_out.valid && egress_1_out.valid && egress_2_out.valid)
      else $fatal(1, "CtrlBroadcast repeated or lost recipient zero");
    egress_2_in.ready = 1'b1;
    tick();
    assert (!egress_0_out.valid && egress_1_out.valid && !egress_2_out.valid)
      else $fatal(1, "CtrlBroadcast repeated or lost recipient two");

    ingress_in.valid = 1'b1;
    egress_1_in.ready = 1'b1;
    #1;
    assert (ingress_out.ready)
      else $fatal(1, "CtrlBroadcast could not refill on final drain");
    tick();
    assert (egress_0_out.valid && egress_1_out.valid && egress_2_out.valid)
      else $fatal(1, "CtrlBroadcast refill did not reach every recipient");
    $finish;
  end
endmodule
