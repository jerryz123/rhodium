// Exhaustively checks legal RV64 load/store lanes and size-based alignment.
// SPDX-License-Identifier: Apache-2.0
module load_store_tb;
  localparam logic [1:0] BYTE   = 2'd0;
  localparam logic [1:0] HALF   = 2'd1;
  localparam logic [1:0] WORD   = 2'd2;
  localparam logic [1:0] DOUBLE = 2'd3;

  logic [63:0] address;
  logic [63:0] load_data;
  logic [63:0] store_value;
  logic [1:0]  width;
  logic        unsigned_load;
  logic        aligned;
  logic [63:0] load_value;
  logic [63:0] store_data;
  logic [7:0]  store_mask;

  LoadStoreFixture dut (
    .address       (address),
    .load_data     (load_data),
    .store_value   (store_value),
    .width         (width),
    .unsigned_load (unsigned_load),
    .aligned       (aligned),
    .load_value    (load_value),
    .store_data    (store_data),
    .store_mask    (store_mask)
  );

  function automatic logic [7:0] base_mask(input logic [1:0] size);
    case (size)
      BYTE:    base_mask = 8'h01;
      HALF:    base_mask = 8'h03;
      WORD:    base_mask = 8'h0f;
      default: base_mask = 8'hff;
    endcase
  endfunction

  function automatic logic [63:0] expected_load(
    input logic [1:0] size,
    input logic       is_unsigned,
    input logic [2:0] offset
  );
    logic [63:0] shifted;
    shifted = load_data >> (offset * 8);
    case (size)
      BYTE:
        expected_load = is_unsigned
          ? {56'b0, shifted[7:0]}
          : {{56{shifted[7]}}, shifted[7:0]};
      HALF:
        expected_load = is_unsigned
          ? {48'b0, shifted[15:0]}
          : {{48{shifted[15]}}, shifted[15:0]};
      WORD:
        expected_load = is_unsigned
          ? {32'b0, shifted[31:0]}
          : {{32{shifted[31]}}, shifted[31:0]};
      default: expected_load = load_data;
    endcase
  endfunction

  task automatic check_legal_lane(
    input logic [1:0] size,
    input logic [2:0] offset
  );
    address = 64'h1000 + {61'b0, offset};
    width = size;
    unsigned_load = 1'b0;
    #1;
    assert (aligned)
      else $fatal(1, "legal size %0d offset %0d reported unaligned", size, offset);
    assert (load_value === expected_load(size, 1'b0, offset))
      else $fatal(1, "signed load failed for size %0d offset %0d", size, offset);
    assert (store_data === (store_value << (offset * 8)))
      else $fatal(1, "store data failed for size %0d offset %0d", size, offset);
    assert (store_mask === (base_mask(size) << offset))
      else $fatal(1, "store mask failed for size %0d offset %0d", size, offset);

    unsigned_load = 1'b1;
    #1;
    assert (load_value === expected_load(size, 1'b1, offset))
      else $fatal(1, "unsigned load failed for size %0d offset %0d", size, offset);
  endtask

  task automatic check_unaligned(
    input logic [1:0] size,
    input logic [2:0] offset
  );
    address = 64'h1000 + {61'b0, offset};
    width = size;
    #1;
    assert (!aligned)
      else $fatal(1, "illegal size %0d offset %0d reported aligned", size, offset);
  endtask

  initial begin
    load_data = 64'h80ff_7f01_8000_ff80;
    store_value = 64'h8877_6655_4433_2211;

    for (int offset = 0; offset < 8; offset++)
      check_legal_lane(BYTE, offset[2:0]);
    for (int offset = 0; offset < 8; offset += 2)
      check_legal_lane(HALF, offset[2:0]);
    for (int offset = 0; offset < 8; offset += 4)
      check_legal_lane(WORD, offset[2:0]);
    check_legal_lane(DOUBLE, 3'd0);

    check_unaligned(HALF, 3'd1);
    check_unaligned(WORD, 3'd2);
    check_unaligned(DOUBLE, 3'd4);

    $display("load/store lane generation simulation passed");
    $finish;
  end
endmodule
