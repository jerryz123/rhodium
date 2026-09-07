// Exercises default Valid pipe latency, bubble advancement, and invalid-cycle payload retention.
module valid_pipe_tb;
  typedef struct packed {
    logic       valid;
    logic [7:0] bits;
  } valid_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  valid_t ingress_in;
  valid_t egress_out;

  ValidDelay dut (
    .clock      (clock),
    .reset      (reset),
    .ingress_in (ingress_in),
    .egress_out (egress_out)
  );

  always #5 clock = ~clock;

  task automatic tick;
    @(posedge clock);
    #1;
  endtask

  initial begin
    ingress_in = '{valid: 1'b0, bits: 8'h00};
    tick();
    assert (!egress_out.valid)
      else $fatal(1, "ValidPipe reset did not clear every stage");

    reset = 1'b0;
    ingress_in = '{valid: 1'b1, bits: 8'ha1};
    tick();
    assert (!egress_out.valid)
      else $fatal(1, "two-stage ValidPipe produced data one cycle too early");

    ingress_in = '{valid: 1'b1, bits: 8'hb2};
    tick();
    assert (egress_out.valid && egress_out.bits == 8'ha1)
      else $fatal(1, "ValidPipe did not preserve its two-cycle latency");

    ingress_in = '{valid: 1'b0, bits: 8'h00};
    tick();
    assert (egress_out.valid && egress_out.bits == 8'hb2)
      else $fatal(1, "ValidPipe did not sustain one item per cycle");

    ingress_in = '{valid: 1'b1, bits: 8'hc3};
    tick();
    assert (!egress_out.valid)
      else $fatal(1, "ValidPipe did not advance an invalid cycle");
    assert (egress_out.bits == 8'hb2)
      else $fatal(1, "default ValidPipe did not retain its payload on an invalid cycle");

    ingress_in.valid = 1'b0;
    tick();
    assert (egress_out.valid && egress_out.bits == 8'hc3)
      else $fatal(1, "ValidPipe lost data following an invalid cycle");

    tick();
    assert (!egress_out.valid)
      else $fatal(1, "ValidPipe did not drain");

    $finish;
  end
endmodule
