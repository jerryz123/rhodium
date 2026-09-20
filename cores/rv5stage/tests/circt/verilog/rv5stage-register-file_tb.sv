// Verifies simultaneous writes, write-first reads, retention, and x0 behavior.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_register_file_tb;
  typedef struct packed {
    logic [4:0] address;
    logic [63:0] data;
  } write_bits_t;
  typedef struct packed {
    logic valid;
    write_bits_t bits;
  } write_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  logic [4:0] read_address_1;
  logic [4:0] read_address_2;
  write_t commit_write;
  write_t load_completion_write;
  logic [63:0] read_data_1;
  logic [63:0] read_data_2;

  RV5StageRegisterFile dut (
    .clock(clock),
    .reset(reset),
    .read_address_1(read_address_1),
    .read_address_2(read_address_2),
    .writes_0_in(commit_write),
    .writes_1_in(load_completion_write),
    .read_data_1(read_data_1),
    .read_data_2(read_data_2)
  );
  always #5 clock = ~clock;

  initial begin
    read_address_1 = 5'd5;
    read_address_2 = 5'd6;
    commit_write = '0;
    load_completion_write = '0;
    repeat (2) @(posedge clock);
    #1;
    reset = 1'b0;

    commit_write.valid = 1'b1;
    commit_write.bits.address = 5'd5;
    commit_write.bits.data = 64'd11;
    load_completion_write.valid = 1'b1;
    load_completion_write.bits.address = 5'd6;
    load_completion_write.bits.data = 64'd22;
    #1;
    assert (read_data_1 == 64'd11 && read_data_2 == 64'd22)
      else $fatal(1, "simultaneous writes were not write-first");
    @(posedge clock);
    #1;
    commit_write.valid = 1'b0;
    load_completion_write.valid = 1'b0;
    assert (read_data_1 == 64'd11 && read_data_2 == 64'd22)
      else $fatal(1, "simultaneous writes were not retained");

    read_address_1 = 5'd0;
    read_address_2 = 5'd7;
    commit_write.valid = 1'b1;
    commit_write.bits.address = 5'd0;
    commit_write.bits.data = 64'hffffffffffffffff;
    load_completion_write.valid = 1'b1;
    load_completion_write.bits.address = 5'd7;
    load_completion_write.bits.data = 64'd33;
    #1;
    assert (read_data_1 == 64'd0 && read_data_2 == 64'd33)
      else $fatal(1, "x0 or second-port bypass behavior was incorrect");
    @(posedge clock);
    #1;
    commit_write.valid = 1'b0;
    load_completion_write.valid = 1'b0;
    assert (read_data_1 == 64'd0 && read_data_2 == 64'd33)
      else $fatal(1, "x0 or second-port retention behavior was incorrect");

    $display("RV5Stage two-write register file passed");
    $finish;
  end
endmodule
