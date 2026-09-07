// Boots the complete core through an uncached register load and jalr under delayed CHI service.
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
  logic clock = 0, reset = 1, fault;
  struct packed { logic valid; logic [63:0] bits; } start_in;
  struct packed { logic ready; } start_out;
  chi_in_t umem_in;
  chi_out_t umem_out;
  localparam int IDLE = 0, READ = 1, DBID = 2, DATA = 3, COMP = 4;
  int state = IDLE, delay_left = 0, latency = 0, cycle = 0;
  int boot_reads = 0, stores = 0, completions = 0, payload_fetches = 0;
  logic [43:0] address;
  logic [127:0] read_value;

  RV5Stage dut (
    .clock, .reset, .start_in, .start_out, .fault,
    .chi_identity({7'd2, 7'd3, 7'd5}),
    .interrupts('0), .hart_id(64'd0), .time_counter(64'd0),
    .imem_in('0), .dmem_in('0), .imem_out(), .dmem_out(),
    .umem_in, .umem_out
  );

  function automatic logic [31:0] instruction_at(input logic [43:0] pc);
    case (pc)
      44'hc000: return 32'h000082b7; // lui t0, 8       # boot register at 0x8000
      44'hc004: return 32'h0002b303; // ld t1, 0(t0)
      44'hc008: return 32'h00030067; // jalr zero, t1, 0
      44'hc100: return 32'h02a00313; // addi t1, zero, 42
      44'hc104: return 32'h0062a423; // sw t1, 8(t0)    # first signature
      44'hc108: return 32'h0ff0000f; // fence iorw, iorw
      44'hc10c: return 32'h0062a623; // sw t1, 12(t0)   # after first completion
      44'hc110: return 32'h10500073; // wfi
      44'hc114: return 32'hffdff06f; // j -4
      default: return 32'h00000013;
    endcase
  endfunction

  always_comb begin
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
      read_value <= 0;
      address <= 0;
    end else begin
      cycle <= cycle + 1;
      if (delay_left != 0) delay_left <= delay_left - 1;
      assert (!fault) else $fatal(1, "uncached boot faulted");
      assert (!dut.imem_out.requests.valid && !dut.dmem_out.requests.valid)
        else $fatal(1, "non-cacheable boot allocated or accessed an L1 cache");
      if (umem_out.req.valid && umem_in.req.ready) begin
        address <= umem_out.req.bits.address;
        assert (state == IDLE && umem_out.req.bits.tgt_id == 7'd4 &&
                !umem_out.req.bits.mem_attr.cacheable && !umem_out.req.bits.mem_attr.allocate)
          else $fatal(1, "invalid or overlapping boot transaction");
        delay_left <= latency;
        if (umem_out.req.bits.opcode == 7'h04) begin
          state <= READ;
          if (umem_out.req.bits.address == 44'h8000) begin
            assert (umem_out.req.bits.size_or_num_req == 6'd3 && boot_reads == 0)
              else $fatal(1, "boot register load duplicated or widened");
            boot_reads <= boot_reads + 1;
            read_value <= 128'hc100;
          end else begin
            assert (umem_out.req.bits.address >= 44'hc000 &&
                    umem_out.req.bits.address < 44'h10000 &&
                    umem_out.req.bits.size_or_num_req == 6'd2)
              else $fatal(1, "unexpected instruction read");
            read_value <= 128'(instruction_at(umem_out.req.bits.address)) <<
                          (32 * umem_out.req.bits.address[3:2]);
            if (umem_out.req.bits.address == 44'hc100) payload_fetches <= payload_fetches + 1;
          end
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
    start_in = '0;
    for (int run = 0; run < 3; run++) begin
      latency = run == 0 ? 0 : run == 1 ? 3 : 17;
      reset = 1;
      tick();
      reset = 0;
      start_in.valid = 1;
      start_in.bits = 64'hc000;
      #1;
      assert (start_out.ready) else $fatal(1, "boot start not accepted");
      tick();
      start_in.valid = 0;
      for (int wait_cycle = 0; wait_cycle < 3000 && completions != 2; wait_cycle++) tick();
      assert (completions == 2 && boot_reads == 1 && payload_fetches > 0)
        else $fatal(1, "indirect boot made no progress at latency %0d", latency);
      repeat (80) tick();
      assert (completions == 2 && boot_reads == 1 && stores == 2)
        else $fatal(1, "boot produced repeated architectural effects");
    end
    $display("RV5Stage uncached ld/jalr boot and IO fence passed at three CHI latencies");
    $finish;
  end
endmodule
