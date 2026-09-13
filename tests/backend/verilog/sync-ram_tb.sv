// Verifies one-cycle read validity, write silence, and lane-masked updates.
// SPDX-License-Identifier: Apache-2.0
module sync_ram_tb;
  typedef struct packed {
    logic [1:0]  address;
    logic        write;
    logic [31:0] data;
    logic [3:0]  mask;
  } request_bits_t;
  typedef struct packed { logic valid; request_bits_t bits; } request_t;
  typedef struct packed { logic valid; logic [31:0] bits; } response_t;
  typedef struct packed { request_t request; } port_in_t;
  typedef struct packed { response_t response; } port_out_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  port_in_t port_in;
  port_out_t port_out;

  SyncRamExample dut (.*);
  always #5 clock = ~clock;

  task automatic tick;
    @(posedge clock);
    #1;
  endtask

  task automatic request_write(
    input logic [1:0] address,
    input logic [31:0] data,
    input logic [3:0] mask
  );
    port_in.request = '{valid: 1'b1,
                        bits: '{address: address,
                                write: 1'b1,
                                data: data,
                                mask: mask}};
    tick();
    port_in.request.valid = 1'b0;
    assert (!port_out.response.valid)
      else $fatal(1, "write unexpectedly produced a response");
  endtask

  task automatic request_read(input logic [1:0] address);
    port_in.request = '{valid: 1'b1,
                        bits: '{address: address,
                                write: 1'b0,
                                data: '0,
                                mask: '0}};
    tick();
    port_in.request.valid = 1'b0;
  endtask

  initial begin
    port_in = '0;
    tick();
    assert (!port_out.response.valid)
      else $fatal(1, "reset did not clear response valid");
    reset = 1'b0;

    request_write(2'd1, 32'h11223344, 4'b1111);
    request_read(2'd1);
    assert (port_out.response.valid && port_out.response.bits == 32'h11223344)
      else $fatal(1, "read did not respond exactly one cycle later");

    tick();
    assert (!port_out.response.valid)
      else $fatal(1, "read response valid lasted more than one cycle");

    request_write(2'd1, 32'haabbccdd, 4'b0101);
    request_read(2'd1);
    assert (port_out.response.valid && port_out.response.bits == 32'h11bb33dd)
      else $fatal(1, "byte write mask was not applied by lane");

    $display("fixed-latency masked SyncRam passed");
    $finish;
  end
endmodule
