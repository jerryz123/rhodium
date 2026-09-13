// Checks dirty-code visibility, fence synchronization, and snapshot retention under outer replacement.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_icache_coherence_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; logic [63:0] bits; } lookup_t;
  typedef struct packed { logic valid; RV5StageDataReq bits; } host_req_t;
  typedef struct packed { logic valid; RV5StageDataResp bits; } host_resp_t;
  typedef struct packed { host_req_t request; } host_in_t;
  typedef struct packed { ready_t request; logic request_fault, request_access_fault; host_resp_t response; logic drained, reservation_valid; } host_out_t;
  typedef struct packed { logic valid; logic [63:0] bits; } fetch_req_t;
  typedef struct packed { logic [31:0] word; logic page_fault, access_fault; } instruction_t;
  typedef struct packed { instruction_t response; logic replay; } result_t;
  typedef struct packed { logic valid; result_t bits; } fetch_resp_t;
  typedef struct packed { logic flush, invalidate_all, s1_kill; fetch_req_t request; } fetch_in_t;
  typedef struct packed { fetch_resp_t response; } fetch_out_t;
  logic clock = 0, reset = 1;
  host_in_t host_in;
  host_out_t host_out;
  fetch_in_t fetch_in;
  fetch_out_t fetch_out;
  lookup_t virtual_lookup_in;
  ready_t virtual_lookup_out;
  logic instruction_request, instruction_fence;
  int requests = 0, completions = 0;
  logic [63:0] data_result;
  RV5StageInstructionCoherence dut(.*);
  always #5 clock = ~clock;
  task automatic tick;
    if (!reset) begin
      if (instruction_request) requests++;
      if (host_out.response.valid) begin
        assert (!host_out.response.bits.access_fault) else $fatal(1, "data access fault");
        completions++;
        data_result = host_out.response.bits.data;
      end
    end
    @(posedge clock); #1;
  endtask
  task automatic access(input bit write, input int address, input logic [63:0] value = 0);
    int previous;
    previous = completions;
    host_in.request.bits = '0;
    host_in.request.bits.address = 64'(address);
    host_in.request.bits.access = write ? 2 : 1;
    host_in.request.bits.width = 3;
    host_in.request.bits.data = value;
    host_in.request.bits.destination = write ? 0 : 1;
    host_in.request.valid = 1;
    #1;
    for (int c = 0; !host_out.request.ready && c < 10000; c++) tick();
    assert (host_out.request.ready) else $fatal(1, "data admission timeout");
    tick();
    host_in.request.valid = 0;
    for (int c = 0; completions == previous && c < 10000; c++) tick();
    assert (completions == previous + 1) else $fatal(1, "data completion timeout");
  endtask
  task automatic fetch_word(input int address, input logic [31:0] expected);
    bit done;
    done=0;
    for(int attempt=0; !done && attempt<1000; attempt++) begin
      fetch_in.request.bits=64'(address);
      virtual_lookup_in='{valid:1, bits:64'(address)};
      #1;
      for(int c=0; !virtual_lookup_out.ready && c<10000; c++) tick();
      assert(virtual_lookup_out.ready) else $fatal(1,"virtual fetch admission timeout");
      tick();
      virtual_lookup_in.valid=0;
      fetch_in.request.valid=1;
      tick();
      fetch_in.request.valid=0;
      done=fetch_out.response.valid && !fetch_out.response.bits.replay;
    end
    assert (fetch_out.response.valid && !fetch_out.response.bits.response.page_fault &&
            !fetch_out.response.bits.response.access_fault && fetch_out.response.bits.response.word == expected)
      else $fatal(1, "instruction %h, expected %h", fetch_out.response.bits.response.word, expected);
    tick();
  endtask
  task automatic fence_i;
    for (int c = 0; !host_out.drained && c < 10000; c++) tick();
    assert (host_out.drained) else $fatal(1, "fence data drain timeout");
    fetch_in.invalidate_all = 1;
    tick();
    fetch_in.invalidate_all = 0;
  endtask
  initial begin
    host_in = '0; fetch_in = '0; virtual_lookup_in = '0;
    repeat (2) tick(); reset = 0;
    // Backing RAM still contains its old value: the instruction read must intervene on L1D.
    access(1, 'h4000, 64'h00000013_00100293);
    fetch_word('h4000, 32'h00100293);
    access(1, 'h4000, 64'h00000013_00200293);
    // This implementation deliberately retains the old snapshot until synchronization.
    fetch_word('h4000, 32'h00100293);
    fence_i();
    fetch_word('h4000, 32'h00200293);
    // Repeated unrelated data allocations replace this line in the tiny inclusive LLC.
    for (int i = 0; i < 32; i++) access(1, 'h5000 + 128*i, 64'(i));
    begin
      int before_fetch;
      before_fetch = requests;
      fetch_word('h4000, 32'h00200293);
      assert (requests == before_fetch) else $fatal(1, "outer replacement evicted instruction snapshot");
    end
    fence_i();
    fetch_word('h4000, 32'h00200293);
    access(0, 'h4000);
    assert (data_result == 64'h00000013_00200293) else $fatal(1, "dirty intervention lost authoritative data");
    $display("Instruction snapshots: dirty owner, fence visibility, and outer replacement passed");
    $finish;
  end
endmodule
