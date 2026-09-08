// Checks complete requester packets and independent repeated construction at all physical widths/options.
module rv5stage_chi_requests_tb;
  logic [63:0] address;
  logic [6:0] opcode;
  logic [2:0] size;
  logic [11:0] txn;
  logic [5:0] flags;
  logic [3:0] pcrd;
  RV5StageCHIRequestFixture dut(.address(address), .opcode(opcode), .size(size), .txn(txn), .flags(flags), .pcrd(pcrd),
    .w128(), .other_w128(),
    .o128(), .other_o128(),
    .w256(), .other_w256(),
    .o256(), .other_o256(),
    .w512(), .other_w512(),
    .o512(), .other_o512());

`define CHECK_REQUEST(PORT, ADDRESS, TXN, FLAGS) \
  begin \
    type(dut.PORT) expected_packet; \
    logic [5:0] expected_flags; \
    expected_flags = FLAGS; \
    expected_packet = '0; \
    expected_packet.exp_comp_ack = expected_flags[0]; \
    expected_packet.snp_attr_or_do_dwt = !(opcode == 7'h1c || opcode == 7'h1d); \
    expected_packet.mem_attr.allocate = expected_flags[1]; \
    expected_packet.mem_attr.cacheable = expected_flags[3]; \
    expected_packet.mem_attr.device = expected_flags[4]; \
    expected_packet.mem_attr.early_write_acknowledge = expected_flags[5]; \
    expected_packet.pcrd_type = pcrd; \
    expected_packet.allow_retry = expected_flags[2]; \
    expected_packet.address = 52'(ADDRESS); \
    expected_packet.size_or_num_req = {3'b0, size}; \
    expected_packet.opcode = opcode; \
    expected_packet.return_nid_or_stash_nid_or_data_target = 16'h1234; \
    expected_packet.txn_id = TXN; \
    expected_packet.src_id = 16'h1234; \
    expected_packet.tgt_id = 16'h4321; \
    assert(dut.PORT === expected_packet) else $fatal(1, "REQ construction mismatch: %s", `"PORT`"); \
  end
  initial begin
    for (int op = 0; op < 6; op++) begin
      case (op)
        0: opcode = 7'h02;
        1: opcode = 7'h07;
        2: opcode = 7'h18;
        3: opcode = 7'h04;
        4: opcode = 7'h1d;
        5: opcode = 7'h1c;
      endcase
      for (int control = 0; control < 64; control++) begin
        for (int sz = 0; sz <= 6; sz++) begin
          address = {$urandom(), $urandom()};
          txn = 12'($urandom());
          pcrd = 4'($urandom());
          flags = 6'(control);
          size = 3'(sz);
          #1;
          `CHECK_REQUEST(w128, address, txn, flags)
          `CHECK_REQUEST(other_w128, ~address, ~txn, ~flags)
          `CHECK_REQUEST(o128, address, txn, flags)
          `CHECK_REQUEST(other_o128, ~address, ~txn, ~flags)
          `CHECK_REQUEST(w256, address, txn, flags)
          `CHECK_REQUEST(other_w256, ~address, ~txn, ~flags)
          `CHECK_REQUEST(o256, address, txn, flags)
          `CHECK_REQUEST(other_o256, ~address, ~txn, ~flags)
          `CHECK_REQUEST(w512, address, txn, flags)
          `CHECK_REQUEST(other_w512, ~address, ~txn, ~flags)
          `CHECK_REQUEST(o512, address, txn, flags)
          `CHECK_REQUEST(other_o512, ~address, ~txn, ~flags)
        end
      end
    end
    $display("RV5Stage complete CHI REQ checks passed (six width/option variants, paired calls)");
    $finish;
  end
`undef CHECK_REQUEST
endmodule
