// Checks both Homes against independent caches/RAM, snoop ordering, stalls, and reset recovery.
typedef struct packed { logic ready; } ready_t;
typedef struct packed { logic valid; CHIReqFlit bits; } req_t;
typedef struct packed { logic valid; CHIRspFlit bits; } rsp_t;
typedef struct packed { logic valid; CHIDatFlit bits; } dat_t;
typedef struct packed { logic valid; CHISnoopDispatch bits; } snp_t;
typedef struct packed { req_t requests; rsp_t requester_responses; dat_t request_data; ready_t responses; ready_t response_data; ready_t snoops; } rn_in_t;
typedef struct packed { ready_t requests; ready_t requester_responses; ready_t request_data; rsp_t responses; dat_t response_data; snp_t snoops; } rn_out_t;
typedef struct packed { rsp_t rsp; ready_t req; struct packed { ready_t request; dat_t response; } dat; } sn_in_t;
typedef struct packed { ready_t rsp; req_t req; struct packed { dat_t request; ready_t response; } dat; } sn_out_t;
typedef struct packed { rn_in_t requester; sn_in_t subordinate; } hn_in_t;
typedef struct packed { rn_out_t requester; sn_out_t subordinate; } hn_out_t;
logic clock = 0, reset = 1;
CHIHNFIdentity identity;
hn_in_t port_in;
hn_out_t port_out;
req_t issue;
dat_t write_data;
`MAINTENANCE_HOME dut (.*);
always #5 clock = ~clock;
int cycles = 0, writes = 0, reads = 0, snoops = 0;
logic [127:0] ram [0:255];
logic cached [0:3], dirty [0:3];
logic [43:0] cache_address [0:3];
logic [127:0] cache_data [0:3][0:3];
logic snp_active = 0, snp_data = 0, snp_hit = 0;
logic hold_snoops = 0;
logic stalled_snoop = 0, maintenance_active = 0;
logic [1:0] expected_snoops = 0;
CHISnoopDispatch stalled_dispatch;
int snp_target = 0, snp_packet = 0;
logic [4:0] snp_opcode;
logic [11:0] snp_txn;
int mem_phase = 0, mem_packet = 0, mem_packets = 0, mem_delay = 0;
logic [43:0] mem_address;
logic [1:0] write_error = 0;
logic [127:0] observed [0:3];
int observed_count = 0;
localparam logic [43:0] A = 44'h80000000, B = 44'h80000100, C = 44'h80000200;

always_comb begin
  identity = '0;
  identity.home_node_id = 5;
  identity.subordinate_node_id = 9;
  identity.service_base = A;
  port_in = '0;
  port_in.requester.requests = issue;
  port_in.requester.request_data = write_data;
  port_in.requester.responses.ready = cycles % 5 == 0;
  port_in.requester.response_data.ready = cycles % 3 != 0;
  port_in.requester.snoops.ready = !hold_snoops && !snp_active && cycles % 3 != 0;
  if (snp_active) begin
    if (snp_data) begin
      port_in.requester.request_data.valid = 1;
      port_in.requester.request_data.bits = '0;
      port_in.requester.request_data.bits.opcode = 1;
      port_in.requester.request_data.bits.src_id = 7'(snp_target);
      port_in.requester.request_data.bits.tgt_id = 5;
      port_in.requester.request_data.bits.txn_id = snp_txn;
      port_in.requester.request_data.bits.data_id = 2'(snp_packet);
      port_in.requester.request_data.bits.byte_enable = '1;
      port_in.requester.request_data.bits.data = cache_data[snp_target][snp_packet];
      port_in.requester.request_data.bits.resp = snp_opcode == 8 ? 3'b101 : 3'b100;
    end else begin
      port_in.requester.requester_responses.valid = 1;
      port_in.requester.requester_responses.bits.opcode = 1;
      port_in.requester.requester_responses.bits.src_id = 7'(snp_target);
      port_in.requester.requester_responses.bits.tgt_id = 5;
      port_in.requester.requester_responses.bits.txn_id = snp_txn;
      port_in.requester.requester_responses.bits.resp = snp_hit && snp_opcode == 8 ? 1 : 0;
    end
  end
  port_in.subordinate.req.ready = mem_phase == 0 && cycles % 3 != 0;
  if ((mem_phase == 1 || mem_phase == 3) && mem_delay == 0) begin
    port_in.subordinate.rsp.valid = 1;
    port_in.subordinate.rsp.bits.opcode = mem_phase == 1 ? 6 : 4;
    port_in.subordinate.rsp.bits.src_id = 9;
    port_in.subordinate.rsp.bits.tgt_id = 5;
    port_in.subordinate.rsp.bits.dbid_or_group_id = 12'h55;
    port_in.subordinate.rsp.bits.resp_err = mem_phase == 3 ? write_error : 0;
  end
  port_in.subordinate.dat.request.ready = mem_phase == 2 && cycles % 3 != 0;
  if (mem_phase == 4) begin
    port_in.subordinate.dat.response.valid = 1;
    port_in.subordinate.dat.response.bits.opcode = 4;
    port_in.subordinate.dat.response.bits.src_id = 9;
    port_in.subordinate.dat.response.bits.tgt_id = 5;
    port_in.subordinate.dat.response.bits.data_id = 2'(mem_packet);
    port_in.subordinate.dat.response.bits.byte_enable = '1;
    port_in.subordinate.dat.response.bits.data = ram[int'(mem_address[11:4]) + mem_packet];
  end
end

always @(posedge clock) begin
  if (reset) begin
    stalled_snoop <= 0;
    maintenance_active <= 0;
    expected_snoops <= 0;
  end else begin
    if (stalled_snoop)
      assert(port_out.requester.snoops.valid && port_out.requester.snoops.bits == stalled_dispatch)
        else $fatal(1, "snoop dispatch changed under backpressure");
    stalled_snoop <= port_out.requester.snoops.valid && !port_in.requester.snoops.ready;
    stalled_dispatch <= port_out.requester.snoops.bits;
    if (snp_active)
      assert(!port_out.requester.snoops.valid) else $fatal(1, "Home issued another snoop before retiring its responder");
    if (issue.valid && port_out.requester.requests.ready) begin
      maintenance_active <= issue.bits.opcode inside {7'd8, 7'd9, 7'd10};
      expected_snoops <= {issue.bits.src_id != 7'd3 || issue.bits.excl_snoop_me_cah,
                         issue.bits.src_id != 7'd2 || issue.bits.excl_snoop_me_cah};
    end
    if (maintenance_active && port_out.requester.snoops.valid && port_in.requester.snoops.ready) begin
      assert((|expected_snoops) && port_out.requester.snoops.bits.target_id == (expected_snoops[0] ? 7'd2 : 7'd3))
        else $fatal(1, "Home skipped, repeated, or reordered a snoop target");
      expected_snoops <= expected_snoops & (port_out.requester.snoops.bits.target_id == 7'd2 ? 2'b10 : 2'b01);
    end
    if (maintenance_active && port_out.requester.responses.valid && port_in.requester.responses.ready) begin
      assert(expected_snoops == 0) else $fatal(1, "Home completed with pending snoop targets");
      maintenance_active <= 0;
    end
  end
end

always @(posedge clock) if (!reset) begin
  cycles <= cycles + 1;
  assert(cycles < 10000) else $fatal(1, "maintenance timeout");
  if (port_out.requester.snoops.valid && port_in.requester.snoops.ready) begin
    assert(port_out.requester.snoops.bits.target_id == 2 || port_out.requester.snoops.bits.target_id == 3) else $fatal(1, "unknown snoop target");
    snoops <= snoops + 1;
    snp_active <= 1;
    snp_target <= int'(port_out.requester.snoops.bits.target_id);
    snp_packet <= 0;
    snp_opcode <= port_out.requester.snoops.bits.flit.opcode;
    snp_txn <= port_out.requester.snoops.bits.flit.txn_id;
    snp_hit <= cached[port_out.requester.snoops.bits.target_id[1:0]] && (cache_address[port_out.requester.snoops.bits.target_id[1:0]][43:6] == port_out.requester.snoops.bits.flit.address[40:3]);
    snp_data <= cached[port_out.requester.snoops.bits.target_id[1:0]] && dirty[port_out.requester.snoops.bits.target_id[1:0]] &&
      (cache_address[port_out.requester.snoops.bits.target_id[1:0]][43:6] == port_out.requester.snoops.bits.flit.address[40:3]) && port_out.requester.snoops.bits.flit.opcode != 10;
  end
  if (snp_active && ((snp_data && port_in.requester.request_data.valid && port_out.requester.request_data.ready) ||
                    (!snp_data && port_in.requester.requester_responses.valid && port_out.requester.requester_responses.ready))) begin
    if (!snp_data || snp_packet == 3) begin
      snp_active <= 0;
      if (snp_hit) begin
        dirty[snp_target] <= 0;
        if (snp_opcode != 8) cached[snp_target] <= 0;
      end
    end else snp_packet <= snp_packet + 1;
  end
  if (mem_delay > 0) mem_delay <= mem_delay - 1;
  if (port_out.subordinate.req.valid && port_in.subordinate.req.ready) begin
    mem_address <= port_out.subordinate.req.bits.address;
    mem_packets <= port_out.subordinate.req.bits.size_or_num_req == 6 ? 4 : 1;
    mem_packet <= 0;
    if (port_out.subordinate.req.bits.opcode == 4) begin
      reads <= reads + 1;
      mem_phase <= 4;
    end else begin
      assert(port_out.subordinate.req.bits.opcode == 7'h1d) else $fatal(1, "unexpected memory operation");
      writes <= writes + 1;
      mem_phase <= 1;
      mem_delay <= 3;
    end
  end
  if (port_in.subordinate.rsp.valid && port_out.subordinate.rsp.ready) mem_phase <= mem_phase == 1 ? 2 : 0;
  if (port_out.subordinate.dat.request.valid && port_in.subordinate.dat.request.ready) begin
    assert(port_out.subordinate.dat.request.bits.txn_id == 12'h55) else $fatal(1, "write DBID mismatch");
    for (int b = 0; b < 16; b++) if (port_out.subordinate.dat.request.bits.byte_enable[b] && write_error == 0)
      ram[int'(mem_address[11:4]) + mem_packet][b*8+:8] <= port_out.subordinate.dat.request.bits.data[b*8+:8];
    if (mem_packet == mem_packets - 1) begin mem_phase <= 3; mem_delay <= 5; end
    else mem_packet <= mem_packet + 1;
  end
  if (port_in.subordinate.dat.response.valid && port_out.subordinate.dat.response.ready) begin
    if (mem_packet == mem_packets - 1) mem_phase <= 0;
    else mem_packet <= mem_packet + 1;
  end
  if (port_out.requester.response_data.valid && port_in.requester.response_data.ready) begin
    observed[port_out.requester.response_data.bits.data_id] <= port_out.requester.response_data.bits.data;
    observed_count <= observed_count + 1;
  end
end

task automatic tick;
  @(posedge clock); #1;
endtask
task automatic send(input int source, input logic [6:0] opcode, input logic [43:0] address, input bit snoop_me);
  issue = '0;
  issue.bits.src_id = 7'(source); issue.bits.tgt_id = 5; issue.bits.txn_id = 12'h31;
  issue.bits.return_nid_or_stash_nid_or_data_target = 7'(source);
  issue.bits.return_txn_id_or_stash_lpid = 12'h31;
  issue.bits.opcode = opcode; issue.bits.address = address; issue.bits.size_or_num_req = 6;
  issue.bits.excl_snoop_me_cah = snoop_me;
  issue.valid = 1;
  #1; while (!port_out.requester.requests.ready) tick();
  tick(); issue = '0;
endtask
task automatic finish(input logic [1:0] error_code);
  #1; while (!(port_out.requester.responses.valid && port_in.requester.responses.ready)) tick();
  assert(port_out.requester.responses.bits.opcode == 4 && port_out.requester.responses.bits.txn_id == 12'h31 && port_out.requester.responses.bits.resp_err == error_code)
    else $fatal(1, "incorrect maintenance completion");
  assert(!snp_active && mem_phase == 0) else $fatal(1, "maintenance completed before snoops or memory");
  tick();
endtask
task automatic install(input int target, input logic [43:0] address, input bit is_dirty, input logic [7:0] value);
  cached[target] = 1; dirty[target] = is_dirty; cache_address[target] = address;
  for (int p = 0; p < 4; p++) cache_data[target][p] = {16{value}};
endtask
task automatic check_ram(input logic [43:0] address, input logic [7:0] value);
  for (int p = 0; p < 4; p++) assert(ram[int'(address[11:4])+p] == {16{value}}) else $fatal(1, "backing memory mismatch");
endtask
task automatic read_line(input logic [43:0] address, input logic [7:0] value);
  observed_count = 0;
  send(1, 4, address, 0);
  while (observed_count < 4) tick();
  for (int p = 0; p < 4; p++) assert(observed[p] == {16{value}}) else $fatal(1, "cached read mismatch");
endtask
task automatic write_line(input logic [43:0] address, input logic [7:0] value);
  send(1, 7'h1d, address, 0);
  while (!(port_out.requester.responses.valid && port_in.requester.responses.ready)) tick();
  assert(port_out.requester.responses.bits.opcode == 6) else $fatal(1, "missing requester DBID");
  tick();
  for (int p = 0; p < 4; p++) begin
    write_data = '0; write_data.valid = 1; write_data.bits.opcode = 3;
    write_data.bits.src_id = 1; write_data.bits.tgt_id = 5;
    write_data.bits.data_id = 2'(p); write_data.bits.byte_enable = '1; write_data.bits.data = {16{value}};
    #1; while (!port_out.requester.request_data.ready) tick();
    tick(); write_data = '0;
  end
  finish(0);
endtask

initial begin
  issue = '0; write_data = '0;
  for (int i = 0; i < 256; i++) ram[i] = {16{8'h11}};
  for (int i = 0; i < 4; i++) begin cached[i] = 0; dirty[i] = 0; cache_address[i] = 0; end
  repeat (3) tick(); reset = 0; tick();
  // Abort both an unaccepted dispatch and one with a remembered responder and
  // another target pending. Neither may leak into the next transaction.
  for (int accept_first = 0; accept_first < 2; accept_first++) begin
    hold_snoops = 1;
    send(1, 8, A, 0);
    while (!port_out.requester.snoops.valid) tick();
    repeat (3) tick();
    assert(port_out.requester.snoops.bits.target_id == 7'd2) else $fatal(1, "incorrect first snoop target");
    if (accept_first != 0) begin
      hold_snoops = 0;
      #1; while (!port_in.requester.snoops.ready) tick();
      tick();
      assert(snp_active) else $fatal(1, "reset test did not accept its first snoop");
    end
    reset = 1; tick();
    snp_active = 0; snp_data = 0;
    hold_snoops = 0; reset = 0; tick();
    assert(port_out.requester.requests.ready && !port_out.requester.snoops.valid)
      else $fatal(1, "Home did not clear snoop scheduling on reset");
  end
  // Requester dirty copy participates only with SnpMe. Clean sharers stay clean.
  install(3, A, 1, 8'h22); install(2, A, 0, 8'h22);
  send(3, 8, A, 1); finish(0); check_ram(A, 8'h22);
  assert(cached[2] && cached[3] && !dirty[3]) else $fatal(1, "clean changed cache state incorrectly");
  // SnpMe=0 relies on the caller's already-clean copy; remote dirty data still wins.
  install(2, A, 1, 8'h33); install(3, A, 0, 8'h33);
  send(3, 8, A, 0); finish(0); check_ram(A, 8'h33);
  // Flush preserves dirty data, then invalidates every copy, even on an LLC miss.
  install(2, A, 0, 8'h44); install(3, A, 1, 8'h44);
  send(1, 9, A, 0); finish(0); check_ram(A, 8'h44);
  assert(!cached[2] && !cached[3]) else $fatal(1, "flush left a cache copy");
  // Discard must not write dirty bytes back over a noncoherent agent's RAM value.
  install(3, A, 1, 8'h55);
  begin
    int before_writes; before_writes = writes;
    send(3, 10, A, 1); finish(0);
    assert(writes == before_writes && !cached[3]) else $fatal(1, "discard performed a write");
    check_ram(A, 8'h44);
  end
  // No allocation, refill, or unrelated eviction on a maintenance miss.
  read_line(A, 8'h44); read_line(B, 8'h11);
  begin
    int before_reads, before_writes;
    before_reads = reads; before_writes = writes;
    send(1, 9, C, 0); finish(0);
    assert(reads == before_reads && writes == before_writes) else $fatal(1, "maintenance miss generated memory traffic");
    read_line(A, 8'h44); read_line(B, 8'h11);
    if (INCLUSIVE) assert(reads == before_reads) else $fatal(1, "maintenance evicted unrelated data");
  end
  // LLC-only dirty data is written on clean and remains readable without refill.
  write_line(A, 8'h66);
  if (INCLUSIVE) check_ram(A, 8'h44);
  send(1, 8, A, 0); finish(0); check_ram(A, 8'h66); read_line(A, 8'h66);
  write_line(A, 8'h77);
  send(1, 9, A, 0); finish(0); check_ram(A, 8'h77);
  begin
    int before_reads; before_reads = reads;
    read_line(A, 8'h77);
    assert(reads == before_reads + 1) else $fatal(1, "flush retained the LLC copy");
  end
  if (INCLUSIVE) begin
    write_line(A, 8'h88);
    send(1, 10, A, 0); finish(0); check_ram(A, 8'h77); read_line(A, 8'h77);
  end
  // A downstream write failure is surfaced rather than reported as success.
  install(3, A, 1, 8'h99); write_error = 2;
  send(1, 8, A, 0); finish(2); check_ram(A, 8'h77);
  $display("CHI maintenance Home contract passed (inclusive=%0d)", INCLUSIVE);
  $finish;
end
`undef MAINTENANCE_HOME
