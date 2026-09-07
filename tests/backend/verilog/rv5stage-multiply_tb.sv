// Verifies commit-issued multiplication, deferred hazards, and independent progress.
module rv5stage_multiply_tb;
  typedef struct packed {
    logic supervisor_software;
    logic machine_software;
    logic supervisor_timer;
    logic machine_timer;
    logic supervisor_external;
    logic machine_external;
  } interrupts_t;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic [63:0] address; } instruction_req_bits_t;
  typedef struct packed { logic valid; instruction_req_bits_t bits; } instruction_req_t;
  typedef struct packed { logic [31:0] word; logic page_fault; logic access_fault; } instruction_resp_bits_t;
  typedef struct packed { logic valid; instruction_resp_bits_t bits; } instruction_resp_t;
  typedef struct packed { ready_t request; instruction_resp_t response; } instruction_in_t;
  typedef struct packed {
    logic flush;
    logic invalidate_all;
    instruction_req_t request;
    ready_t response;
  } instruction_out_t;
  typedef struct packed {
    logic [63:0] address;
    logic [3:0] access;
    logic [3:0] atomic;
    logic [1:0] width;
    logic unsigned_0;
    logic [63:0] data;
    logic [1:0] destination;
    logic [4:0] rd;
    logic [1:0] floating_point_precision;
  } data_req_bits_t;
  typedef struct packed { logic valid; data_req_bits_t bits; } data_req_t;
  typedef struct packed {
    logic access_fault;
    logic [63:0] data;
    logic [1:0] destination;
    logic [4:0] rd;
    logic [1:0] floating_point_precision;
  } data_resp_bits_t;
  typedef struct packed { logic valid; data_resp_bits_t bits; } data_resp_t;
  typedef struct packed { ready_t request; logic request_fault; logic request_access_fault; data_resp_t response; logic drained; logic reservation_valid; } data_in_t;
  typedef struct packed { data_req_t request; } data_out_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  logic [63:0] time_counter = '0;
  logic [63:0] hart_id = '0;
  interrupts_t interrupts;
  instruction_in_t instruction_access_in;
  data_in_t data_access_in;
  instruction_out_t instruction_access_out;
  data_out_t data_access_out;

  logic [1:0] privilege;
  logic [63:0] mstatus;
  logic [63:0] satp;
  logic translation_flush;
  logic instruction_response_valid;
  logic [31:0] instruction_response_bits;
  logic [8:0] cycles;
  logic [2:0] stores_seen;
  localparam logic [3:0] MEMORY_STORE = 4'd2;

  RV5StageCore dut (.prefetch_out(), .*);
  always #5 clock = ~clock;

  function automatic logic [31:0] instruction_at(input logic [63:0] address);
    case (address)
      64'h00000001_00000000: instruction_at = 32'h00600293; // addi x5, x0, 6
      64'h00000001_00000004: instruction_at = 32'hff900313; // addi x6, x0, -7
      64'h00000001_00000008: instruction_at = 32'h026283b3; // mul x7, x5, x6
      64'h00000001_0000000c: instruction_at = 32'h00900413; // addi x8, x0, 9
      64'h00000001_00000010: instruction_at = 32'h02803023; // sd x8, 32(x0)
      64'h00000001_00000014: instruction_at = 32'h008384b3; // add x9, x7, x8
      64'h00000001_00000018: instruction_at = 32'h0282b533; // mulhu x10, x5, x8
      64'h00000001_0000001c: instruction_at = 32'h028305bb; // mulw x11, x6, x8
      64'h00000001_00000020: instruction_at = 32'h02831633; // mulh x12, x6, x8
      64'h00000001_00000024: instruction_at = 32'h028326b3; // mulhsu x13, x6, x8
      64'h00000001_00000028: instruction_at = 32'h00703023; // sd x7, 0(x0)
      64'h00000001_0000002c: instruction_at = 32'h00903423; // sd x9, 8(x0)
      64'h00000001_00000030: instruction_at = 32'h00a03823; // sd x10, 16(x0)
      64'h00000001_00000034: instruction_at = 32'h00b03c23; // sd x11, 24(x0)
      64'h00000001_00000038: instruction_at = 32'h02c03423; // sd x12, 40(x0)
      64'h00000001_0000003c: instruction_at = 32'h02d03823; // sd x13, 48(x0)
      default: instruction_at = 32'h00000013;
    endcase
  endfunction

  always_comb begin
    instruction_access_in.request.ready = !instruction_response_valid;
    instruction_access_in.response.valid = instruction_response_valid;
    instruction_access_in.response.bits.word = instruction_response_bits;
    instruction_access_in.response.bits.page_fault = 1'b0;
    instruction_access_in.response.bits.access_fault = 1'b0;
    data_access_in.request.ready = 1'b1;
    data_access_in.request_fault = 1'b0;
    data_access_in.request_access_fault = 1'b0;
    data_access_in.response = '0;
    data_access_in.drained = 1'b1;
    data_access_in.reservation_valid = 1'b0;
  end

  always_ff @(posedge clock) begin
    if (reset) begin
      instruction_response_valid <= 1'b0;
      instruction_response_bits <= '0;
      cycles <= '0;
      stores_seen <= '0;
    end else begin
      cycles <= cycles + 1'b1;
      if (instruction_response_valid && instruction_access_out.response.ready)
        instruction_response_valid <= 1'b0;
      if (instruction_access_out.request.valid && instruction_access_in.request.ready) begin
        instruction_response_valid <= 1'b1;
        instruction_response_bits <= instruction_at(instruction_access_out.request.bits.address);
      end

      if (data_access_out.request.valid && data_access_in.request.ready) begin
        assert (data_access_out.request.bits.access == MEMORY_STORE)
          else $fatal(1, "multiply program unexpectedly issued a load");
        case (stores_seen)
          0: begin
            assert (cycles < 40 && data_access_out.request.bits.address == 64'd32 &&
                    data_access_out.request.bits.data == 64'd9)
              else $fatal(1, "independent instruction did not pass the active multiplier");
          end
          1: begin
            assert (data_access_out.request.bits.address == 64'd0 &&
                    data_access_out.request.bits.data == 64'hffffffffffffffd6)
              else $fatal(1, "MUL result was incorrect");
          end
          2: begin
            assert (data_access_out.request.bits.address == 64'd8 &&
                    data_access_out.request.bits.data == 64'hffffffffffffffdf)
              else $fatal(1, "MUL dependency was released with the wrong value");
          end
          3: begin
            assert (data_access_out.request.bits.address == 64'd16 &&
                    data_access_out.request.bits.data == 64'd0)
              else $fatal(1, "MULHU result was incorrect");
          end
          4: begin
            assert (data_access_out.request.bits.address == 64'd24 &&
                    data_access_out.request.bits.data == 64'hffffffffffffffc1)
              else $fatal(1, "MULW result was not sign extended");
          end
          5: begin
            assert (data_access_out.request.bits.address == 64'd40 &&
                    data_access_out.request.bits.data == 64'hffffffffffffffff)
              else $fatal(1, "MULH result was incorrect");
          end
          6: begin
            assert (data_access_out.request.bits.address == 64'd48 &&
                    data_access_out.request.bits.data == 64'hffffffffffffffff)
              else $fatal(1, "MULHSU result was incorrect");
            $display("RV5Stage commit-issued multiplication passed");
            $finish;
          end
          default: $fatal(1, "unexpected extra store");
        endcase
        stores_seen <= stores_seen + 1'b1;
      end

    end
  end

  initial begin
    interrupts = '0;

    repeat (2) @(posedge clock);
    #1;
    reset = 1'b0;
    @(posedge clock);
    #1;
    repeat (600) @(posedge clock);
    $fatal(1, "core did not complete the multiply scenario");
  end
endmodule
