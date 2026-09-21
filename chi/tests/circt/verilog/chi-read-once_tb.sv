// Checks generic ReadOnce retries, packet assembly, CompAck, errors, and retained results.
// SPDX-License-Identifier: Apache-2.0
module chi_read_once_tb #(parameter bit BAD_ADDRESS = 0, DUPLICATE_DATA = 0);
  logic clock = 0, reset = 1;
  logic [6:0] node_id;
  logic [11:0] txn_id;
  struct packed { logic valid; CHIReadOnceCommand bits; } command_in;
  struct packed { logic ready; } command_out;
  struct packed { logic ready; } completion_in;
  struct packed { logic valid; CHIReadOnceResult bits; } completion_out;
  struct packed { logic ready; } requests_in;
  struct packed { logic valid; CHIReqFlit bits; } requests_out;
  struct packed { logic ready; } acknowledgements_in;
  struct packed { logic valid; CHIRspFlit bits; } acknowledgements_out;
  struct packed { logic valid; CHIRspFlit bits; } responses_in;
  struct packed { logic ready; } responses_out;
  struct packed { logic valid; CHIDatFlit bits; } response_data_in;
  struct packed { logic ready; } response_data_out;

  logic [6:0] expected_node, expected_home;
  logic [11:0] expected_txn;
  logic [43:0] expected_address;
  logic [2:0] expected_pas;
  logic expected_allocate;
  logic [3:0] expected_qos;
  logic [7:0] expected_context;
  logic [127:0] expected_packets [4];
  localparam logic [11:0] RESPONSE_DBID = 12'h155;

  CHIReadOnce dut (.*);
  always #5 clock = ~clock;

  task automatic tick;
    @(posedge clock); #1;
  endtask

  task automatic begin_read(
    input logic [6:0] requester,
    input logic [11:0] transaction,
    input logic [6:0] home,
    input logic [43:0] address,
    input logic [2:0] pas,
    input logic allocate,
    input logic [3:0] qos,
    input logic [7:0] command_context
  );
    expected_node = requester;
    expected_txn = transaction;
    expected_home = home;
    expected_address = address;
    expected_pas = pas;
    expected_allocate = allocate;
    expected_qos = qos;
    expected_context = command_context;
    node_id = requester;
    txn_id = transaction;
    command_in = '0;
    command_in.valid = 1;
    command_in.bits.address = BAD_ADDRESS ? (address | 1) : address;
    command_in.bits.home_id = home;
    command_in.bits.pas = pas;
    command_in.bits.allocate = allocate;
    command_in.bits.qos = qos;
    command_in.bits.context_0 = command_context;
    #1;
    assert(command_out.ready) else $fatal(1, "idle ReadOnce command rejected");
    tick();
    command_in = '0;
    node_id = requester + 1;
    txn_id = transaction + 1;
  endtask

  task automatic accept_request(input logic retried, input int stall_cycles);
    CHIReqFlit expected_request, held_request;
    expected_request = '0;
    expected_request.exp_comp_ack = 1;
    expected_request.snp_attr_or_do_dwt = 1;
    expected_request.mem_attr.allocate = expected_allocate;
    expected_request.mem_attr.cacheable = 1;
    expected_request.pcrd_type = retried ? 4'hb : 4'h0;
    expected_request.allow_retry = !retried;
    expected_request.pas = expected_pas;
    expected_request.address = expected_address;
    expected_request.size_or_num_req = 6;
    expected_request.opcode = 7'h03;
    expected_request.return_txn_id_or_stash_lpid = expected_txn;
    expected_request.return_nid_or_stash_nid_or_data_target = expected_node;
    expected_request.txn_id = expected_txn;
    expected_request.src_id = expected_node;
    expected_request.tgt_id = expected_home;
    expected_request.qos = expected_qos;
    #1;
    assert(requests_out.valid && requests_out.bits === expected_request)
      else $fatal(1, "complete ReadOnce request mismatch");
    held_request = requests_out.bits;
    repeat (stall_cycles) begin
      tick();
      assert(requests_out.valid && requests_out.bits === held_request)
        else $fatal(1, "stalled ReadOnce request changed");
    end
    requests_in.ready = 1;
    tick();
    requests_in.ready = 0;
  endtask

  task automatic send_response(input logic [4:0] opcode, input logic [3:0] credit_type);
    responses_in = '0;
    responses_in.valid = 1;
    responses_in.bits.opcode = opcode;
    responses_in.bits.pcrd_type = credit_type;
    responses_in.bits.txn_id = opcode == 5'h03 ? expected_txn : 0;
    responses_in.bits.src_id = expected_home;
    responses_in.bits.tgt_id = expected_node;
    #1;
    assert(responses_out.ready) else $fatal(1, "ReadOnce retry response rejected");
    tick();
    responses_in = '0;
  endtask

  task automatic send_data(
    input logic [1:0] data_id,
    input logic [127:0] data,
    input logic [1:0] error,
    input logic [1:0] poison
  );
    response_data_in = '0;
    response_data_in.valid = 1;
    response_data_in.bits.poison = poison;
    response_data_in.bits.data = data;
    response_data_in.bits.byte_enable = '1;
    response_data_in.bits.data_id = data_id;
    response_data_in.bits.dbid_or_mecid = {4'h0, RESPONSE_DBID};
    response_data_in.bits.resp_err = error;
    response_data_in.bits.opcode = 4'h4;
    response_data_in.bits.home_nid_or_pbha_or_mismatched_mecid = expected_home;
    response_data_in.bits.txn_id = expected_txn;
    response_data_in.bits.src_id = expected_home;
    response_data_in.bits.tgt_id = expected_node;
    response_data_in.bits.qos = expected_qos;
    #1;
    assert(response_data_out.ready) else $fatal(1, "ReadOnce data rejected");
    tick();
    response_data_in = '0;
  endtask

  task automatic finish_read(input logic [1:0] error, input logic poisoned);
    CHIRspFlit expected_ack, held_ack;
    CHIReadOnceResult held_result;
    expected_ack = '0;
    expected_ack.opcode = 5'h02;
    expected_ack.txn_id = RESPONSE_DBID;
    expected_ack.src_id = expected_node;
    expected_ack.tgt_id = expected_home;
    expected_ack.qos = expected_qos;
    #1;
    assert(acknowledgements_out.valid && acknowledgements_out.bits === expected_ack)
      else $fatal(1, "ReadOnce CompAck mismatch");
    assert(!completion_out.valid) else $fatal(1, "ReadOnce completed before CompAck");
    held_ack = acknowledgements_out.bits;
    repeat (3) begin
      tick();
      assert(acknowledgements_out.valid && acknowledgements_out.bits === held_ack)
        else $fatal(1, "stalled ReadOnce CompAck changed");
    end
    acknowledgements_in.ready = 1;
    tick();
    acknowledgements_in.ready = 0;
    #1;
    assert(completion_out.valid) else $fatal(1, "ReadOnce completion missing");
    assert(completion_out.bits.address == expected_address &&
           completion_out.bits.context_0 == expected_context &&
           completion_out.bits.error == error &&
           completion_out.bits.poisoned == poisoned)
      else $fatal(1, "ReadOnce completion metadata mismatch");
    for (int packet = 0; packet < 4; packet++) begin
      assert(completion_out.bits.line[packet * 128 +: 128] == expected_packets[packet])
        else $fatal(1, "ReadOnce line packet %0d mismatch", packet);
    end
    held_result = completion_out.bits;
    repeat (3) begin
      tick();
      assert(completion_out.valid && completion_out.bits === held_result && !command_out.ready)
        else $fatal(1, "stalled ReadOnce completion changed");
    end
    completion_in.ready = 1;
    tick();
    completion_in.ready = 0;
    assert(command_out.ready && !completion_out.valid)
      else $fatal(1, "ReadOnce transaction did not retire");
  endtask

  initial begin
    command_in = '0;
    completion_in = '0;
    requests_in = '0;
    acknowledgements_in = '0;
    responses_in = '0;
    response_data_in = '0;
    node_id = '0;
    txn_id = '0;
    repeat (3) tick();
    reset = 0;
    tick();

    begin_read(7'h03, 12'h042, 7'h05, 44'h080000040,
               3'h3, 1'b0, 4'ha, 8'h11);
    if (BAD_ADDRESS) begin
      repeat (5) tick();
      $fatal(1, "misaligned ReadOnce command was not rejected");
    end
    accept_request(1'b0, 3);
    send_response(5'h07, 4'hb);
    send_response(5'h03, 4'hb);
    accept_request(1'b1, 2);
    expected_packets[0] = 128'h100;
    expected_packets[1] = 128'h101;
    expected_packets[2] = 128'h102;
    expected_packets[3] = 128'h103;
    send_data(2, expected_packets[2], 0, 0);
    if (DUPLICATE_DATA) begin
      send_data(2, expected_packets[2], 0, 0);
      repeat (5) tick();
      $fatal(1, "duplicate ReadOnce DataID was not rejected");
    end
    send_data(0, expected_packets[0], 0, 0);
    send_data(3, expected_packets[3], 2, 2'b01);
    send_data(1, expected_packets[1], 0, 0);
    finish_read(2, 1'b1);

    begin_read(7'h04, 12'h043, 7'h06, 44'h080000080,
               3'h1, 1'b1, 4'h3, 8'h22);
    accept_request(1'b0, 0);
    for (int packet = 0; packet < 4; packet++) begin
      expected_packets[packet] = 128'h200 + 128'(packet);
      send_data(packet[1:0], expected_packets[packet], 0, 0);
    end
    finish_read(0, 1'b0);

    $display("CHI ReadOnce retry, packet assembly, and CompAck passed");
    $finish;
  end

  initial begin #20000; $fatal(1, "ReadOnce requester timeout"); end
endmodule

module chi_read_once_bad_address_tb;
  chi_read_once_tb #(.BAD_ADDRESS(1)) test();
endmodule

module chi_read_once_duplicate_data_tb;
  chi_read_once_tb #(.DUPLICATE_DATA(1)) test();
endmodule
