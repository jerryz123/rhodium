// Executes a short mixed-width Zcb program through RV5Stage's normal pipeline.
module rv5stage_zcb_tb;
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
  localparam logic [3:0] MEMORY_STORE = 4'd2;
  localparam logic [1:0] MEMORY_WIDTH_BYTE = 2'd0;
  localparam logic [1:0] DATA_DESTINATION_NONE = 2'd0;

  RV5StageCore dut (.prefetch_out(), .*);
  always #5 clock = ~clock;

  function automatic logic [31:0] instruction_at(input logic [63:0] address);
    case (address)
      64'h00000001_00000000: instruction_at = 32'h4495440d; // c.li x8, 3; c.li x9, 5
      64'h00000001_00000004: instruction_at = 32'h45019c45; // c.mul x8, x9; c.li x10, 0
      64'h00000001_00000008: instruction_at = 32'h00018900; // c.sb x8, 0(x10); c.nop
      default: instruction_at = 32'h00010001; // c.nop; c.nop
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
    end else if (instruction_access_out.flush) begin
      instruction_response_valid <= 1'b0;
    end else begin
      if (instruction_response_valid && instruction_access_out.response.ready)
        instruction_response_valid <= 1'b0;
      if (instruction_access_out.request.valid && instruction_access_in.request.ready) begin
        instruction_response_valid <= 1'b1;
        instruction_response_bits <= instruction_at(instruction_access_out.request.bits.address);
      end
    end

    if (!reset && data_access_out.request.valid && data_access_in.request.ready) begin
      assert (data_access_out.request.bits.access == MEMORY_STORE &&
              data_access_out.request.bits.width == MEMORY_WIDTH_BYTE &&
              data_access_out.request.bits.address == 64'd0 &&
              data_access_out.request.bits.data == 64'd15 &&
              data_access_out.request.bits.destination == DATA_DESTINATION_NONE)
        else $fatal(1, "Zcb multiply result did not reach the compressed byte store");

      $display("RV5Stage Zcb mixed-width execution passed");
      $finish;
    end
  end

  initial begin
    interrupts = '0;

    repeat (2) @(posedge clock);
    #1;
    reset = 1'b0;
    @(posedge clock);
    #1;
    repeat (300) @(posedge clock);
    $fatal(1, "Zcb program did not reach its byte store");
  end
endmodule
