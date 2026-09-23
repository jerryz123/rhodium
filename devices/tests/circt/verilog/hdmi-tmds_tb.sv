// Exhausts reachable TMDS disparity transitions and tests controls, gaps, and reset.
// SPDX-License-Identifier: Apache-2.0
module hdmi_tmds_tb;
  `include "devices/tests/circt/verilog/tmds-reference.svh"
  logic clock = 0, reset = 1;
  logic valid = 0, data_enable = 0, symbol_valid;
  logic [7:0] data = 0;
  logic [1:0] control = 0;
  logic [9:0] symbol;
  TMDSChannelEncoder dut (.*);
  always #5 clock = ~clock;

  int running = 0, transitions = 0;
  logic [9:0] expected = 10'b1101010100;
  int states[$], path[$];
  bit reached[33];
  bit active_codes[1024];
  int parent[33], parent_byte[33];
  int next_state, cursor;
  logic [9:0] unused_symbol;

  task automatic sample(input bit present, input bit active,
                        input logic [7:0] value, input logic [1:0] controls);
    logic [7:0] recovered, minimized;
    @(negedge clock);
    valid = present;
    data_enable = active;
    data = value;
    control = controls;
    if (present) expected = tmds_reference(value, controls, active, running);
    @(posedge clock);
    #1;
    assert(symbol_valid == present && symbol == expected)
      else $fatal(1, "TMDS mismatch byte=%h active=%b valid=%b got=%h expected=%h", value, active, present, symbol, expected);
    if (present && active) begin
      active_codes[symbol] = 1;
      minimized = symbol[9] ? ~symbol[7:0] : symbol[7:0];
      recovered[0] = minimized[0];
      for (int i = 1; i < 8; i++)
        recovered[i] = symbol[8] ? minimized[i] ^ minimized[i-1] : ~(minimized[i] ^ minimized[i-1]);
      assert(recovered == value) else $fatal(1, "TMDS decode mismatch");
      assert(!(symbol inside {10'h354, 10'h0ab, 10'h154, 10'h2ab}))
        else $fatal(1, "active data became a control symbol");
    end
  endtask

  task automatic restart_encoder;
    @(negedge clock);
    reset = 1;
    valid = 1;
    data_enable = 1;
    @(posedge clock);
    #1;
    assert(!symbol_valid && symbol == 10'b1101010100) else $fatal(1, "TMDS reset");
    running = 0;
    expected = 10'b1101010100;
    @(negedge clock);
    reset = 0;
    valid = 0;
  endtask

  initial begin
    // Discover the complete reference state graph and a byte path to each state.
    states.push_back(0);
    reached[16] = 1;
    for (int n = 0; n < states.size(); n++)
      for (int value = 0; value < 256; value++) begin
        next_state = states[n];
        unused_symbol = tmds_reference(8'(value), 0, 1, next_state);
        assert(next_state >= -16 && next_state <= 16) else $fatal(1, "reference disparity bound");
        if (!reached[next_state + 16]) begin
          reached[next_state + 16] = 1;
          parent[next_state + 16] = states[n];
          parent_byte[next_state + 16] = value;
          states.push_back(next_state);
        end
      end
    restart_encoder();
    for (int n = 0; n < states.size(); n++) begin
      path.delete();
      cursor = states[n];
      while (cursor != 0) begin
        path.push_front(parent_byte[cursor + 16]);
        cursor = parent[cursor + 16];
      end
      for (int value = 0; value < 256; value++) begin
        sample(1, 0, 8'hff, 2'(value));
        foreach (path[i]) sample(1, 1, 8'(path[i]), 0);
        assert(running == states[n]) else $fatal(1, "reference prefix");
        // Invalid blanking must not reset disparity or change the held symbol.
        sample(0, 0, 8'(~value), 2'(value + 1));
        sample(1, 1, 8'(value), 2'(value));
        sample(0, 1, 8'(~value), 0);
        transitions++;
      end
    end
    // Long streams expose state overflow and recurrence mistakes.
    for (int i = 0; i < 4096; i++) sample(1, 1, 8'(i * 73 + (i >> 4)), 0);
    sample(1, 1, 0, 0);
    restart_encoder();
    sample(0, 0, 8'hff, 3);
    sample(1, 1, 8'hff, 0);
    for (int i = 0; i < 4; i++) sample(1, 0, 8'ha5, 2'(i));
    // DVI 1.0 section 3.2.2 defines exactly 460 active-data characters.
    begin
      int count = 0;
      foreach (active_codes[i]) if (active_codes[i]) count++;
      assert(count == 460) else $fatal(1, "active TMDS alphabet has %0d symbols", count);
    end
    $display("TMDS passed %0d byte/disparity transitions across %0d reachable states, controls, gaps, reset, and long streams", transitions, states.size());
    $finish;
  end
  initial begin
    #1000000;
    $fatal(1, "TMDS timeout");
  end
endmodule
