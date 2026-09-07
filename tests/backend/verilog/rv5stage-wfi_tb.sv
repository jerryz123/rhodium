// Verifies RV5Stage WFI sleep, masked wake, lost-wakeup avoidance, and interrupt entry.
module rv5stage_wfi_tb;
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
  typedef struct packed { ready_t request; logic request_fault; logic request_access_fault; data_resp_t response; logic drained; } data_in_t;
  typedef struct packed { data_req_t request; } data_out_t;

  localparam logic [3:0] MEMORY_STORE = 4'd2;
  localparam logic [63:0] MACHINE_TIMER_CAUSE = 64'h8000000000000007;
  localparam logic [2:0] SETUP_MASKED_WAIT = 3'd0;
  localparam logic [2:0] MASKED_WAIT = 3'd1;
  localparam logic [2:0] MASKED_RESUME = 3'd2;
  localparam logic [2:0] PENDING_BEFORE_WFI = 3'd3;
  localparam logic [2:0] SETUP_TRAPPING_WAIT = 3'd4;
  localparam logic [2:0] TRAPPING_WAIT = 3'd5;
  localparam logic [2:0] INTERRUPT_HANDLER = 3'd6;

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
  logic [2:0] phase;
  logic [5:0] quiet_cycles;
  logic handler_mepc_seen;
  logic [9:0] cycles;

  RV5StageCore dut (.prefetch_out(), .*);
  always #5 clock = ~clock;

  function automatic logic [31:0] instruction_at(input logic [63:0] address);
    case (address)
      64'h00: instruction_at = 32'h10000093; // addi x1, x0, 0x100
      64'h04: instruction_at = 32'h30509073; // csrw mtvec, x1
      64'h08: instruction_at = 32'h08000093; // addi x1, x0, 0x80
      64'h0c: instruction_at = 32'h30409073; // csrw mie, x1
      64'h10: instruction_at = 32'h05500113; // addi x2, x0, 0x55
      64'h14: instruction_at = 32'h02203423; // sd x2, 40(x0)
      64'h18: instruction_at = 32'h10500073; // wfi
      64'h1c: instruction_at = 32'h00100113; // addi x2, x0, 1
      64'h20: instruction_at = 32'h00203023; // sd x2, 0(x0)
      64'h24: instruction_at = 32'h10500073; // wfi with MTIP already set
      64'h28: instruction_at = 32'h00200113; // addi x2, x0, 2
      64'h2c: instruction_at = 32'h00203423; // sd x2, 8(x0)
      64'h30: instruction_at = 32'h00800093; // addi x1, x0, 8
      64'h34: instruction_at = 32'h3000a073; // csrs mstatus, x1
      64'h38: instruction_at = 32'h06600113; // addi x2, x0, 0x66
      64'h3c: instruction_at = 32'h02203823; // sd x2, 48(x0)
      64'h40: instruction_at = 32'h10500073; // wfi
      64'h44: instruction_at = 32'h00300113; // addi x2, x0, 3
      64'h48: instruction_at = 32'h02203023; // sd x2, 32(x0), must be squashed
      64'h100: instruction_at = 32'h341021f3; // csrr x3, mepc
      64'h104: instruction_at = 32'h00303823; // sd x3, 16(x0)
      64'h108: instruction_at = 32'h34202273; // csrr x4, mcause
      64'h10c: instruction_at = 32'h00403c23; // sd x4, 24(x0)
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
  end

  always_ff @(posedge clock) begin
    if (reset) begin
      instruction_response_valid <= 1'b0;
      instruction_response_bits <= '0;
      interrupts <= '0;
      phase <= SETUP_MASKED_WAIT;
      quiet_cycles <= '0;
      handler_mepc_seen <= 1'b0;
      cycles <= '0;
    end else begin
      cycles <= cycles + 1'b1;
      if (instruction_access_out.flush)
        instruction_response_valid <= 1'b0;
      else begin
        if (instruction_response_valid && instruction_access_out.response.ready)
          instruction_response_valid <= 1'b0;
        if (instruction_access_out.request.valid && instruction_access_in.request.ready) begin
          instruction_response_valid <= 1'b1;
          instruction_response_bits <= instruction_at(instruction_access_out.request.bits.address);
        end
      end

      if (phase == MASKED_WAIT || phase == TRAPPING_WAIT) begin
        if (instruction_access_out.request.valid && instruction_access_in.request.ready)
          quiet_cycles <= '0;
        else
          quiet_cycles <= quiet_cycles + 1'b1;
      end else begin
        quiet_cycles <= '0;
      end

      if (phase == MASKED_WAIT && quiet_cycles == 6'd12) begin
        interrupts.machine_timer <= 1'b1;
        phase <= MASKED_RESUME;
      end else if (phase == TRAPPING_WAIT && quiet_cycles == 6'd12) begin
        interrupts.machine_timer <= 1'b1;
        phase <= INTERRUPT_HANDLER;
      end

      if (data_access_out.request.valid && data_access_in.request.ready) begin
        assert (data_access_out.request.bits.access == MEMORY_STORE)
          else $fatal(1, "WFI fixture issued an unexpected memory operation");
        case (data_access_out.request.bits.address)
          64'd40: begin
            assert (phase == SETUP_MASKED_WAIT && data_access_out.request.bits.data == 64'h55)
              else $fatal(1, "masked-WFI setup marker was incorrect");
            phase <= MASKED_WAIT;
          end
          64'd0: begin
            assert (phase == MASKED_RESUME && data_access_out.request.bits.data == 64'd1)
              else $fatal(1, "globally masked interrupt did not resume after WFI");
            phase <= PENDING_BEFORE_WFI;
          end
          64'd8: begin
            assert (phase == PENDING_BEFORE_WFI && data_access_out.request.bits.data == 64'd2)
              else $fatal(1, "interrupt pending before WFI caused a lost wakeup");
            interrupts.machine_timer <= 1'b0;
            phase <= SETUP_TRAPPING_WAIT;
          end
          64'd48: begin
            assert (phase == SETUP_TRAPPING_WAIT && data_access_out.request.bits.data == 64'h66)
              else $fatal(1, "trapping-WFI setup marker was incorrect");
            phase <= TRAPPING_WAIT;
          end
          64'd16: begin
            assert (phase == INTERRUPT_HANDLER && data_access_out.request.bits.data == 64'h44)
              else $fatal(1, "WFI interrupt recorded the wrong successor PC");
            handler_mepc_seen <= 1'b1;
          end
          64'd24: begin
            assert (phase == INTERRUPT_HANDLER && handler_mepc_seen &&
                    data_access_out.request.bits.data == MACHINE_TIMER_CAUSE)
              else $fatal(1, "WFI interrupt recorded the wrong cause");
            $display("RV5Stage WFI sleep and wake behavior passed");
            $finish;
          end
          64'd32: $fatal(1, "instruction after trapping WFI executed before the handler");
          default: $fatal(1, "WFI fixture stored to an unexpected address");
        endcase
      end

      assert (!(phase == MASKED_WAIT && data_access_out.request.valid))
        else $fatal(1, "globally masked WFI did not stop younger execution");
      assert (!(phase == TRAPPING_WAIT && data_access_out.request.valid))
        else $fatal(1, "interrupt-taking WFI did not stop younger execution");

      assert (cycles != 10'h3ff) else $fatal(1, "WFI fixture timed out");
    end
  end

  initial begin

    repeat (2) @(posedge clock);
    #1;
    reset = 1'b0;
    @(posedge clock);
    #1;
  end
endmodule
