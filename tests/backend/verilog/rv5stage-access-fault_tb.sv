// Verifies an instruction access fault traps precisely and squashes younger execution.
module rv5stage_access_fault_tb;
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
  typedef struct packed {
    logic [31:0] word;
    logic page_fault;
    logic access_fault;
  } instruction_resp_bits_t;
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
  typedef struct packed {
    ready_t request;
    logic request_fault;
    logic request_access_fault;
    data_resp_t response;
    logic drained;
  } data_in_t;
  typedef struct packed { data_req_t request; } data_out_t;

  localparam logic [3:0] MEMORY_STORE = 4'd2;

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
  instruction_resp_bits_t instruction_response_bits;
  logic [1:0] stores_seen;

  RV5StageCore dut (.prefetch_out(), .*);
  always #5 clock = ~clock;

  function automatic logic [31:0] instruction_at(input logic [63:0] address);
    case (address)
      64'h0: instruction_at = 32'h342020f3; // csrr x1, mcause
      64'h4: instruction_at = 32'h00103023; // sd x1, 0(x0)
      64'h8: instruction_at = 32'h34302173; // csrr x2, mtval
      64'hc: instruction_at = 32'h00203423; // sd x2, 8(x0)
      64'h104: instruction_at = 32'h02003023; // sd x0, 32(x0), must be squashed
      default: instruction_at = 32'h00000013;
    endcase
  endfunction

  always_comb begin
    instruction_access_in.request.ready = !instruction_response_valid;
    instruction_access_in.response.valid = instruction_response_valid;
    instruction_access_in.response.bits = instruction_response_bits;
    data_access_in.request.ready = 1'b1;
    data_access_in.request_fault = 1'b0;
    data_access_in.request_access_fault = 1'b0;
    data_access_in.response = '0;
    data_access_in.drained = 1'b1;
  end

  always_ff @(posedge clock) begin
    if (reset) begin
      instruction_response_valid <= 1'b0;
      instruction_response_bits <= '0;
      stores_seen <= '0;
    end else begin
      if (instruction_access_out.flush)
        instruction_response_valid <= 1'b0;
      else begin
        if (instruction_response_valid && instruction_access_out.response.ready)
          instruction_response_valid <= 1'b0;
        if (instruction_access_out.request.valid && instruction_access_in.request.ready) begin
          instruction_response_valid <= 1'b1;
          instruction_response_bits.word <= instruction_at(instruction_access_out.request.bits.address);
          instruction_response_bits.page_fault <= 1'b0;
          instruction_response_bits.access_fault <= instruction_access_out.request.bits.address == 64'h100;
        end
      end

      if (data_access_out.request.valid && data_access_in.request.ready) begin
        assert (data_access_out.request.bits.access == MEMORY_STORE)
          else $fatal(1, "fault handler issued a non-store data request");
        assert (data_access_out.request.bits.address != 64'd32)
          else $fatal(1, "instruction after a fault executed before MEM recovery");
        if (stores_seen == 0) begin
          assert (data_access_out.request.bits.address == 0 &&
                  data_access_out.request.bits.data == 64'd1)
            else $fatal(1, "instruction access fault mcause was incorrect");
          stores_seen <= 1;
        end else begin
          assert (stores_seen == 1 &&
                  data_access_out.request.bits.address == 8 &&
                  data_access_out.request.bits.data == 64'h100)
            else $fatal(1, "instruction access fault mtval was incorrect");
          $display("RV5Stage precise instruction access fault passed");
          $finish;
        end
      end

    end
  end

  initial begin
    interrupts = '0;

    repeat (2) @(posedge clock);
    #1 reset = 1'b0;
    @(posedge clock);
    #1;
    repeat (80) @(posedge clock);
    $fatal(1, "instruction access fault handler did not complete");
  end
endmodule
