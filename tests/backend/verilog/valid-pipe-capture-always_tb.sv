// Checks always-capture payload latency through bubbles, back-to-back tokens, and reset.
module valid_pipe_capture_always_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } valid_t;
  logic clock = 1'b0;
  logic reset = 1'b1;
  valid_t ingress_in;
  valid_t egress_out;
  valid_t previous_input;
  logic previous_reset;
  integer samples = 0;

  ValidPipeAlwaysCapture dut (.*);
  always #5 clock = ~clock;

  task automatic sample(input logic rst, input logic valid, input logic [7:0] bits);
    @(negedge clock);
    reset = rst;
    ingress_in = '{valid: valid, bits: bits};
    @(posedge clock);
    #1;
    if (samples != 0) begin
      assert (egress_out.valid == (!rst && !previous_reset && previous_input.valid))
        else $fatal(1, "always-capture changed token latency or reset behavior");
      assert (egress_out.bits == previous_input.bits)
        else $fatal(1, "always-capture held a payload across an invalid cycle or reset");
    end
    previous_input = ingress_in;
    previous_reset = rst;
    samples++;
  endtask

  initial begin
    ingress_in = '0;
    sample(1, 0, 8'h01);
    sample(0, 1, 8'ha1);
    sample(0, 1, 8'hb2);
    sample(0, 0, 8'h33);
    sample(0, 0, 8'h44);
    sample(0, 1, 8'hc3);
    sample(1, 1, 8'hd4);
    sample(0, 0, 8'h55);
    sample(0, 1, 8'he5);
    sample(0, 0, 8'h66);
    sample(0, 0, 8'h77);
    $display("ValidPipe always-capture latency, bubbles, and reset passed");
    $finish;
  end
endmodule
