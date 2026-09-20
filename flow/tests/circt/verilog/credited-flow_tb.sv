// Simulates initial grants, stalls, credit recycling, ordering, and disabled grants.
// SPDX-License-Identifier: Apache-2.0
module credited_flow_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } ingress_forward_t;
  typedef struct packed { logic ready; } ingress_reverse_t;
  typedef struct packed { logic ready; } egress_reverse_t;
  typedef struct packed { logic valid; logic [7:0] bits; } egress_forward_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  logic grant_enable;
  ingress_forward_t ingress_in;
  egress_reverse_t egress_in;
  ingress_reverse_t ingress_out;
  egress_forward_t egress_out;
  logic [1:0] credit_count;
  logic [1:0] count;
  logic [1:0] reserved;

  CreditedRoundTrip dut (.*);
  always #5 clock = ~clock;

  task automatic tick;
    @(posedge clock);
    #1;
  endtask

  initial begin
    grant_enable = 1'b0;
    ingress_in = '{valid: 1'b1, bits: 8'hA1};
    egress_in = '{ready: 1'b0};
    tick();
    reset = 1'b0;

    #1;
    assert (!ingress_out.ready && !egress_out.valid)
      else $fatal(1, "traffic advanced without a prior credit");
    assert (credit_count == 0 && count == 0 && reserved == 0)
      else $fatal(1, "credited adapter status was not empty after reset");

    // The first grant is visible only after its clock edge. It cannot
    // authorize the already-present input during the grant cycle itself.
    grant_enable = 1'b1;
    #1;
    assert (!ingress_out.ready)
      else $fatal(1, "same-cycle credit incorrectly enabled transmission");
    tick();
    assert (ingress_out.ready)
      else $fatal(1, "registered credit did not enable the sender");
    assert (credit_count == 1 && count == 0 && reserved == 1)
      else $fatal(1, "first credit reservation was not reported");

    // Fill the two receiver slots while its egress is stalled.
    tick();
    ingress_in.bits = 8'hB2;
    tick();
    assert (!ingress_out.ready)
      else $fatal(1, "sender exceeded the two-credit limit");
    assert (egress_out.valid && egress_out.bits == 8'hA1)
      else $fatal(1, "first buffered payload was not retained");

    // Removing A returns a credit, but a zero-balance sender cannot spend that
    // credit until the following edge.
    ingress_in.bits = 8'hC3;
    egress_in.ready = 1'b1;
    #1;
    assert (!ingress_out.ready && egress_out.bits == 8'hA1)
      else $fatal(1, "returned credit was consumed too early");
    tick();
    assert (ingress_out.ready && egress_out.valid && egress_out.bits == 8'hB2)
      else $fatal(1, "credit return did not preserve FIFO ordering");

    // B dequeues while C arrives and a replacement credit is granted.
    tick();
    assert (egress_out.valid && egress_out.bits == 8'hC3)
      else $fatal(1, "simultaneous grant and transfer lost payload C");

    // Disabling new grants does not revoke the one already held remotely.
    grant_enable = 1'b0;
    ingress_in.valid = 1'b0;
    tick();
    assert (!egress_out.valid)
      else $fatal(1, "buffer did not drain payload C");

    ingress_in = '{valid: 1'b1, bits: 8'hD4};
    egress_in.ready = 1'b0;
    #1;
    assert (ingress_out.ready)
      else $fatal(1, "disabling grants revoked an outstanding credit");
    tick();
    assert (!ingress_out.ready && egress_out.valid && egress_out.bits == 8'hD4)
      else $fatal(1, "outstanding credit did not carry payload D");
    assert (credit_count == 0 && count == 1 && reserved == 1)
      else $fatal(1, "outstanding payload status was incorrect");

    ingress_in.valid = 1'b0;
    egress_in.ready = 1'b1;
    tick();
    assert (!egress_out.valid && !ingress_out.ready)
      else $fatal(1, "disabled receiver unexpectedly granted another credit");
    assert (credit_count == 0 && count == 0 && reserved == 0)
      else $fatal(1, "credited adapter did not fully drain");

    $display("Credited flow simulation passed");
    $finish;
  end
endmodule
