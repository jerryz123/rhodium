// Checks repeated-instance registration, unchanged transfers, and reset rebinding.
// SPDX-License-Identifier: Apache-2.0
module event_instance_tb #(parameter bit BAD = 0);
  typedef struct packed { logic valid; logic [7:0] bits; } forward_t;
  logic clock = 0, reset = 1;
  logic [7:0] chip_id = 0, hart0 = 0, hart1 = 0;
  logic [63:0] bank_id = 0;
  forward_t source[3], sink[3], previous[3];
  EventInstances dut(.clock(clock), .reset(reset), .chip_id(chip_id),
    .hart0(hart0), .hart1(hart1), .bank_id(bank_id),
    .sources_0_in(source[0]), .sources_1_in(source[1]), .sources_2_in(source[2]),
    .sinks_0_out(sink[0]), .sinks_1_out(sink[1]), .sinks_2_out(sink[2]),
    .__event_activity());
  always #5 clock = ~clock;
  import "DPI-C" function void event_instance_bind();
  import "DPI-C" function void event_instance_sample(input int unsigned rst,
    input int unsigned inputs, input int unsigned outputs,
    input int unsigned chip, input int unsigned a, input int unsigned b,
    input longint unsigned bank);
  import "DPI-C" function void event_instance_check();
  import "DPI-C" function void event_instance_finish();
  initial begin
    event_instance_bind();
    foreach (source[i]) begin source[i] = '0; previous[i] = '0; end
    for (int step = 0; step < 26; ++step) begin
      int cycle;
      @(negedge clock);
      reset = step < 2 || (step >= 13 && step < 15);
      cycle = step < 13 ? step - 2 : step - 15;
      // IDs may change before the first event; siblings deliberately share ID 7.
      chip_id = 8'(cycle < 2 ? step : step < 13 ? 3 : 4);
      hart0 = 8'(cycle < 2 ? step : step < 13 ? 7 : 9);
      hart1 = step < 13 || cycle < 2 ? hart0 : 8'd10;
      bank_id = cycle < 5 ? 64'(step) : step < 13 ? 64'hffffffffffffffff : 64'h8000000000000000;
      if (BAD && step == 8) hart0 = 8'd8;
      foreach (source[i]) begin
        source[i].valid = !reset && (i == 2 ? cycle == 5 : cycle == 2 || cycle == 3);
        source[i].bits = 8'(42 + cycle);
      end
      @(posedge clock);
      if (!reset) foreach (sink[i]) begin
        assert (sink[i].valid == previous[i].valid);
        if (sink[i].valid) assert (sink[i].bits == previous[i].bits);
      end
      event_instance_sample(32'(reset), {29'b0,source[2].valid,source[1].valid,source[0].valid},
        {29'b0,sink[2].valid,sink[1].valid,sink[0].valid}, 32'(chip_id),32'(hart0),32'(hart1),bank_id);
      foreach (previous[i]) previous[i] = reset ? '0 : source[i];
      #1; event_instance_check();
    end
    event_instance_finish();
    $finish;
  end
endmodule
