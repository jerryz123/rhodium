// Checks vset execution, scalar dependencies, CSR visibility, squash, and unimplemented vector traps.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_vector_config_tb;
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
  logic clock = 0, reset = 1;
  interrupts_t interrupts;
  instruction_in_t instruction_access_in;
  instruction_out_t instruction_access_out;
  data_in_t data_access_in;
  data_out_t data_access_out;
  logic fault, translation_flush;
  logic [1:0] privilege;
  logic [63:0] mstatus, satp, hart_id = 0, time_counter = 0;
  logic response_valid = 0;
  logic [31:0] response_word;

  integer cycles = 0, stores = 0;
  RV5StageCoreFixture dut (.pipeline_access_in('0), .pipeline_access_out(), .prefetch_out(), .*);
  always #5 clock = ~clock;
  // Configure VS, exercise all three vset forms, consume scalar results, and
  // squash a younger configuration before trapping on decode-only vector ADD.
  function automatic logic [31:0] instruction_at(input logic [63:0] address);
    case (address)
      0: return 32'h10000093;
      4: return 32'h30509073;
      8: return 32'h20000093;
      12: return 32'h3000a073;
      16: return 32'h00300093;
      20: return 32'h0100f157;
      24: return 32'h00110193;
      28: return 32'h00303023;
      32: return 32'h01900213;
      36: return 32'h80407057;
      40: return 32'hc20021f3;
      44: return 32'h00303423;
      48: return 32'hc002f157;
      52: return 32'h00203823;
      56: return 32'h0080006f;
      60: return 32'hc00ff157;
      64: return 32'hc20021f3;
      68: return 32'h00303c23;
      72: return 32'h008021f3;
      76: return 32'h02303023;
      80: return 32'h02218257;
      84: return 32'h00303023;
      256: return 32'h342021f3; // mcause
      260: return 32'h02303423;
      264: return 32'h341021f3; // mepc
      268: return 32'h02303823;
      default: return 32'h0000006f;
    endcase
  endfunction
  always_comb begin
    instruction_access_in = '0;
    instruction_access_in.request.ready = !response_valid;
    instruction_access_in.response.valid = response_valid;
    instruction_access_in.response.bits.word = response_word;
    data_access_in = '0;
    data_access_in.request.ready = cycles % 5 != 0;
    data_access_in.drained = 1;
  end
  always @(posedge clock) begin
    if (reset) begin
      response_valid <= 0;
      cycles <= 0;
      stores <= 0;
    end else begin
      cycles <= cycles + 1;
      if (instruction_access_out.flush) response_valid <= 0;
      else begin
        if (response_valid && instruction_access_out.response.ready) response_valid <= 0;
        if (instruction_access_out.request.valid && instruction_access_in.request.ready) begin
          response_valid <= 1;
          response_word <= instruction_at(instruction_access_out.request.bits.address);
        end
      end
      if (data_access_out.request.valid && data_access_in.request.ready) begin
        assert (data_access_out.request.bits.access == 2 && data_access_out.request.bits.address == 64'(stores) * 8)
          else $fatal(1, "unexpected vector-program memory request, store=%0d address=%0d", stores, data_access_out.request.bits.address);
        case (stores)
          0: assert (data_access_out.request.bits.data == 4) else $fatal(1, "vset result forwarding");
          1: assert (data_access_out.request.bits.data == 3) else $fatal(1, "vsetvl preserve");
          2,3: assert (data_access_out.request.bits.data == 5) else $fatal(1, "vsetivli or squash");
          4: assert (data_access_out.request.bits.data == 0) else $fatal(1, "vstart");
          5: assert (data_access_out.request.bits.data == 2) else $fatal(1, "arithmetic must trap");
          6: begin
            assert (data_access_out.request.bits.data == 80) else $fatal(1, "precise vector trap");
            $display("rv5stage vector configuration pipeline passed");
            $finish;
          end
        endcase
        stores <= stores + 1;
      end
      if (cycles > 5000) $fatal(1, "vector config pipeline timeout, stores=%0d", stores);
    end
  end
  initial begin
    interrupts = '0;
    repeat (4) @(posedge clock);
    @(negedge clock); reset = 0;
  end
endmodule
