// Violates the credited monitor by granting beyond the configured link limit.
// SPDX-License-Identifier: Apache-2.0
module credited_monitor_overgrant_tb;
  typedef struct packed { logic credit; } link_reverse_t;
  typedef struct packed { logic valid; logic [7:0] bits; } link_forward_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  logic send;
  logic [7:0] payload;
  link_reverse_t link_in;
  link_forward_t link_out;

  MonitoredCreditedTransmitter dut (.*);
  always #5 clock = ~clock;

  initial begin
    send = 1'b0;
    payload = 8'h00;
    link_in = '{credit: 1'b0};
    @(posedge clock);
    #1;
    reset = 1'b0;
    link_in.credit = 1'b1;
    repeat (3) begin
      @(posedge clock);
      #1;
    end
    $fatal(1, "credited overgrant assertion did not fire");
  end
endmodule
