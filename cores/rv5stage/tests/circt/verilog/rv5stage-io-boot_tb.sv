// Executes cached polling BootROM with delayed uncached entry publication, secondary parking, and IO ordering.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_io_boot_tb;
  typedef struct packed {
    struct packed { logic ready; } req;
    struct packed {
      struct packed { logic ready; } requester;
      struct packed { logic valid; CHIRspFlit bits; } response;
    } rsp;
    struct packed {
      struct packed { logic ready; } request;
      struct packed { logic valid; CHIDatFlit bits; } response;
    } dat;
  } chi_in_t;
  typedef struct packed {
    struct packed { logic valid; CHIReqFlit bits; } req;
    struct packed {
      struct packed { logic valid; CHIRspFlit bits; } requester;
      struct packed { logic ready; } response;
    } rsp;
    struct packed {
      struct packed { logic valid; CHIDatFlit bits; } request;
      struct packed { logic ready; } response;
    } dat;
  } chi_out_t;
  logic clock = 0, reset = 1;
  logic [63:0] hart_id = 0, entry_address = 0;
  logic [9:0][31:0] boot_words;
  chi_in_t umem_in;
  chi_out_t umem_out;
  chi_in_t imem_in;
  chi_out_t imem_out;
  bit rom_active = 0;
  int rom_delay = 0, rom_packet = 0, rom_line_reads = 0;
  logic [7:0] rom_lines_seen = 0;
  logic [43:0] rom_address;
  localparam int IDLE = 0, READ = 1, DBID = 2, DATA = 3, COMP = 4;
  int state = IDLE, delay_left = 0, latency = 0, cycle = 0;
  int boot_reads = 0, stores = 0, completions = 0, payload_fetches = 0;
  bit park_fetched = 0;
  logic [43:0] address;
  logic [127:0] read_value;

  RV5StagePollingBoot dut (
    .clock, .reset, .hart_id, .boot_words,
    .imem_in, .dmem_in('0), .imem_out, .dmem_out(),
    .umem_in, .umem_out
  );

  function automatic logic [31:0] instruction_at(input logic [43:0] pc);
    if (pc >= 44'hc000 && pc < 44'hc028) return boot_words[(pc - 44'hc000) >> 2];
    case (pc)
      44'hc100: return 32'h000082b7; // lui t0, 8
      44'hc104: return 32'h02a00313; // addi t1, zero, 42
      44'hc108: return 32'h0062a423; // sw t1, 8(t0)
      44'hc10c: return 32'h0ff0000f; // fence iorw, iorw
      44'hc110: return 32'h0062a623; // sw t1, 12(t0)
      44'hc114: return 32'h10500073; // wfi
      44'hc118: return 32'hffdff06f; // j -4
      default: return 32'h00000013;
    endcase
  endfunction

  always_comb begin
    imem_in = '0;
    imem_in.req.ready = !rom_active && cycle % 3 != 0;
    imem_in.dat.response.valid = rom_active && rom_delay == 0;
    imem_in.dat.response.bits.opcode = 4'h4;
    imem_in.dat.response.bits.home_nid_or_pbha_or_mismatched_mecid = 7'd4;
    imem_in.dat.response.bits.src_id = 7'd4;
    imem_in.dat.response.bits.tgt_id = 7'd2;
    imem_in.dat.response.bits.data_id = 2'(rom_packet);
    imem_in.dat.response.bits.byte_enable = '1;
    for (int word_index = 0; word_index < 4; word_index++)
      imem_in.dat.response.bits.data[word_index*32 +: 32] = instruction_at(rom_address + 44'(rom_packet*16 + word_index*4));
    umem_in = '0;
    umem_in.req.ready = state == IDLE && cycle % 4 != 0;
    umem_in.dat.request.ready = state == DATA && delay_left == 0;
    umem_in.dat.response.valid = state == READ && delay_left == 0;
    umem_in.dat.response.bits.opcode = 4'h4;
    umem_in.dat.response.bits.home_nid_or_pbha_or_mismatched_mecid = 7'd4;
    umem_in.dat.response.bits.data = read_value;
    umem_in.rsp.response.valid = (state == DBID || state == COMP) && delay_left == 0;
    umem_in.rsp.response.bits.opcode = state == DBID ? 5'h06 : 5'h04;
    umem_in.rsp.response.bits.src_id = 7'd4;
    umem_in.rsp.response.bits.dbid_or_group_id = 12'h123;
  end

  always @(posedge clock) begin
    if (reset) begin
      state <= IDLE;
      delay_left <= 0;
      cycle <= 0;
      boot_reads <= 0;
      stores <= 0;
      completions <= 0;
      payload_fetches <= 0;
      park_fetched <= 0;
      read_value <= 0;
      address <= 0;
      rom_active <= 0;
      rom_delay <= 0;
      rom_packet <= 0;
      rom_address <= 0;
      rom_line_reads <= 0;
      rom_lines_seen <= 0;
    end else begin
      cycle <= cycle + 1;
      if (delay_left != 0) delay_left <= delay_left - 1;

      assert (!dut.dmem_out.requests.valid && !imem_out.rsp.requester.valid)
        else $fatal(1, "ROM acquired data-cache ownership or sent CompAck");
      if (rom_delay != 0) rom_delay <= rom_delay - 1;
      if (imem_out.req.valid && imem_in.req.ready) begin
        assert (imem_out.req.bits.opcode == 7'h04 && imem_out.req.bits.size_or_num_req == 6 &&
                imem_out.req.bits.src_id == 2 && imem_out.req.bits.tgt_id == 4 &&
                !imem_out.req.bits.exp_comp_ack && imem_out.req.bits.mem_attr == 0 &&
                imem_out.req.bits.address >= 44'hc000 && imem_out.req.bits.address < 44'hc200 &&
                imem_out.req.bits.address[5:0] == 0 && !rom_lines_seen[imem_out.req.bits.address[8:6]])
          else $fatal(1, "invalid ROM cache-line transaction: opcode=%h size=%d src=%d tgt=%d ack=%b attributes=%h address=%h", imem_out.req.bits.opcode, imem_out.req.bits.size_or_num_req, imem_out.req.bits.src_id, imem_out.req.bits.tgt_id, imem_out.req.bits.exp_comp_ack, imem_out.req.bits.mem_attr, imem_out.req.bits.address);
        rom_active <= 1;
        rom_address <= imem_out.req.bits.address;
        rom_delay <= latency;
        rom_packet <= 0;
        rom_line_reads <= rom_line_reads + 1;
        // Fetch may speculate past WFI while older IO retires. Every such
        // read must still acquire a new ROM line, never repeat a resident one.
        rom_lines_seen[imem_out.req.bits.address[8:6]] <= 1;
        if (imem_out.req.bits.address == 44'hc100) payload_fetches <= payload_fetches + 1;
        if (imem_out.req.bits.address == 44'hc000) park_fetched <= 1;
      end
      if (imem_in.dat.response.valid && imem_out.dat.response.ready) begin
        rom_packet <= rom_packet + 1;
        rom_delay <= latency;
        if (rom_packet == 3) rom_active <= 0;
      end
      if (umem_out.req.valid && umem_in.req.ready) begin
        address <= umem_out.req.bits.address;
        assert (state == IDLE && umem_out.req.bits.tgt_id == 7'd4 &&
                !umem_out.req.bits.mem_attr.cacheable && !umem_out.req.bits.mem_attr.allocate)
          else $fatal(1, "invalid or overlapping boot transaction");
        delay_left <= latency;
        if (umem_out.req.bits.opcode == 7'h04) begin
          state <= READ;
          assert (umem_out.req.bits.address == 44'h8000 &&
                  umem_out.req.bits.size_or_num_req == 6'd3 && hart_id == 0)
            else $fatal(1, "only primary-hart entry polling may use uncached reads");
          boot_reads <= boot_reads + 1;
          read_value <= 128'(entry_address);
        end else begin
          assert (umem_out.req.bits.opcode == 7'h1c &&
                  umem_out.req.bits.size_or_num_req == 6'd2 &&
                  umem_out.req.bits.address == (stores == 0 ? 44'h8008 : 44'h800c) &&
                  stores < 2 && completions == stores)
            else $fatal(1, "signature store duplicated, reordered, or passed fence");
          stores <= stores + 1;
          state <= DBID;
        end
      end
      if (umem_in.dat.response.valid && umem_out.dat.response.ready) state <= IDLE;
      if (umem_in.rsp.response.valid && umem_out.rsp.response.ready) begin
        delay_left <= latency;
        if (state == DBID) state <= DATA;
        else begin
          completions <= completions + 1;
          state <= IDLE;
        end
      end
      if (umem_out.dat.request.valid && umem_in.dat.request.ready) begin
        assert (umem_out.dat.request.bits.txn_id == 12'h123 &&
                umem_out.dat.request.bits.byte_enable == (address == 44'h8008 ? 16'h0f00 : 16'hf000) &&
                umem_out.dat.request.bits.data == (128'd42 << (8 * address[3:0])))
          else $fatal(1, "incorrect signature store payload");
        state <= COMP;
        delay_left <= latency + 8;
      end
    end
  end

  task automatic tick;
    #5 clock = 1;
    #1 clock = 0;
    #4;
  endtask

  initial begin
    for (int run = 0; run < 3; run++) begin
      latency = run == 0 ? 0 : run == 1 ? 3 : 17;
      reset = 1;
      entry_address = 0;
      tick();
      reset = 0;
      for (int wait_cycle = 0; wait_cycle < 3000 && boot_reads < 3; wait_cycle++) tick();
      assert (boot_reads >= 3 && stores == 0 && payload_fetches == 0 && rom_lines_seen[0])
        else $fatal(1, "core did not wait in ROM for entry publication");
      entry_address = 64'hc100;
      for (int wait_cycle = 0; wait_cycle < 3000 && completions != 2; wait_cycle++) tick();
      assert (completions == 2 && boot_reads >= 4 && payload_fetches == 1 && rom_lines_seen[0] && rom_lines_seen[4])
        else $fatal(1, "indirect boot made no progress at latency %0d", latency);
      repeat (80) tick();
      assert (completions == 2 && stores == 2)
        else $fatal(1, "boot produced repeated architectural effects");
      $display("Cached polling boot passed at latency %0d: %0d distinct ROM lines including speculation", latency, rom_line_reads);
    end
    reset = 1; hart_id = 1; tick(); reset = 0;
    repeat (600) tick();
    assert (park_fetched && rom_lines_seen[0] && boot_reads == 0 && payload_fetches == 0 && stores == 0 && state == IDLE)
      else $fatal(1, "secondary hart did not park in ROM");
    $display("RV5Stage generated polling ROM, reset, secondary parking, and IO fence passed at three CHI latencies");
    $finish;
  end
endmodule
