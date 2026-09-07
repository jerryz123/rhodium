// Checks two-hart time packet stability, atomic reconstruction, and latest-snapshot coalescing.
module tiled_time_tb;
  logic clock = 0;
  logic reset = 1;
  struct packed { logic valid; logic [63:0] bits; } update_in;
  logic [1:0] accepting = 0;
  logic [127:0] hart_time;
  TiledTimeBeat offered;
  logic offer_valid;
  logic transferred;
  TiledTimeBeat held;
  localparam logic [63:0] FIRST = 64'h0123_4567_89ab_cdef;
  localparam logic [63:0] SUPERSEDED = 64'h1111_2222_3333_4444;
  localparam logic [63:0] LATEST = 64'hfedc_ba98_7654_3210;
  int beats = 0;
  logic [63:0] expected_snapshot;

  TiledTimeFixture dut (.*);
  always #5 clock = ~clock;
  always_comb expected_snapshot = beats < 8 ? FIRST : LATEST;
  always @(posedge clock) begin
    if (!reset && transferred) begin
      assert (beats < 16 && offered.hart == 1'((beats / 4) % 2) &&
              offered.flit.first == (beats % 4 == 0) &&
              offered.flit.last == (beats % 4 == 3) &&
              offered.flit.payload == 16'(expected_snapshot >> (16 * (beats % 4))))
        else $fatal(1, "time sweep changed destination, framing, or snapshot");
      beats <= beats + 1;
    end
  end
  always @(negedge clock) begin
    if (!reset) begin
      assert (hart_time[63:0] == (beats >= 12 ? LATEST : beats >= 4 ? FIRST : 64'd0) &&
              hart_time[127:64] == (beats >= 16 ? LATEST : beats >= 8 ? FIRST : 64'd0))
        else $fatal(1, "receiver exposed a partial timestamp");
    end
  end
  task automatic tick;
    @(posedge clock);
    #1;
  endtask
  task automatic update_time(input logic [63:0] value);
    @(negedge clock);
    update_in = '{valid: 1'b1, bits: value};
    tick();
    @(negedge clock);
    update_in.valid = 0;
  endtask
  initial begin
    update_in = '0;
    repeat (2) tick();
    @(negedge clock); reset = 0;
    update_time(FIRST);
    wait (offer_valid);
    #1; held = offered;
    update_time(SUPERSEDED);
    update_time(LATEST);
    repeat (3) begin
      tick();
      assert (offer_valid && offered == held && !transferred && hart_time == 0)
        else $fatal(1, "stalled time offer changed or partially updated a hart");
    end
    @(negedge clock); accepting = 2'b01;
    wait (beats == 4);
    #1;
    assert (hart_time == {64'd0, FIRST})
      else $fatal(1, "first hart did not reconstruct its complete snapshot");
    wait (offer_valid && offered.hart == 1);
    #1; held = offered;
    repeat (3) begin
      tick();
      assert (offered == held && offer_valid && beats == 4 && hart_time == {64'd0, FIRST})
        else $fatal(1, "second-hart stall changed the active sweep");
    end
    @(negedge clock); accepting = 2'b11;
    wait (beats == 8);
    #1;
    assert (hart_time == {FIRST, FIRST}) else $fatal(1, "first sweep was not consistent across harts");
    wait (beats == 16);
    #1;
    assert (hart_time == {LATEST, LATEST}) else $fatal(1, "latest pending snapshot did not reach both harts");
    repeat (12) tick();
    assert (!offer_valid && beats == 16) else $fatal(1, "superseded snapshot produced another sweep");
    $display("Tiled time stability, reconstruction, and coalescing passed");
    $finish;
  end
  initial begin
    repeat (200) tick();
    $fatal(1, "time sweep timed out");
  end
endmodule
