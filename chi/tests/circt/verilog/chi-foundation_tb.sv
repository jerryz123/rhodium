// Checks CHI flits, service matching, and singleton/multiregion address maps including NodeID-zero hits.
// SPDX-License-Identifier: Apache-2.0
module chi_foundation_tb;
  logic [43:0] map_address;
  logic [7:0] home_lookup, subordinate_lookup;
  logic [7:0] single_home_lookup, single_subordinate_lookup;
  logic [136:0] req;
  logic [70:0] rsp;
  logic [93:0] snp;
  logic [239:0] dat;
  logic [6:0] req_opcode;
  logic [4:0] rsp_opcode;
  logic [4:0] snp_opcode;
  logic [3:0] dat_opcode;
  logic [2:0] size;
  logic [1:0] data_id;
  logic [136:0] req_out;
  logic [70:0] rsp_out;
  logic [93:0] snp_out;
  logic [239:0] dat_out;
  logic req_opcode_valid;
  logic rsp_opcode_valid;
  logic snp_opcode_valid;
  logic dat_opcode_valid;
  logic req_atomic;
  logic req_read_no_snp;
  logic req_write_no_snp;
  logic rsp_allocates_dbid;
  logic dat_write_data;
  logic dat_response_data;
  logic [2:0] req_size;
  logic [5:0] req_num_req;
  logic size_valid;
  logic [1:0] data_beats_minus_one;
  logic data_id_valid;
  logic match_byte, match_line, match_middle, match_large;

  CHIFoundationFixture dut (.*);

  initial begin
    for (int addr = 0; addr < 'h3000; addr++) begin
      bit hit;
      int target;
      hit = (addr >= 'h1000 && addr < 'h1100) ||
            (addr >= 'h2000 && addr < 'h2040) ||
            (addr >= 'h2080 && addr < 'h20c0);
      target = (addr >= 'h2000 && addr < 'h2040) ? 3 :
               (addr >= 'h2080 && addr < 'h20c0) ? 6 : 0;
      map_address = 44'(addr);
      #1;
      assert(home_lookup == {hit, 7'(target)} && subordinate_lookup == {hit, 7'(target)})
        else $fatal(1, "Home/subordinate map decode mismatch at %h", addr);
      assert(single_home_lookup == {(addr >= 'h1000 && addr < 'h1100), 7'b0} &&
             single_subordinate_lookup == single_home_lookup)
        else $fatal(1, "singleton map decode mismatch at %h", addr);
    end
    // Every opcode and encoded Size, including the reserved Size encoding.
    for (int op = 0; op < 128; op++) begin
      for (int sz = 0; sz < 8; sz++) begin
        req_opcode = 7'(op);
        size = 3'(sz);
        #1;
        assert(match_byte == (op == 4 && sz == 0) &&
               match_line == (op == 4 && sz <= 6) &&
               match_middle == (op == 4 && sz >= 2 && sz <= 4) &&
               match_large == (op == 4 && sz >= 5 && sz <= 6))
          else $fatal(1, "hardware request support mismatch");
      end
    end
    req = 137'h123456789abcdef;
    rsp = 71'h123456789ab;
    snp = 94'h123456789abcdef;
    dat = 240'h123456789abcdef;
    req_opcode = 7'h04;
    rsp_opcode = 5'h05;
    snp_opcode = 5'h17;
    dat_opcode = 4'h03;
    size = 3'h6;
    data_id = 2'h3;
    #1;

    assert (req_out == req && rsp_out == rsp && snp_out == snp && dat_out == dat)
      else $fatal(1, "flit pass-through changed packed CHI bits");
    assert (req_opcode_valid && rsp_opcode_valid && snp_opcode_valid && dat_opcode_valid)
      else $fatal(1, "a defined CHI opcode was rejected");
    assert (req_read_no_snp && !req_write_no_snp && !req_atomic)
      else $fatal(1, "ReadNoSnp classification failed");
    assert (rsp_allocates_dbid && dat_write_data && !dat_response_data)
      else $fatal(1, "response or data classification failed");
    assert (size_valid && data_beats_minus_one == 2'h3 && data_id_valid)
      else $fatal(1, "128-bit DAT packetization failed");
    assert (req_size == req_num_req[2:0])
      else $fatal(1, "REQ Size and NumReq views disagree on shared storage");

    req_opcode = 7'h06;
    rsp_opcode = 5'h0f;
    snp_opcode = 5'h0e;
    dat_opcode = 4'h08;
    size = 3'h7;
    #1;
    assert (!req_opcode_valid && !rsp_opcode_valid && !snp_opcode_valid && !dat_opcode_valid)
      else $fatal(1, "a reserved CHI opcode was accepted");
    assert (!size_valid)
      else $fatal(1, "reserved Size encoding was accepted");

    req_opcode = 7'h28;
    rsp_opcode = 5'h04;
    dat_opcode = 4'h04;
    size = 3'h5;
    #1;
    assert (req_atomic && !rsp_allocates_dbid)
      else $fatal(1, "atomic or response classification failed");
    assert (!dat_write_data && dat_response_data && data_beats_minus_one == 2'h1)
      else $fatal(1, "response-data or 32-byte packetization failed");

    $display("CHI foundation simulation passed");
    $finish;
  end
endmodule
