// Verifies CBO.ZERO replay, single retirement, fence ordering, and precise faults.
module rv5stage_zicboz_tb;
  typedef struct packed {
    logic supervisor_software;
    logic machine_software;
    logic supervisor_timer;
    logic machine_timer;
    logic supervisor_external;
    logic machine_external;
  } interrupts_t;
  typedef struct packed { logic valid; logic [63:0] bits; } start_in_t;
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
    logic [2:0] access;
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

  localparam logic [2:0] MEMORY_LOAD = 3'd1;
  localparam logic [2:0] MEMORY_STORE = 3'd2;

  logic clock = 1'b0;
  logic reset = 1'b1;
  logic [63:0] time_counter = '0;
  logic [63:0] hart_id = '0;
  interrupts_t interrupts;
  start_in_t start_in;
  instruction_in_t instruction_access_in;
  data_in_t data_access_in;
  ready_t start_out;
  instruction_out_t instruction_access_out;
  data_out_t data_access_out;
  logic fault;
  logic [1:0] privilege;
  logic [63:0] mstatus;
  logic [63:0] satp;
  logic translation_flush;
  logic [66:0] prefetch_out;
  logic instruction_response_valid;
  logic [31:0] instruction_response_bits;

  integer scenario = 0;
  integer attempts, accepted, pending_cycles, stores;
  logic done;
  RV5StageCore dut (.*);
  always #5 clock = ~clock;

  function automatic logic [31:0] instruction_at(input logic [63:0] address);
    case (address)
      64'h0: instruction_at = 32'h342021f3;   // csrr x3, mcause
      64'h4: instruction_at = 32'h00303423;   // sd x3, 8(x0)
      64'h8: instruction_at = 32'h34302273;   // csrr x4, mtval
      64'hc: instruction_at = 32'h00403823;   // sd x4, 16(x0)
      64'h10: instruction_at = 32'h0000006f;
      64'h100: instruction_at = 32'h03f00093; // addi x1, x0, 63
      64'h104: instruction_at = scenario == 3 ? 32'h12000113 : 32'h01c0006f;
      64'h108: instruction_at = 32'h34111073; // csrw mepc, x2
      64'h10c: instruction_at = 32'h00001137; // lui x2, 1
      64'h110: instruction_at = 32'h00115113; // srli x2, x2, 1 (MPP=S)
      64'h114: instruction_at = 32'h30011073; // csrw mstatus, x2
      64'h118: instruction_at = 32'h30200073; // mret
      64'h120: instruction_at = 32'h0040a00f; // cbo.zero (x1)
      64'h124: instruction_at = 32'h0ff0000f; // fence iorw, iorw
      64'h128: instruction_at = 32'h02003023; // sd x0, 32(x0)
      default: instruction_at = 32'h0000006f;
    endcase
  endfunction

  always_comb begin
    instruction_access_in = '0;
    instruction_access_in.request.ready = !instruction_response_valid;
    instruction_access_in.response.valid = instruction_response_valid;
    instruction_access_in.response.bits.word = instruction_response_bits;
    data_access_in = '0;
    data_access_in.request.ready = data_access_out.request.bits.access != 3'd6 || (scenario == 0 && attempts >= 3);
    data_access_in.request_fault = data_access_out.request.valid && data_access_out.request.bits.access == 3'd6 && scenario == 2;
    data_access_in.request_access_fault = data_access_out.request.valid && data_access_out.request.bits.access == 3'd6 && scenario == 1;
    data_access_in.response.valid = pending_cycles == 1;
    data_access_in.drained = pending_cycles == 0;
  end

  always_ff @(posedge clock) begin
    if (reset) begin
      instruction_response_valid <= 0;
      instruction_response_bits <= 0;
      attempts <= 0;
      accepted <= 0;
      pending_cycles <= 0;
      stores <= 0;
      done <= 0;
    end else begin
      if (instruction_access_out.flush)
        instruction_response_valid <= 0;
      else begin
        if (instruction_response_valid && instruction_access_out.response.ready)
          instruction_response_valid <= 0;
        if (instruction_access_out.request.valid && instruction_access_in.request.ready) begin
          instruction_response_valid <= 1;
          instruction_response_bits <= instruction_at(instruction_access_out.request.bits.address);
        end
      end
      if (pending_cycles != 0) pending_cycles <= pending_cycles - 1;
      if (data_access_out.request.valid && data_access_out.request.bits.access == 3'd6) begin
        assert (scenario != 3 && data_access_out.request.bits.address == 63 &&
                data_access_out.request.bits.destination == 0)
          else $fatal(1, "CBO.ZERO lost its address, had a destination, or bypassed CBZE");
        attempts <= attempts + 1;
        if (data_access_in.request.ready) begin
          assert (accepted == 0) else $fatal(1, "CBO.ZERO accepted more than once");
          accepted <= accepted + 1;
          pending_cycles <= 16;
        end
      end
      if (data_access_out.request.valid && data_access_out.request.bits.access == MEMORY_STORE) begin
        if (scenario == 0) begin
          assert (data_access_out.request.bits.address == 32 && accepted == 1 && attempts == 4 && pending_cycles == 0)
            else $fatal(1, "zero replay or fence ordering failed");
          done <= 1;
        end else if (stores == 0) begin
          assert (data_access_out.request.bits.address == 8 &&
                  data_access_out.request.bits.data == (scenario == 1 ? 64'd7 : scenario == 2 ? 64'd15 : 64'd2) &&
                  accepted == 0)
            else $fatal(1, "wrong zero trap cause or side effect, scenario %0d", scenario);
          stores <= 1;
        end else begin
          assert (data_access_out.request.bits.address == 16 &&
                  data_access_out.request.bits.data == (scenario == 3 ? 64'h0040a00f : 64'd63))
            else $fatal(1, "zero trap did not preserve original VA/encoding");
          done <= 1;
        end
      end
      assert (!fault) else $fatal(1, "nonarchitectural fault");
    end
  end

  initial begin
    interrupts = '0;
    start_in = '0;
    for (scenario = 0; scenario < 4; scenario++) begin
      reset = 1;
      repeat (2) @(posedge clock);
      @(negedge clock);
      reset = 0;
      start_in.bits = 64'h100;
      start_in.valid = 1;
      @(posedge clock);
      @(negedge clock);
      start_in.valid = 0;
      for (int cycles = 0; cycles < 600 && !done; cycles++) begin
        @(posedge clock);
        #1;
      end
      assert (done) else $fatal(1, "zero scenario %0d timed out", scenario);
    end
    $display("RV5Stage CBO.ZERO replay, ordering and precise faults passed");
    $finish;
  end
endmodule
