// Checks final entry publication after host drain, CHI stalls/errors, reserved writes, and reset.
module fesvr_boot_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_t;
  typedef struct packed { logic valid; CHISnpFlit bits; } snp_t;
  typedef struct packed {
    ready_t requests, requester_responses, request_data;
    rsp_t responses;
    dat_t response_data;
    snp_t snoops;
  } rn_in_t;
  typedef struct packed {
    req_t requests;
    rsp_t requester_responses;
    dat_t request_data;
    ready_t responses, response_data, snoops;
  } rn_out_t;
  typedef struct packed { logic valid; FesvrMemoryRequest bits; } command_t;
  typedef struct packed { logic valid; FesvrMemoryResponse bits; } completion_t;
  logic clock = 0, reset = 1, failed;
  command_t requests_in;
  ready_t requests_out, responses_in, entry_out;
  completion_t responses_out;
  struct packed { logic valid; logic [63:0] bits; } entry_in;
  rn_in_t port_in;
  rn_out_t port_out;
  FesvrBootAccess dut(.*);
  always #5 clock = ~clock;
  initial begin #100000; $fatal(1, "FESVR boot timeout"); end
  task automatic tick;
    @(posedge clock); #1;
  endtask
  task automatic restart;
    reset = 1;
    requests_in = '0; responses_in = '0; entry_in = '0;
    port_in = '0;
    repeat (3) tick();
    reset = 0; tick();
    assert(!failed && !entry_out.ready);
  endtask
  task automatic stalled(int cycles = 4);
    repeat (cycles) begin
      assert(!entry_out.ready && !failed)
        else $fatal(1, "entry acknowledged before successful final Comp");
      tick();
    end
  endtask
  task automatic write_transaction(logic [63:0] value, int error_kind = 0, logic [43:0] target_address = 44'h3000);
    wait (port_out.requests.valid); #1;
    repeat (4) begin
      assert(port_out.requests.valid && port_out.requests.bits.address == target_address);
      assert(port_out.requests.bits.opcode == 7'h1c && port_out.requests.bits.size_or_num_req == 3);
      assert(port_out.requests.bits.tgt_id == 6 && port_out.requests.bits.src_id == 1);
      stalled(1);
    end
    port_in.requests.ready = 1; tick(); port_in.requests.ready = 0;
    stalled(); // DBID response delayed independently.
    port_in.responses = '0;
    port_in.responses.valid = 1;
    port_in.responses.bits.opcode = 5'h06;
    port_in.responses.bits.src_id = 6;
    port_in.responses.bits.tgt_id = 1;
    port_in.responses.bits.dbid_or_group_id = 12'h357;
    tick(); port_in.responses.valid = 0;
    repeat (4) begin
      assert(port_out.request_data.valid && port_out.request_data.bits.txn_id == 12'h357);
      assert(port_out.request_data.bits.tgt_id == 6);
      assert(port_out.request_data.bits.byte_enable == 16'hff);
      assert(port_out.request_data.bits.data == 128'(value));
      stalled(1);
    end
    port_in.request_data.ready = 1; tick(); port_in.request_data.ready = 0;
    stalled(); // Accepting DAT is not sufficient to acknowledge publication.
    port_in.responses.valid = 1;
    port_in.responses.bits.opcode = 5'h04;
    if (error_kind == 1) port_in.responses.bits.resp_err = 2'd2;
    if (error_kind == 2) port_in.responses.bits.txn_id = 1;
    tick(); port_in.responses.valid = 0;
  endtask
  task automatic acknowledge_entry;
    wait (entry_out.ready); #1;
    assert(!responses_out.valid && !port_out.requests.valid && !failed);
    tick(); entry_in.valid = 0;
    repeat (4) begin
      assert(!entry_out.ready && !port_out.requests.valid);
      tick();
    end
  endtask
  initial begin
    restart();
    // A loading write and entry arrive together: finish the host transaction,
    // including its backpressured response, before inserting the entry write.
    requests_in = '{valid: 1, bits: '{write: 1, address: 64'h3010, data: 64'hdeadbeef, length: 8'd8}};
    entry_in = '{valid: 1, bits: 64'h80002000};
    tick(); requests_in.valid = 0;
    write_transaction(64'hdeadbeef, 0, 44'h3010);
    repeat (4) begin
      assert(responses_out.valid && responses_out.bits.status == 0);
      assert(!port_out.requests.valid);
      stalled(1);
    end
    responses_in.ready = 1; tick(); responses_in.ready = 0;
    write_transaction(64'h80002000);
    acknowledge_entry();
    // Ordinary polling resumes after publication and is not mistaken for startup.
    requests_in = '{valid: 1, bits: '{write: 0, address: 64'h3000, data: 64'd0, length: 8'd8}};
    tick(); requests_in.valid = 0;
    wait (port_out.requests.valid); #1;
    assert(port_out.requests.bits.opcode == 7'h04);
    port_in.requests.ready = 1; tick(); port_in.requests.ready = 0;
    port_in.response_data = '0;
    port_in.response_data.valid = 1;
    port_in.response_data.bits.opcode = 4'h4;
    port_in.response_data.bits.src_id = 6;
    port_in.response_data.bits.home_nid_or_pbha_or_mismatched_mecid = 6;
    port_in.response_data.bits.tgt_id = 1;
    port_in.response_data.bits.data = 128'h80002000;
    tick(); port_in.response_data.valid = 0;
    wait (responses_out.valid); #1;
    assert(responses_out.bits.status == 0 && responses_out.bits.data == 64'h80002000);
    responses_in.ready = 1; tick(); responses_in.ready = 0;
    for (int error_kind = 1; error_kind <= 2; error_kind++) begin
      restart();
      entry_in = '{valid: 1, bits: 64'h80003000};
      write_transaction(64'h80003000, error_kind);
      repeat (3) tick();
      repeat (4) begin
        assert(failed && !entry_out.ready && !responses_out.valid);
        assert(!port_out.requests.valid && !requests_out.ready);
        tick();
      end
    end
    restart();
    entry_in = '{valid: 1, bits: 64'h180002000};
    tick();
    assert(failed && !port_out.requests.valid && !entry_out.ready); // RV32 overflow
    restart();
    entry_in = '{valid: 1, bits: 64'd0}; tick();
    assert(failed && !port_out.requests.valid && !entry_out.ready); // Zero is the waiting sentinel.
    for (int offset = -4; offset <= 4; offset += 4) begin
      restart();
      requests_in = '{valid: 1, bits: '{write: 1, address: (offset == -4 ? 64'h2ffc : offset == 0 ? 64'h3000 : 64'h3004), data: 64'h80002000, length: 8'd8}};
      tick();
      assert(failed && !requests_out.ready && !port_out.requests.valid)
        else $fatal(1, "reserved write %h: failed=%b ready=%b CHI valid=%b", requests_in.bits.address, failed, requests_out.ready, port_out.requests.valid);
    end
    restart();
    entry_in = '{valid: 1, bits: 64'h80003000};
    write_transaction(64'h80003000);
    acknowledge_entry();
    $display("FESVR boot publication, stalls, errors, reserved writes, RV32/zero entry, and reset passed");
    $finish;
  end
endmodule
