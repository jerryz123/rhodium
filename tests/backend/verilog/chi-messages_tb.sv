// Checks repeated CHI builders and complete Home transforms, including optional metadata at every DAT width.
module chi_messages_tb;
  logic [511:0] home_request_bits;
  logic [1023:0] home_data_bits;
  CHIReqFlit request, other_request;
  logic [15:0] node_id;
  logic [4:0] dbid;
  logic [1:0] data_id;
  logic [511:0] data;
  logic [15:0] other_node_id;
  logic [4:0] other_dbid;
  logic [1:0] other_data_id;
  logic [127:0] other_data;
  CHIMessageFixture dut(.request(request), .node_id(node_id), .dbid(dbid), .data_id(data_id), .data(data),
                        .home_request_bits(home_request_bits), .home_data_bits(home_data_bits),
                        .original_req_w128(), .original_dat_w128(), .downstream_w128(), .write_w128(), .upstream_w128(),
                        .requester_write_w128(), .snoop_w128(), .intervention_w128(),
                        .original_req_h128(), .original_dat_h128(), .downstream_h128(), .write_h128(), .upstream_h128(),
                        .requester_write_h128(), .snoop_h128(), .intervention_h128(),
                        .original_req_w256(), .original_dat_w256(), .downstream_w256(), .write_w256(), .upstream_w256(),
                        .requester_write_w256(), .snoop_w256(), .intervention_w256(),
                        .original_req_h256(), .original_dat_h256(), .downstream_h256(), .write_h256(), .upstream_h256(),
                        .requester_write_h256(), .snoop_h256(), .intervention_h256(),
                        .original_req_w512(), .original_dat_w512(), .downstream_w512(), .write_w512(), .upstream_w512(),
                        .requester_write_w512(), .snoop_w512(), .intervention_w512(),
                        .original_req_h512(), .original_dat_h512(), .downstream_h512(), .write_h512(), .upstream_h512(),
                        .requester_write_h512(), .snoop_h512(), .intervention_h512(),
                        .other_request(other_request), .other_node_id(other_node_id), .other_dbid(other_dbid),
                        .other_data_id(other_data_id), .other_data(other_data),
                        .dbid_response(), .write_response(), .other_dbid_response(), .other_write_response(), .other_read_response(),
                        .read_w128(), .read_o128(), .read_w256(), .read_o256(), .read_w512(), .read_o512());

  function automatic logic [63:0] expected_mask(input int bytes_per_packet, input int address, input int size);
    logic [63:0] mask;
    mask = '0;
    for (int b = 0; b < bytes_per_packet; b++)
      if ((1 << size) >= bytes_per_packet ||
          (b >= address % bytes_per_packet && b < (address % bytes_per_packet) + (1 << size))) mask[b] = 1;
    return mask;
  endfunction

`define CHECK_RSP(PORT, OPCODE, REQ, NODE, DBID) \
  assert (dut.PORT.opcode == OPCODE && dut.PORT.txn_id == REQ.txn_id && \
          dut.PORT.src_id == NODE && dut.PORT.tgt_id == REQ.src_id && \
          dut.PORT.dbid_or_group_id == {7'b0, DBID} && dut.PORT.qos == REQ.qos) \
    else $fatal(1, "response correlation/routing mismatch: %s", `"PORT`"); \
  assert ({dut.PORT.cache_line_id, dut.PORT.trace_tag, dut.PORT.tag_op, dut.PORT.pcrd_type, \
           dut.PORT.c_busy, dut.PORT.fwd_state_or_data_pull, dut.PORT.resp, dut.PORT.resp_err} == '0) \
    else $fatal(1, "response defaults mismatch: %s", `"PORT`");

`define CHECK_DAT(PORT, WIDTH, REQ, NODE, ID, DATA) \
  assert (dut.PORT.opcode == 4'h4 && dut.PORT.txn_id == REQ.return_txn_id_or_stash_lpid && \
          dut.PORT.src_id == NODE && dut.PORT.tgt_id == REQ.return_nid_or_stash_nid_or_data_target && \
          dut.PORT.home_nid_or_pbha_or_mismatched_mecid == NODE && dut.PORT.qos == REQ.qos && \
          dut.PORT.data_id == ID && dut.PORT.data == DATA[WIDTH-1:0]) \
    else $fatal(1, "read correlation/routing/data mismatch: %s", `"PORT`"); \
  assert (64'(dut.PORT.byte_enable) == expected_mask(WIDTH/8, addr, sz)) \
    else $fatal(1, "read byte mask mismatch: %s address=%0h size=%0d", `"PORT`", addr, sz); \
  assert ({dut.PORT.replicate, dut.PORT.num_dat, dut.PORT.cah, dut.PORT.trace_tag, \
           dut.PORT.tag_update, dut.PORT.tag, dut.PORT.tag_op, dut.PORT.cache_line_id, dut.PORT.ccid, \
           dut.PORT.dbid_or_mecid, dut.PORT.c_busy, dut.PORT.data_pull, \
           dut.PORT.data_source_or_fwd_state, dut.PORT.resp, dut.PORT.resp_err} == '0) \
    else $fatal(1, "read defaults mismatch: %s", `"PORT`");

`define CHECK_HOME(P) \
  begin \
    type(dut.original_req_``P) expected_req; \
    type(dut.original_dat_``P) expected_dat; \
    type(dut.snoop_``P) expected_snp; \
    expected_req = dut.original_req_``P; \
    expected_req.exp_comp_ack = 0; \
    expected_req.excl_snoop_me_cah = 0; \
    expected_req.snp_attr_or_do_dwt = 0; \
    expected_req.mem_attr.early_write_acknowledge = other_request.mem_attr.early_write_acknowledge; \
    expected_req.pcrd_type = 0; \
    expected_req.order = 0; \
    expected_req.allow_retry = 0; \
    expected_req.address = other_request.address; \
    expected_req.size_or_num_req = other_request.size_or_num_req; \
    expected_req.multi_req = 0; \
    expected_req.opcode = other_request.opcode; \
    expected_req.return_txn_id_or_stash_lpid = 0; \
    expected_req.stash_nid_valid_endian_deep_prefetch_tgt_hint = 0; \
    expected_req.return_nid_or_stash_nid_or_data_target = node_id; \
    expected_req.txn_id = other_request.txn_id; \
    expected_req.src_id = node_id; \
    expected_req.tgt_id = other_node_id; \
    assert (dut.downstream_``P === expected_req) else $fatal(1, "Home REQ transform mismatch"); \
    expected_dat = dut.original_dat_``P; \
    expected_dat.replicate = 0; \
    expected_dat.num_dat = 0; \
    expected_dat.cah = 0; \
    expected_dat.dbid_or_mecid = {4'b0, other_request.txn_id}; \
    expected_dat.c_busy = 0; \
    expected_dat.data_pull = 0; \
    expected_dat.data_source_or_fwd_state = 0; \
    expected_dat.resp = 0; \
    expected_dat.opcode = 4'h3; \
    expected_dat.home_nid_or_pbha_or_mismatched_mecid = node_id; \
    expected_dat.txn_id = other_request.txn_id; \
    expected_dat.src_id = node_id; \
    expected_dat.tgt_id = other_node_id; \
    assert (dut.write_``P === expected_dat) else $fatal(1, "Home write DAT transform mismatch"); \
    expected_dat = dut.original_dat_``P; \
    expected_dat.dbid_or_mecid = 0; \
    expected_dat.data_pull = 0; \
    expected_dat.data_source_or_fwd_state = 0; \
    expected_dat.resp = other_data[2:0]; \
    expected_dat.opcode = 4'h4; \
    expected_dat.home_nid_or_pbha_or_mismatched_mecid = node_id; \
    expected_dat.txn_id = dut.original_req_``P.return_txn_id_or_stash_lpid; \
    expected_dat.src_id = node_id; \
    expected_dat.tgt_id = dut.original_req_``P.return_nid_or_stash_nid_or_data_target; \
    expected_dat.qos = dut.original_req_``P.qos; \
    assert (dut.upstream_``P === expected_dat) else $fatal(1, "Home read DAT transform mismatch"); \
    expected_dat = '0; \
    expected_dat.data = dut.original_dat_``P.data; \
    expected_dat.byte_enable = dut.original_dat_``P.byte_enable; \
    expected_dat.data_id = dut.original_dat_``P.data_id; \
    expected_dat.ccid = dut.original_dat_``P.ccid; \
    expected_dat.dbid_or_mecid = dut.original_dat_``P.dbid_or_mecid; \
    expected_dat.opcode = 4'h3; \
    expected_dat.home_nid_or_pbha_or_mismatched_mecid = other_node_id; \
    expected_dat.txn_id = other_request.txn_id; \
    expected_dat.src_id = node_id; \
    expected_dat.tgt_id = other_node_id; \
    assert (dut.requester_write_``P === expected_dat) else $fatal(1, "Requester write DAT construction mismatch"); \
    expected_snp = '0; \
    expected_snp.trace_tag = dut.original_req_``P.trace_tag; \
    expected_snp.pas = dut.original_req_``P.pas; \
    expected_snp.address = other_request.address[51:3]; \
    expected_snp.opcode = 5'h07; \
    expected_snp.txn_id = other_request.txn_id; \
    expected_snp.src_id = node_id; \
    expected_snp.qos = dut.original_req_``P.qos; \
    assert (dut.snoop_``P === expected_snp) else $fatal(1, "Home SNP construction mismatch"); \
    expected_req = '0; \
    expected_req.trace_tag = dut.original_dat_``P.trace_tag; \
    expected_req.mem_attr = dut.original_req_``P.mem_attr; \
    expected_req.mem_attr.early_write_acknowledge = other_request.mem_attr.early_write_acknowledge; \
    expected_req.pas = dut.original_req_``P.pas; \
    expected_req.address = {dut.original_req_``P.address[51:6], dut.original_dat_``P.data_id, 4'b0}; \
    expected_req.size_or_num_req = 6'($clog2($bits(dut.original_dat_``P.data) / 8)); \
    expected_req.opcode = 7'h1d; \
    expected_req.return_nid_or_stash_nid_or_data_target = node_id; \
    expected_req.txn_id = other_request.txn_id; \
    expected_req.src_id = node_id; \
    expected_req.tgt_id = other_node_id; \
    expected_req.qos = dut.original_req_``P.qos; \
    assert (dut.intervention_``P === expected_req) else $fatal(1, "Home intervention REQ construction mismatch"); \
  end

  initial begin
    for (int sz = 0; sz <= 6; sz++) begin
      for (int addr = 0; addr < 256; addr += (1 << sz)) begin
        for (int b = 0; b < $bits(request); b++) request[b] = 1'($urandom);
        request.address = 52'(addr);
        request.size_or_num_req = 6'(sz);
        request.multi_req = 0;
        node_id = 16'($urandom);
        dbid = 5'($urandom);
        data_id = 2'($urandom);
        for (int b = 0; b < 512; b += 32) data[b +: 32] = $urandom;
        other_request = ~request;
        other_request.address = request.address;
        other_request.size_or_num_req = request.size_or_num_req;
        other_request.multi_req = 0;
        other_node_id = ~node_id;
        other_dbid = ~dbid;
        other_data_id = ~data_id;
        other_data = ~data[127:0];
        for (int b = 0; b < 512; b += 32) home_request_bits[b +: 32] = $urandom;
        for (int b = 0; b < 1024; b += 32) home_data_bits[b +: 32] = $urandom;
        #1;
        `CHECK_HOME(w128)
        `CHECK_HOME(h128)
        `CHECK_HOME(w256)
        `CHECK_HOME(h256)
        `CHECK_HOME(w512)
        `CHECK_HOME(h512)
        `CHECK_RSP(dbid_response, 5'h06, request, node_id, dbid)
        `CHECK_RSP(write_response, 5'h04, request, node_id, dbid)
        `CHECK_RSP(other_dbid_response, 5'h06, other_request, other_node_id, other_dbid)
        `CHECK_RSP(other_write_response, 5'h04, other_request, other_node_id, other_dbid)
        `CHECK_DAT(other_read_response, 128, other_request, other_node_id, other_data_id, other_data)
        `CHECK_DAT(read_w128, 128, request, node_id, data_id, data)
        `CHECK_DAT(read_o128, 128, request, node_id, data_id, data)
        `CHECK_DAT(read_w256, 256, request, node_id, data_id, data)
        `CHECK_DAT(read_o256, 256, request, node_id, data_id, data)
        `CHECK_DAT(read_w512, 512, request, node_id, data_id, data)
        `CHECK_DAT(read_o512, 512, request, node_id, data_id, data)
        assert ({dut.read_o128.poison, dut.read_o128.data_check, dut.read_o128.rsvdc,
                 dut.read_o256.poison, dut.read_o256.data_check, dut.read_o256.rsvdc,
                 dut.read_o512.poison, dut.read_o512.data_check, dut.read_o512.rsvdc} == '0)
          else $fatal(1, "optional DAT defaults mismatch");
      end
    end
    $display("CHI stateless messages passed at 128/256/512 bits with optional fields on and off");
    $finish;
  end
`undef CHECK_RSP
`undef CHECK_DAT
`undef CHECK_HOME
endmodule
