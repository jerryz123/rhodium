// Violates the credited monitor by transmitting without a previously held credit.
// SPDX-License-Identifier: Apache-2.0
module credited_monitor_underflow_tb;
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
    payload = 8'hA5;
    link_in = '{credit: 1'b0};
    @(posedge clock);
    #1;
    reset = 1'b0;
    send = 1'b1;
    @(posedge clock);
    #1;
    $fatal(1, "credited underflow assertion did not fire");
  end
endmodule
