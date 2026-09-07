// Verifies RV5StageCore memory replay, redirects, ordered commit, scoreboard hazards, and FENCE.I control.
module rv5stage_core_tb;
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
  typedef struct packed { ready_t request; logic request_fault; logic request_access_fault; data_resp_t response; logic drained; } data_in_t;
  typedef struct packed { data_req_t request; } data_out_t;

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
  logic instruction_response_valid;
  logic [31:0] instruction_response_bits;
  logic data_response_valid;
  logic [63:0] data_response_bits;
  logic [4:0] data_response_rd;
  logic [1:0] load_requests;
  logic first_response_sent;
  logic [1:0] first_response_delay;
  logic second_response_sent;
  logic [2:0] second_response_delay;
  logic [1:0] stores_seen;
  logic rejected_first_load;
  logic saw_replay_refetch;
  logic saw_fetch_flush;
  logic saw_redirect;
  logic saw_jal_redirect;
  logic saw_fence_i_invalidate;
  logic saw_fence_i_refetch;
  logic [2:0] fetch_flushes;
  localparam logic [2:0] MEMORY_LOAD = 3'd1;
  localparam logic [1:0] DATA_DESTINATION_NONE = 2'd0;
  localparam logic [1:0] DATA_DESTINATION_INTEGER = 2'd1;

  RV5StageCore dut (.prefetch_out(), .*);
  always #5 clock = ~clock;

  function automatic logic [31:0] instruction_at(input logic [63:0] address);
    case (address)
      64'h00000001_00000000: instruction_at = 32'h00003283;  // ld x5, 0(x0)
      64'h00000001_00000004: instruction_at = 32'h00028533;  // add x10, x5, x0
      64'h00000001_00000008: instruction_at = 32'h01003403;  // ld x8, 16(x0)
      64'h00000001_0000000c: instruction_at = 32'h00100313; // addi x6, x0, 1
      64'h00000001_00000010: instruction_at = 32'h02031863; // bne x6, x0, +48
      64'h00000001_00000014: instruction_at = 32'h02603023; // sd x6, 32(x0), must be squashed
      64'h00000001_00000040: instruction_at = 32'h006503b3; // add x7, x10, x6
      64'h00000001_00000044: instruction_at = 32'h00900413; // addi x8, x0, 9
      64'h00000001_00000048: instruction_at = 32'h0000100f; // fence.i
      64'h00000001_0000004c: instruction_at = 32'h008004ef; // jal x9, +8
      64'h00000001_00000050: instruction_at = 32'h02603023; // sd x6, 32(x0), must be squashed
      64'h00000001_00000054: instruction_at = 32'h00703423; // sd x7, 8(x0)
      64'h00000001_00000058: instruction_at = 32'h00803823; // sd x8, 16(x0)
      64'h00000001_0000005c: instruction_at = 32'h00903c23; // sd x9, 24(x0)
      default: instruction_at = 32'h00000013;
    endcase
  endfunction

  always_comb begin
    instruction_access_in.request.ready = !instruction_response_valid;
    instruction_access_in.response.valid = instruction_response_valid;
    instruction_access_in.response.bits.word = instruction_response_bits;
    instruction_access_in.response.bits.page_fault = 1'b0;
    instruction_access_in.response.bits.access_fault = 1'b0;
    data_access_in.request.ready = rejected_first_load ||
                                   !data_access_out.request.valid ||
                                   data_access_out.request.bits.address != 64'd0;
    data_access_in.request_fault = 1'b0;
    data_access_in.request_access_fault = 1'b0;
    data_access_in.response.valid = data_response_valid;
    data_access_in.response.bits.data = data_response_bits;
    data_access_in.response.bits.destination = DATA_DESTINATION_INTEGER;
    data_access_in.response.bits.rd = data_response_rd;
    data_access_in.response.bits.floating_point_precision = 2'b01;
    data_access_in.drained = 1'b1;
  end

  always_ff @(posedge clock) begin
    if (reset) begin
      instruction_response_valid <= 1'b0;
      instruction_response_bits <= '0;
      data_response_valid <= 1'b0;
      data_response_bits <= '0;
      data_response_rd <= '0;
      load_requests <= '0;
      first_response_sent <= 1'b0;
      first_response_delay <= '0;
      second_response_sent <= 1'b0;
      second_response_delay <= '0;
      stores_seen <= '0;
      rejected_first_load <= 1'b0;
      saw_replay_refetch <= 1'b0;
      saw_fetch_flush <= 1'b0;
      saw_redirect <= 1'b0;
      saw_jal_redirect <= 1'b0;
      saw_fence_i_invalidate <= 1'b0;
      saw_fence_i_refetch <= 1'b0;
      fetch_flushes <= '0;
    end else begin
      if (instruction_access_out.flush) begin
        instruction_response_valid <= 1'b0;
        saw_fetch_flush <= 1'b1;
        fetch_flushes <= fetch_flushes + 1'b1;
        if (instruction_access_out.invalidate_all)
          saw_fence_i_invalidate <= 1'b1;
      end else begin
        if (instruction_response_valid && instruction_access_out.response.ready)
          instruction_response_valid <= 1'b0;
        if (instruction_access_out.request.valid && instruction_access_in.request.ready) begin
          instruction_response_valid <= 1'b1;
          instruction_response_bits <= instruction_at(instruction_access_out.request.bits.address);
          if (instruction_access_out.request.bits.address == 64'h00000001_00000000 &&
              rejected_first_load) begin
            assert (fetch_flushes >= 1)
              else $fatal(1, "replayed load was refetched without flushing younger work");
            saw_replay_refetch <= 1'b1;
          end
          if (instruction_access_out.request.bits.address == 64'h00000001_00000040) begin
            assert (saw_fetch_flush)
              else $fatal(1, "branch target fetched without flushing wrong-path requests");
            assert (first_response_sent && !second_response_sent)
              else $fatal(1, "branch redirect did not pass only the deferred load");
            assert (fetch_flushes >= 1)
              else $fatal(1, "branch target was requested before MEM redirected Fetch");
            saw_redirect <= 1'b1;
          end
          if (instruction_access_out.request.bits.address == 64'h00000001_0000004c &&
              saw_fence_i_invalidate)
            saw_fence_i_refetch <= 1'b1;
          if (instruction_access_out.request.bits.address == 64'h00000001_00000054 &&
              fetch_flushes >= 3) begin
            assert (saw_fence_i_refetch && fetch_flushes >= 3)
              else $fatal(1, "JAL target was requested without branch, fence, and JAL flushes");
            saw_jal_redirect <= 1'b1;
          end
        end
      end

      if (data_access_out.request.valid && !data_access_in.request.ready) begin
        assert (!rejected_first_load && load_requests == 0 &&
                data_access_out.request.bits.access == MEMORY_LOAD &&
                data_access_out.request.bits.address == 64'd0 &&
                data_access_out.request.bits.destination == DATA_DESTINATION_INTEGER &&
                data_access_out.request.bits.rd == 5'd5)
          else $fatal(1, "unexpected or repeated rejected data request");
        rejected_first_load <= 1'b1;
      end

      if (data_response_valid) begin
        data_response_valid <= 1'b0;
        if (data_response_rd == 5'd5) begin
          first_response_sent <= 1'b1;
          second_response_delay <= 3'd3;
        end else begin
          assert (data_response_rd == 5'd8)
            else $fatal(1, "unexpected completion destination register");
          second_response_sent <= 1'b1;
        end
      end else if (!data_response_valid) begin
        if (load_requests != 0 && !first_response_sent) begin
          if (first_response_delay != 0)
            first_response_delay <= first_response_delay - 1'b1;
          else begin
            data_response_valid <= 1'b1;
            data_response_bits <= 64'd42;
            data_response_rd <= 5'd5;
          end
        end else if (saw_redirect && first_response_sent && !second_response_sent) begin
          if (second_response_delay != 0)
            second_response_delay <= second_response_delay - 1'b1;
          else begin
            data_response_valid <= 1'b1;
            data_response_bits <= 64'd100;
            data_response_rd <= 5'd8;
          end
        end
      end

      if (data_access_out.request.valid && data_access_in.request.ready) begin
        if (data_access_out.request.bits.access == MEMORY_LOAD) begin
          if (load_requests == 0) begin
            assert (rejected_first_load && saw_replay_refetch)
              else $fatal(1, "load was accepted without a WB replay and refetch");
            assert (data_access_out.request.bits.address == 64'd0 &&
                    data_access_out.request.bits.destination == DATA_DESTINATION_INTEGER &&
                    data_access_out.request.bits.rd == 5'd5)
              else $fatal(1, "first load lost its address or destination register");
          end else
            assert (load_requests == 1 &&
                    data_access_out.request.bits.address == 64'd16 &&
                    data_access_out.request.bits.destination == DATA_DESTINATION_INTEGER &&
                    data_access_out.request.bits.rd == 5'd8)
              else $fatal(1, "second load lost its address or destination register");
          if (load_requests == 0)
            first_response_delay <= 2'd1;
          load_requests <= load_requests + 1'b1;
        end else begin
          assert (data_access_out.request.bits.address != 64'd32)
            else $fatal(1, "a wrong-path store executed after a MEM-stage redirect");
          assert (second_response_sent)
            else $fatal(1, "a younger store passed a RAW or WAW hazard");
          if (stores_seen == 0) begin
            assert (data_access_out.request.bits.address == 64'd8 &&
                    data_access_out.request.bits.data == 64'd43 &&
                    data_access_out.request.bits.destination == DATA_DESTINATION_NONE)
              else $fatal(1, "RAW-dependent result was incorrect");
            stores_seen <= 1;
          end else if (stores_seen == 1) begin
            assert (
                    data_access_out.request.bits.address == 64'd16 &&
                    data_access_out.request.bits.data == 64'd9 &&
                    data_access_out.request.bits.destination == DATA_DESTINATION_NONE)
              else $fatal(1, "WAW ordering was not preserved");
            stores_seen <= 2;
          end else begin
            assert (stores_seen == 2 &&
                    data_access_out.request.bits.address == 64'd24 &&
                    data_access_out.request.bits.data == 64'h00000001_00000050 &&
                    data_access_out.request.bits.destination == DATA_DESTINATION_NONE)
              else $fatal(1, "JAL link writeback was not preserved through MEM redirect");
            assert (saw_fence_i_invalidate && saw_fence_i_refetch)
              else $fatal(1, "FENCE.I did not invalidate and refetch from pc + 4");
            assert (saw_jal_redirect)
              else $fatal(1, "JAL did not redirect from MEM");
            assert (rejected_first_load && saw_replay_refetch)
              else $fatal(1, "memory replay was not observed before completion");
            $display("RV5Stage memory replay, redirect, and out-of-order load completion passed");
            $finish;
          end
        end
      end
      assert (!fault) else $fatal(1, "pipeline raised an unexpected fault");
    end
  end

  initial begin
    interrupts = '0;
    start_in.valid = 1'b0;
    start_in.bits = 64'h00000001_00000000;
    repeat (2) @(posedge clock);
    #1;
    reset = 1'b0;
    start_in.valid = 1'b1;
    do begin
      @(posedge clock);
      #1;
    end while (!start_out.ready);
    start_in.valid = 1'b0;
    repeat (200) @(posedge clock);
    $fatal(1, "core did not complete the scoreboard scenario");
  end
endmodule
