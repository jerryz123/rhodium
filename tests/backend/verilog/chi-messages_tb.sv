// Checks independent repeated message calls, response routing, byte masks, and optional defaults at all DAT widths.
module chi_messages_tb;
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
        #1;
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
endmodule
