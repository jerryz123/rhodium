// Verifies RV5StageCore forwarding priority, captured operands, replay, redirects, and ordered commit.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_core_tb;
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
  logic data_response_valid;
  logic [63:0] data_response_bits;
  logic [4:0] data_response_rd;
  logic [1:0] load_requests;
  logic first_response_sent;
  logic [1:0] first_response_delay;
  logic second_response_sent;
  logic [2:0] second_response_delay;
  logic [3:0] stores_seen;
  logic rejected_first_load;
  logic saw_replay_refetch;
  logic saw_fetch_flush;
  logic saw_redirect;
  logic saw_jal_redirect;
  logic saw_jal_flush;
  logic saw_fence_i_invalidate;
  logic saw_fence_i_refetch;
  logic [2:0] fetch_flushes;
  localparam logic [3:0] MEMORY_LOAD = 4'd1;
  localparam logic [1:0] DATA_DESTINATION_NONE = 2'd0;
  localparam logic [1:0] DATA_DESTINATION_INTEGER = 2'd1;

  RV5StageCoreFixture dut (.pipeline_access_in('0), .pipeline_access_out(), .prefetch_out(), .*);
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
      64'h00000001_00000060: instruction_at = 32'h00100593; // addi x11, x0, 1
      64'h00000001_00000064: instruction_at = 32'h00258593; // addi x11, x11, 2
      64'h00000001_00000068: instruction_at = 32'h00b58633; // add x12, x11, x11; newest writer wins
      64'h00000001_0000006c: instruction_at = 32'h00400693; // addi x13, x0, 4
      64'h00000001_00000070: instruction_at = 32'h00d60733; // add x14, x12, x13; two forwarding sources
      64'h00000001_00000074: instruction_at = 32'h00970013; // addi x0, x14, 9; must not forward to x0
      64'h00000001_00000078: instruction_at = 32'h00e007b3; // add x15, x0, x14
      64'h00000001_0000007c: instruction_at = 32'h02c03423; // sd x12, 40(x0)
      64'h00000001_00000080: instruction_at = 32'h02e03823; // sd x14, 48(x0)
      64'h00000001_00000084: instruction_at = 32'h02f03c23; // sd x15, 56(x0)
      64'h00000001_00000088: instruction_at = 32'h01400813; // addi x16, x0, 20
      64'h00000001_0000008c: instruction_at = 32'h00100893; // addi x17, x0, 1
      64'h00000001_00000090: instruction_at = 32'h00200913; // addi x18, x0, 2
      64'h00000001_00000094: instruction_at = 32'h011809b3; // add x19, x16, x17; WB-to-ID capture
      64'h00000001_00000098: instruction_at = 32'h05303023; // sd x19, 64(x0)
      64'h00000001_0000009c: instruction_at = 32'h04003423; // sd x0, 72(x0)
      64'h00000001_000000a0: instruction_at = 32'h04b03823; // sd x11, 80(x0)
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
    data_access_in.reservation_valid = 1'b0;
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
      saw_jal_flush <= 1'b0;
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
        if (saw_fence_i_refetch && !instruction_access_out.invalidate_all)
          saw_jal_flush <= 1'b1;
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
          // Sequential lookahead may fetch this address before JAL resolves.
          // Count only its re-fetch after the post-FENCE.I redirect.
          if (instruction_access_out.request.bits.address == 64'h00000001_00000054 &&
              saw_jal_flush) begin
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
          end else if (stores_seen == 2) begin
            assert (
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
            stores_seen <= 3;
          end else begin
            assert (data_access_out.request.bits.destination == DATA_DESTINATION_NONE)
              else $fatal(1, "forwarding test store acquired a destination");
            case (stores_seen)
              3: assert (data_access_out.request.bits.address == 40 && data_access_out.request.bits.data == 6)
                else $fatal(1, "newest-writer forwarding priority failed");
              4: assert (data_access_out.request.bits.address == 48 && data_access_out.request.bits.data == 10)
                else $fatal(1, "dual-source forwarding failed");
              5: assert (data_access_out.request.bits.address == 56 && data_access_out.request.bits.data == 10)
                else $fatal(1, "x0 incorrectly selected a forwarding source");
              6: assert (data_access_out.request.bits.address == 64 && data_access_out.request.bits.data == 21)
                else $fatal(1, "WB-to-ID operand capture failed");
              7: assert (data_access_out.request.bits.address == 72 && data_access_out.request.bits.data == 0)
                else $fatal(1, "x0 store operand was not zero");
              8: begin
                assert (data_access_out.request.bits.address == 80 && data_access_out.request.bits.data == 3)
                  else $fatal(1, "captured register-file operand was stale");
                $display("RV5Stage forwarding, operand capture, replay, redirect, and deferred completion passed");
                $finish;
              end
              default: $fatal(1, "unexpected forwarding test store");
            endcase
            stores_seen <= stores_seen + 1'b1;
          end
        end
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
    repeat (200) @(posedge clock);
    $fatal(1, "core did not complete the scoreboard scenario");
  end
endmodule
