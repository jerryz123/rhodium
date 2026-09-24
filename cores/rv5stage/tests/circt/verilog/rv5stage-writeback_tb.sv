// Checks future-cycle collisions, simultaneous distinct reservations, shifting, and reset.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_writeback_tb;
  typedef struct packed {logic valid; logic [3:0] bits;} request_t;
  typedef struct packed {logic ready;} ready_t;
  logic clock = 0, reset = 1, pause = 0;
  request_t [2:0] reserve_in;
  ready_t [2:0] reserve_out;
  logic [7:0] occupied;
  logic [8:0] model = 0;
  int accepted = 0, blocked = 0, simultaneous = 0;
  RV5StageWritebackCalendar dut(.reserve_0_in(reserve_in[0]), .reserve_1_in(reserve_in[1]), .reserve_2_in(reserve_in[2]), .reserve_0_out(reserve_out[0]), .reserve_1_out(reserve_out[1]), .reserve_2_out(reserve_out[2]), .*);
  always #5 clock = ~clock;

  always @(posedge clock) begin
    logic [8:0] next_model;
    int count;
    if (reset) model = 0;
    else begin
      assert(occupied == model[7:0]) else $fatal(1, "write cycle mismatch");
      next_model = model;
      count = 0;
      for (int i = 0; i < 3; i++) begin
        assert(reserve_out[i].ready == (!pause && !next_model[reserve_in[i].bits])) else $fatal(1, "reservation arbitration mismatch client %0d", i);
        if (reserve_in[i].valid && reserve_out[i].ready) begin
          next_model[reserve_in[i].bits] = 1;
          count++;
          accepted++;
        end else if (reserve_in[i].valid) blocked++;
      end
      if (count > 1) simultaneous++;
      model = next_model >> 1;
    end
  end
  initial begin
    reserve_in = '0;
    for (int i = 0; i < 3; i++) reserve_in[i].bits = 1;
    repeat (3) @(negedge clock);
    reset = 0;
    for (int cycle = 0; cycle < 240; cycle++) begin
      pause = cycle % 23 >= 19;
      for (int i = 0; i < 3; i++) begin
        reserve_in[i].valid = cycle % 7 != i;
        reserve_in[i].bits = 4'(1 + ((cycle / 5 + i * (cycle % 3)) % 8));
      end
      @(negedge clock);
      if (cycle == 111) begin
        reset = 1;
        repeat (2) @(negedge clock);
        reset = 0;
      end
    end
    reserve_in = '0;
    for (int i = 0; i < 3; i++) reserve_in[i].bits = 1;
    repeat (9) @(negedge clock);
    assert(occupied == 0 && accepted > 30 && blocked > 30 && simultaneous > 10) else $fatal(1, "calendar coverage/drain failure");
    $display("writeback calendar passed: %0d reservations, %0d collisions, %0d parallel grants", accepted, blocked, simultaneous);
    $finish;
  end
endmodule
