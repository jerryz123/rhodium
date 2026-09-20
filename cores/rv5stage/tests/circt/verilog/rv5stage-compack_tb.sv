// Tests line-completing packet ownership across reordered data, stalled CompAck, ROM, and reset.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_compack_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_t;
  logic clock = 0, reset = 1, command_valid = 0, instruction_rom = 0, completion_ready = 0;
  logic command_ready, d_complete, i_complete;
  ready_t dreq_in, ireq_in, dack_in, iack_in;
  ready_t ddata_out, idata_out;
  req_t dreq_out, ireq_out;
  dat_t ddata_in, idata_in;
  rsp_t dack_out, iack_out;
  EventCompAck dut (.*);
  always #5 clock = ~clock;
  import "DPI-C" function void compack_bind();
  import "DPI-C" function void compack_sample(int unsigned reset, int unsigned command, int unsigned instruction_rom,
      int unsigned dfire, int unsigned dpacket, int unsigned ifire, int unsigned ipacket,
      int unsigned dack, int unsigned ddbid, int unsigned iack, int unsigned idbid, int unsigned stalled);
  import "DPI-C" function void compack_check();
  import "DPI-C" function void compack_finish();
  always @(posedge clock) begin
    compack_sample(int'(reset), int'(command_valid && command_ready), int'(instruction_rom),
        int'(ddata_in.valid && ddata_out.ready), int'(ddata_in.bits.data_id),
        int'(idata_in.valid && idata_out.ready), int'(idata_in.bits.data_id),
        int'(dack_out.valid && dack_in.ready), int'(dack_out.bits.txn_id),
        int'(iack_out.valid && iack_in.ready), int'(iack_out.bits.txn_id),
        int'((dack_out.valid && !dack_in.ready) || (iack_out.valid && !iack_in.ready)));
    #1; compack_check();
  end
  task automatic tick;
    @(posedge clock); #2;
  endtask
  task automatic clear;
    reset = 1; command_valid = 0; completion_ready = 0;
    dreq_in = '0; ireq_in = '0; dack_in = '0; iack_in = '0; ddata_in = '0; idata_in = '0;
    tick(); reset = 0; tick();
    assert (!dack_out.valid && !iack_out.valid && !d_complete && !i_complete) else $fatal(1, "output survived reset");
  endtask
  task automatic start(input bit rom);
    instruction_rom = rom; command_valid = 1;
    #1; assert (command_ready) else $fatal(1, "idle command blocked");
    tick(); command_valid = 0;
    repeat (3) tick();
    assert (dreq_out.valid && ireq_out.valid && dreq_out.bits.opcode == 7'h2 &&
        ireq_out.bits.opcode == (rom ? 7'h4 : 7'h3)) else $fatal(1, "request opcode mismatch");
    dreq_in.ready = 1; ireq_in.ready = 1; tick(); dreq_in.ready = 0; ireq_in.ready = 0;
  endtask
  task automatic packet(input int index);
    ddata_in = '0; idata_in = '0;
    ddata_in.valid = 1;
    ddata_in.bits.opcode = 4; ddata_in.bits.tgt_id = 3; ddata_in.bits.src_id = 1;
    ddata_in.bits.home_nid_or_pbha_or_mismatched_mecid = 1;
    ddata_in.bits.dbid_or_mecid = 16'h0abc; ddata_in.bits.data_id = 2'(index);
    ddata_in.bits.data = 128'h12345678;
    idata_in = ddata_in; idata_in.bits.tgt_id = 2;
    idata_in.bits.src_id = instruction_rom ? 4 : 1;
    idata_in.bits.home_nid_or_pbha_or_mismatched_mecid = instruction_rom ? 4 : 1;
    #1;
    assert (ddata_out.ready && idata_out.ready) else $fatal(1, "packet blocked");
    tick(); ddata_in.valid = 0; idata_in.valid = 0;
  endtask
  task automatic line(input bit reverse_order);
    for (int index = 0; index < 4; ++index) begin
      assert (!dack_out.valid && !iack_out.valid) else $fatal(1, "early CompAck");
      packet(reverse_order ? 3-index : (index+1)%4);
      repeat (index%3) tick();
    end
  endtask
  task automatic acknowledge;
    rsp_t held_d, held_i;
    held_d = dack_out; held_i = iack_out;
    assert (held_d.valid && held_d.bits.opcode == 2 && held_d.bits.txn_id == 12'habc &&
        held_i.valid == !instruction_rom &&
        (instruction_rom || (held_i.bits.opcode == 2 && held_i.bits.txn_id == 12'habc))) else $fatal(1, "incorrect CompAck");
    repeat (5) begin
      tick();
      assert (dack_out == held_d && iack_out == held_i) else $fatal(1, "stalled CompAck changed");
    end
    dack_in.ready = 1; tick(); dack_in.ready = 0;
    repeat (3) tick();
    if (!instruction_rom) begin
      assert (iack_out == held_i) else $fatal(1, "other lane released CompAck");
      iack_in.ready = 1; tick(); iack_in.ready = 0;
    end
    repeat (3) begin
      tick();
      assert (d_complete && i_complete && !dack_out.valid && !iack_out.valid && !command_ready)
        else $fatal(1, "completion lifetime mismatch");
    end
    completion_ready = 1; tick(); completion_ready = 0;
  endtask
  initial begin
    compack_bind(); clear();
    // Reset a partial packet set, then two complete lines waiting on acknowledgement.
    start(0); packet(3); packet(1); clear();
    repeat (2) begin start(0); line(1); repeat (3) tick(); clear(); end
    // IDs, payloads and addresses deliberately repeat across coherent and ROM transactions.
    repeat (4) begin
      start(0); line(1); acknowledge();
      start(1); line(0); acknowledge();
      start(0); line(0); acknowledge();
    end
    tick(); compack_finish(); $finish;
  end
  initial begin #30000; $fatal(1, "CompAck trace timeout"); end
endmodule
