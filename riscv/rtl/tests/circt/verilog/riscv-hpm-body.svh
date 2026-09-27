// Scores HPM filtering, split writes, overflow/rearming, and concurrent updates.
// SPDX-License-Identifier: Apache-2.0
  localparam int WRITE_WIDTH = XLEN + 1 + (XLEN == 32 ? 1 : 0);
  localparam logic [63:0] OF = 64'h8000000000000000;
  localparam logic [63:0] CONTROL_MASK = HYPERVISOR ? 64'hfcffffffffffffff : 64'hf0ffffffffffffff;
  logic clock = 0, reset = 1, inhibit = 0;
  struct packed {
    logic valid;
    struct packed { logic [1:0] privilege; logic virtualized; } bits;
  } event_in;
  logic [WRITE_WIDTH-1:0] counter_write_in = 0, selector_write_in = 0;
  logic [63:0] counter, selector;
  logic overflow_out;
  logic [63:0] expected_count = 0, expected_control = 0;
  int checks = 0;
  RiscvHpmCounter dut (.*);
  always #5 clock = ~clock;

  function automatic logic [WRITE_WIDTH-1:0] write_port(
    input bit valid, input bit high, input logic [63:0] value
  );
    logic [WRITE_WIDTH-1:0] result;
    result = WRITE_WIDTH'(value);
    result[WRITE_WIDTH-1] = valid;
    if (XLEN == 32) result[XLEN] = high;
    return result;
  endfunction

  task automatic step(
    input bit pulse = 0, input logic [1:0] mode = 3, input bit guest = 0,
    input bit stopped = 0, input bit cw = 0, input bit ch = 0,
    input logic [63:0] cv = 0, input bit sw = 0, input bit sh = 0,
    input logic [63:0] sv = 0
  );
    int filter_bit;
    bit blocked, increment, wrap, request;
    @(negedge clock);
    event_in = {pulse, mode, guest};
    inhibit = stopped;
    counter_write_in = write_port(cw, ch, cv);
    selector_write_in = write_port(sw, sh, sv);
    filter_bit = mode == 3 ? 62 : mode == 1 ? 61 : 60;
    if (HYPERVISOR && guest && mode != 3) filter_bit -= 2;
    blocked = expected_control[filter_bit];
    increment = pulse && !stopped && !blocked && !cw && !sw;
    wrap = increment && expected_count == '1;
    request = wrap && !expected_control[63];
    #1;
    assert (overflow_out == request)
      else $fatal(1, "RV%0d overflow mismatch at check %0d", XLEN, checks);
    if (cw) begin
      if (XLEN == 64) expected_count = cv;
      else if (ch) expected_count[63:32] = cv[31:0];
      else expected_count[31:0] = cv[31:0];
    end else if (increment) expected_count++;
    if (sw) begin
      if (XLEN == 64) expected_control = sv;
      else if (sh) expected_control[63:32] = sv[31:0];
      else expected_control[31:0] = sv[31:0];
      expected_control &= CONTROL_MASK;
    end else if (wrap) expected_control[63] = 1;
    @(posedge clock);
    #1;
    assert (counter == expected_count && selector == expected_control)
      else $fatal(1, "RV%0d state mismatch at %0d: count=%h/%h control=%h/%h",
                  XLEN, checks, counter, expected_count, selector, expected_control);
    checks++;
  endtask

  task automatic set_count(input logic [63:0] value);
    step(0, 3, 0, 0, 1, 0, value);
    if (XLEN == 32) step(0, 3, 0, 0, 1, 1, value >> 32);
  endtask

  task automatic set_selector(input logic [63:0] value);
    step(0, 3, 0, 0, 0, 0, 0, 1, 0, value);
    if (XLEN == 32) step(0, 3, 0, 0, 0, 0, 0, 1, 1, value >> 32);
  endtask

  initial begin
    event_in = 0;
    repeat (2) @(posedge clock);
    #1;
    reset = 0;
    assert (counter == 0 && selector == 0) else $fatal(1, "HPM reset");
    // Sweep every filter combination independently of event selection.
    for (int filters = 0; filters < 32; filters++) begin
      set_selector((64'(filters) << 58) | 64'h0055aa1122334455);
      for (int mode = 0; mode < 5; mode++) begin
        step(1, mode == 0 ? 2'd3 : mode == 1 || mode == 3 ? 2'd1 : 2'd0, mode >= 3);
        step(1, mode == 0 ? 2'd3 : mode == 1 || mode == 3 ? 2'd1 : 2'd0, mode >= 3, 1);
        step(0, mode == 0 ? 2'd3 : mode == 1 || mode == 3 ? 2'd1 : 2'd0, mode >= 3);
      end
    end
    set_selector('1); // Reserved and unsupported filter bits read zero.
    set_selector(0);
    set_count(64'h12345678ffffffff);
    step(1); // Carry across the low half is not 64-bit overflow.
    set_count('1);
    step(1); // First overflow raises one request and sets OF.
    step(1); // OF does not stop counting or continually assert a request.
    set_count('1);
    step(1); // A second wrap with OF set cannot generate another request.
    set_selector(0);
    set_count('1);
    step(1); // Software rearming permits the next overflow.
    set_selector(OF);
    set_count('1);
    step(1); // Software setting OF disables requests, not counting.
    set_selector(0);
    set_count('1);
    step(1, 3, 0, 0, 1, 0, 7); // Explicit counter write wins over wrap.
    set_count('1);
    step(1, 3, 0, 0, 0, 0, 0, 1, 0, 0); // Selector write also suppresses wrap.
    step(1); // Preserved count wraps on the following accepted event.
    step(1, 3, 0, 0, 1, 0, 42, 1, 0, 5); // Independent writes both survive.
    if (XLEN == 32) begin
      step(1, 3, 0, 0, 1, 1, 64'h76543210, 1, 1, 64'h4055aaaa);
      step(0, 3, 0, 0, 0, 0, 0, 1, 0, 64'hdeadbeef);
    end
    // Reset has priority over an enabled event and both writes.
    @(negedge clock);
    reset = 1;
    event_in = {1'b1, 2'd3, 1'b0};
    inhibit = 0;
    counter_write_in = write_port(1, 0, '1);
    selector_write_in = write_port(1, 0, '1);
    @(posedge clock);
    #1;
    assert (counter == 0 && selector == 0) else $fatal(1, "HPM reset priority");
    $display("RV%0d HPM passed %0d checks", XLEN, checks);
    $finish;
  end
