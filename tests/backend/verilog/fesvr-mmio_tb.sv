// Checks exact host MMIO widths, RAM fragmentation, CHI backpressure, and failures.
module fesvr_mmio_tb;
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
  logic clock = 0, reset = 1;
  command_t requests_in;
  ready_t requests_out, responses_in;
  completion_t responses_out;
  rn_in_t port_in;
  rn_out_t port_out;
  FesvrCHIAccess dut(.*);
  always #5 clock = ~clock;
  initial begin #100000; $fatal(1, "host MMIO timeout"); end

  task automatic tick;
    @(posedge clock); #1;
  endtask
  task automatic issue(bit wr, logic [63:0] address, logic [63:0] data, int bytes);
    assert(requests_out.ready) else $fatal(1, "host not idle");
    requests_in = '{valid: 1, bits: '{write: wr, address: address, data: data, length: 8'(bytes)}};
    tick();
    requests_in.valid = 0;
  endtask
  task automatic expect_request(bit wr, bit coherent, bit device, logic [63:0] address, int size);
    CHIReqFlit saved;
    saved = port_out.requests.bits;
    repeat (3) begin
      assert(port_out.requests.valid && port_out.requests.bits == saved)
        else $fatal(1, "request not stable under backpressure");
      assert(saved.address == address[43:0] && saved.size_or_num_req == 6'(size));
      assert(saved.tgt_id == (coherent ? 5 : 6) && saved.src_id == 1);
      assert(saved.opcode == (coherent ? (wr ? 7'h18 : 7'h02) : (wr ? 7'h1c : 7'h04)))
        else $fatal(1, "wrong host request opcode: %h", saved.opcode);
      assert(saved.mem_attr.cacheable == coherent && saved.mem_attr.device == device);
      assert(saved.snp_attr_or_do_dwt == coherent && !saved.mem_attr.early_write_acknowledge);
      tick();
    end
    port_in.requests.ready = 1;
    tick();
    port_in.requests.ready = 0;
  endtask
  task automatic return_read(bit coherent, logic [63:0] address, logic [63:0] data, int error_kind = 0);
    port_in.response_data = '0;
    port_in.response_data.valid = 1;
    port_in.response_data.bits.opcode = 4'h4;
    port_in.response_data.bits.src_id = coherent ? 9 : 6;
    port_in.response_data.bits.home_nid_or_pbha_or_mismatched_mecid = coherent ? 5 : 6;
    port_in.response_data.bits.tgt_id = 1;
    port_in.response_data.bits.data_id = coherent ? address[5:4] : 0;
    port_in.response_data.bits.data = 128'(data) << (address[3:0] * 8);
    case (error_kind)
      1: port_in.response_data.bits.resp_err = 2'd2;
      2: port_in.response_data.bits.txn_id = 1;
      3: port_in.response_data.bits.home_nid_or_pbha_or_mismatched_mecid = 7;
      4: port_in.response_data.bits.data_id = 3;
      5: port_in.response_data.bits.opcode = 4'h3;
      6: port_in.response_data.bits.src_id = 7;
      default: ;
    endcase
    tick();
    port_in.response_data.valid = 0;
  endtask
  task automatic return_write(bit coherent, logic [63:0] address, logic [63:0] data, int bytes, int comp_error = 0);
    port_in.responses = '0;
    port_in.responses.valid = 1;
    port_in.responses.bits.opcode = 5'h06;
    port_in.responses.bits.src_id = coherent ? 5 : 6;
    port_in.responses.bits.tgt_id = 1;
    port_in.responses.bits.dbid_or_group_id = 12'h357;
    tick();
    port_in.responses.valid = 0;
    repeat (3) begin
      assert(port_out.request_data.valid && port_out.request_data.bits.txn_id == 12'h357);
      assert(port_out.request_data.bits.tgt_id == (coherent ? 5 : 6));
      assert(port_out.request_data.bits.byte_enable == 16'(((1 << bytes) - 1) << address[3:0]));
      assert(port_out.request_data.bits.data == (128'(data) << (address[3:0] * 8)));
      assert(port_out.request_data.bits.data_id == (coherent ? address[5:4] : 0));
      assert(!responses_out.valid);
      tick();
    end
    port_in.request_data.ready = 1;
    tick();
    port_in.request_data.ready = 0;
    repeat (3) begin assert(!responses_out.valid); tick(); end
    port_in.responses.valid = 1;
    port_in.responses.bits.opcode = 5'h04;
    if (comp_error == 1) port_in.responses.bits.resp_err = 2'd2;
    if (comp_error == 2) port_in.responses.bits.txn_id = 1;
    tick();
    port_in.responses.valid = 0;
  endtask
  task automatic finish_command(logic [63:0] data, int status = 0);
    repeat (3) begin
      assert(responses_out.valid && responses_out.bits.status == 8'(status))
        else $fatal(1, "host completion status: expected %0d, got %h", status, responses_out.bits.status);
      if (status == 0) assert(responses_out.bits.data == data) else $fatal(1, "wrong read data");
      assert(!requests_out.ready && !port_out.requests.valid);
      tick();
    end
    responses_in.ready = 1;
    tick();
    responses_in.ready = 0;
    assert(requests_out.ready);
  endtask
  initial begin
    requests_in = '0; responses_in = '0; port_in = '0;
    repeat (3) tick(); reset = 0; tick();
    issue(1, 64'h1000, 64'h8877665544332211, 8);
    expect_request(1, 0, 1, 64'h1000, 3);
    // The shared RN-F still services coherent snoops while waiting on MMIO.
    port_in.snoops.valid = 1;
    port_in.snoops.bits.opcode = 5'h02;
    port_in.snoops.bits.src_id = 5;
    port_in.snoops.bits.txn_id = 12'h246;
    assert(port_out.snoops.ready);
    tick(); port_in.snoops.valid = 0;
    repeat (3) begin
      assert(port_out.requester_responses.valid);
      assert(port_out.requester_responses.bits.opcode == 5'h01);
      assert(port_out.requester_responses.bits.resp == 0);
      assert(port_out.requester_responses.bits.tgt_id == 5);
      assert(port_out.requester_responses.bits.txn_id == 12'h246);
      assert(!responses_out.valid);
      tick();
    end
    port_in.requester_responses.ready = 1;
    tick(); port_in.requester_responses.ready = 0;
    return_write(0, 64'h1000, 64'h8877665544332211, 8); finish_command(0);
    issue(0, 64'h1004, 0, 4); expect_request(0, 0, 1, 64'h1004, 2);
    return_read(0, 64'h1004, 64'h88776655); finish_command(64'h88776655);
    issue(1, 64'h10000007, 64'ha5, 1); expect_request(1, 0, 1, 64'h10000007, 0);
    return_write(0, 64'h10000007, 64'ha5, 1); finish_command(0);
    issue(0, 64'h10000007, 0, 1); expect_request(0, 0, 1, 64'h10000007, 0);
    return_read(0, 64'h10000007, 64'hfeedbea5); finish_command(64'ha5);
    issue(0, 64'h10000, 0, 4); expect_request(0, 0, 0, 64'h10000, 2);
    return_read(0, 64'h10000, 64'h12345678); finish_command(64'h12345678);
    // An unaligned RAM chunk is decomposed into 1+4+2+1 byte transactions.
    issue(0, 64'h80000003, 0, 8);
    expect_request(0, 1, 0, 64'h80000003, 0); return_read(1, 64'h80000003, 64'h11);
    expect_request(0, 1, 0, 64'h80000004, 2); return_read(1, 64'h80000004, 64'h55443322);
    expect_request(0, 1, 0, 64'h80000008, 1); return_read(1, 64'h80000008, 64'h7766);
    expect_request(0, 1, 0, 64'h8000000a, 0); return_read(1, 64'h8000000a, 64'h88);
    finish_command(64'h8877665544332211);
    issue(1, 64'h80000028, 64'h8877665544332211, 8);
    expect_request(1, 1, 0, 64'h80000028, 3);
    return_write(1, 64'h80000028, 64'h8877665544332211, 8); finish_command(0);
    // Write fragments preserve the original byte order across a cache line.
    issue(1, 64'h8000003f, 64'h8877665544332211, 8);
    expect_request(1, 1, 0, 64'h8000003f, 0); return_write(1, 64'h8000003f, 64'h8877665544332211, 1);
    expect_request(1, 1, 0, 64'h80000040, 2); return_write(1, 64'h80000040, 64'h88776655443322, 4);
    expect_request(1, 1, 0, 64'h80000044, 1); return_write(1, 64'h80000044, 64'h887766, 2);
    expect_request(1, 1, 0, 64'h80000046, 0); return_write(1, 64'h80000046, 64'h88, 1);
    finish_command(0);
    issue(1, 64'h10000, 0, 4); finish_command(0, 1); // ROM permission
    issue(0, 64'h10000000, 0, 4); finish_command(0, 1); // UART width
    issue(0, 64'h1001, 0, 4); finish_command(0, 1); // MMIO alignment
    issue(0, 64'h1004, 0, 8); finish_command(0, 1); // crosses register service
    issue(0, 64'h2000, 0, 4); finish_command(0, 1); // no mapping
    issue(0, 64'h1000000000001000, 0, 8); finish_command(0, 1); // high address
    issue(0, 64'h80000fff, 0, 2); finish_command(0, 1); // RAM hole: no first-byte access
    issue(0, 64'h1000, 0, 0); finish_command(0, 1);
    issue(0, 64'h1000, 0, 9); finish_command(0, 1);
    for (int error_kind = 1; error_kind <= 6; error_kind++) begin
      issue(0, 64'h1000, 0, 8); expect_request(0, 0, 1, 64'h1000, 3);
      return_read(0, 64'h1000, 0, error_kind);
      finish_command(0, error_kind == 1 ? 2 : 3);
    end
    for (int error_kind = 1; error_kind <= 2; error_kind++) begin
      issue(1, 64'h1000, 0, 8); expect_request(1, 0, 1, 64'h1000, 3);
      return_write(0, 64'h1000, 0, 8, error_kind);
      finish_command(0, error_kind == 1 ? 2 : 3);
    end
    issue(1, 64'h1000, 0, 8); expect_request(1, 0, 1, 64'h1000, 3);
    port_in.responses = '0;
    port_in.responses.valid = 1;
    port_in.responses.bits.opcode = 5'h06;
    port_in.responses.bits.src_id = 7; // DBID must come from the selected Home.
    port_in.responses.bits.tgt_id = 1;
    tick(); port_in.responses.valid = 0;
    assert(!port_out.request_data.valid);
    finish_command(0, 3);
    $display("Host coherent RAM, exact MMIO, backpressure, and errors passed");
    $finish;
  end
endmodule
