// Verifies commit-issued division, word projections, hazards, and independent progress.
module rv5stage_divide_tb;
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
    logic [2:0] locality;
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
  logic [11:0] cycles;
  logic [3:0] stores_seen;
  localparam logic [3:0] MEMORY_STORE = 4'd2;

  RV5StageCore dut (.pipeline_access_in('0), .pipeline_access_out(), .prefetch_out(), .*);
  always #5 clock = ~clock;

  function automatic logic [31:0] instruction_at(input logic [63:0] address);
    case (address)
      64'h00000001_00000000: instruction_at = 32'h06400293; // addi x5, x0, 100
      64'h00000001_00000004: instruction_at = 32'h00700313; // addi x6, x0, 7
      64'h00000001_00000008: instruction_at = 32'h0262c3b3; // div x7, x5, x6
      64'h00000001_0000000c: instruction_at = 32'h00900913; // addi x18, x0, 9
      64'h00000001_00000010: instruction_at = 32'h05203023; // sd x18, 64(x0)
      64'h00000001_00000014: instruction_at = 32'h0262e433; // rem x8, x5, x6
      64'h00000001_00000018: instruction_at = 32'hf9c00493; // addi x9, x0, -100
      64'h00000001_0000001c: instruction_at = 32'h0264c533; // div x10, x9, x6
      64'h00000001_00000020: instruction_at = 32'h0264e5b3; // rem x11, x9, x6
      64'h00000001_00000024: instruction_at = 32'hfff00613; // addi x12, x0, -1
      64'h00000001_00000028: instruction_at = 32'h00200693; // addi x13, x0, 2
      64'h00000001_0000002c: instruction_at = 32'h02d6573b; // divuw x14, x12, x13
      64'h00000001_00000030: instruction_at = 32'h02d677bb; // remuw x15, x12, x13
      64'h00000001_00000034: instruction_at = 32'h0264c83b; // divw x16, x9, x6
      64'h00000001_00000038: instruction_at = 32'h0264e8bb; // remw x17, x9, x6
      64'h00000001_0000003c: instruction_at = 32'h00703023; // sd x7, 0(x0)
      64'h00000001_00000040: instruction_at = 32'h00803423; // sd x8, 8(x0)
      64'h00000001_00000044: instruction_at = 32'h00a03823; // sd x10, 16(x0)
      64'h00000001_00000048: instruction_at = 32'h00b03c23; // sd x11, 24(x0)
      64'h00000001_0000004c: instruction_at = 32'h02e03023; // sd x14, 32(x0)
      64'h00000001_00000050: instruction_at = 32'h02f03423; // sd x15, 40(x0)
      64'h00000001_00000054: instruction_at = 32'h03003823; // sd x16, 48(x0)
      64'h00000001_00000058: instruction_at = 32'h03103c23; // sd x17, 56(x0)
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
          else $fatal(1, "divide program unexpectedly issued a load");
        case (stores_seen)
          0: begin
            assert (cycles < 50 && data_access_out.request.bits.address == 64'd64 &&
                    data_access_out.request.bits.data == 64'd9)
              else $fatal(1, "independent instruction did not pass the active divider");
          end
          1: begin
            assert (data_access_out.request.bits.address == 64'd0 &&
                    data_access_out.request.bits.data == 64'd14)
              else $fatal(1, "DIV result was incorrect");
          end
          2: begin
            assert (data_access_out.request.bits.address == 64'd8 &&
                    data_access_out.request.bits.data == 64'd2)
              else $fatal(1, "REM result was incorrect");
          end
          3: begin
            assert (data_access_out.request.bits.address == 64'd16 &&
                    data_access_out.request.bits.data == -64'sd14)
              else $fatal(1, "signed DIV result was incorrect: %h", data_access_out.request.bits.data);
          end
          4: begin
            assert (data_access_out.request.bits.address == 64'd24 &&
                    data_access_out.request.bits.data == -64'sd2)
              else $fatal(1, "signed REM result was incorrect");
          end
          5: begin
            assert (data_access_out.request.bits.address == 64'd32 &&
                    data_access_out.request.bits.data == 64'h000000007fffffff)
              else $fatal(1, "DIVUW result was not sign extended");
          end
          6: begin
            assert (data_access_out.request.bits.address == 64'd40 &&
                    data_access_out.request.bits.data == 64'd1)
              else $fatal(1, "REMUW result was incorrect");
          end
          7: begin
            assert (data_access_out.request.bits.address == 64'd48 &&
                    data_access_out.request.bits.data == -64'sd14)
              else $fatal(1, "DIVW result was incorrect");
          end
          8: begin
            assert (data_access_out.request.bits.address == 64'd56 &&
                    data_access_out.request.bits.data == -64'sd2)
              else $fatal(1, "REMW result was incorrect");
            $display("RV5Stage commit-issued division passed");
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
    repeat (1600) @(posedge clock);
    $fatal(1, "core did not complete the divide scenario");
  end
endmodule
