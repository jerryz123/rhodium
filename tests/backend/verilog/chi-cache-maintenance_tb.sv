// Exercises dataless request identity, retry/credit ordering, errors, and completion backpressure.
module chi_cache_maintenance_tb #(parameter bit WRONG_SOURCE = 0, BAD_ADDRESS = 0);
  logic clock = 0, reset = 1;
  logic [6:0] node_id = 3;
  logic [11:0] txn_id = 12'h42;
  struct packed { logic valid; CHICacheMaintenanceCommand bits; } command_in;
  struct packed { logic ready; } command_out;
  struct packed { logic ready; } completion_in;
  struct packed { logic valid; CHICacheMaintenanceResult bits; } completion_out;
  struct packed { logic ready; } requests_in;
  struct packed { logic valid; CHIReqFlit bits; } requests_out;
  struct packed { logic valid; CHIRspFlit bits; } responses_in;
  struct packed { logic ready; } responses_out;
  CHIReqFlit held_request;
  CHICacheMaintenanceResult held_result;
  CHICacheMaintenance dut (.*);
  always #5 clock = ~clock;
  task automatic tick;
    @(posedge clock); #1;
  endtask
  task automatic response(input logic [4:0] opcode, input logic [1:0] error_code);
    responses_in = '0;
    responses_in.valid = 1;
    responses_in.bits.opcode = opcode;
    responses_in.bits.src_id = WRONG_SOURCE ? 6 : 5;
    responses_in.bits.tgt_id = 3;
    responses_in.bits.txn_id = opcode == 7 ? 12'h999 : 12'h42;
    responses_in.bits.pcrd_type = 4'hb;
    responses_in.bits.resp_err = error_code;
    #1;
    assert(responses_out.ready) else $fatal(1, "maintenance response rejected");
    tick(); responses_in = '0;
  endtask
  task automatic attempt(input bit retry_attempt);
    #1;
    assert(requests_out.valid) else $fatal(1, "missing maintenance request");
    assert(requests_out.bits.opcode == 7'(8 + operation) && requests_out.bits.address == 44'h80000040 &&
           requests_out.bits.excl_snoop_me_cah && !requests_out.bits.exp_comp_ack && requests_out.bits.size_or_num_req == 6 &&
           requests_out.bits.src_id == 3 && requests_out.bits.tgt_id == 5 && requests_out.bits.txn_id == 12'h42 &&
           requests_out.bits.allow_retry == !retry_attempt && requests_out.bits.pcrd_type == (retry_attempt ? 11 : 0))
      else $fatal(1, "incorrect maintenance request fields");
    held_request = requests_out.bits;
    repeat (3) begin
      tick();
      assert(requests_out.valid && requests_out.bits == held_request) else $fatal(1, "stalled request changed");
    end
    requests_in.ready = 1; tick(); requests_in.ready = 0;
  endtask
  int operation;
  initial begin
    command_in = '0; completion_in = '0; requests_in = '0; responses_in = '0;
    repeat (3) tick(); reset = 0; tick();
    for (operation = 0; operation < 3; operation++) begin
      node_id = 3; txn_id = 12'h42;
      command_in.valid = 1;
      command_in.bits.address = BAD_ADDRESS ? 44'h80000041 : 44'h80000040;
      command_in.bits.opcode = 7'(8 + operation);
      command_in.bits.snoop_me = 1;
      command_in.bits.home_id = 5;
      command_in.bits.context_0 = 8'(operation + 17);
      #1; assert(command_out.ready) else $fatal(1, "idle command rejected");
      tick(); command_in = '0;
      node_id = 7; txn_id = 12'h999; // Active identity must be captured, not sampled live.
      attempt(0);
      if (operation == 0) begin response(7, 0); response(3, 0); end
      else if (operation == 1) begin
        response(3, 0); repeat (3) begin tick(); assert(!requests_out.valid); end
        response(7, 0);
      end
      if (operation < 2) attempt(1);
      response(4, operation == 2 ? 2 : 0);
      held_result = completion_out.bits;
      repeat (4) begin
        assert(completion_out.valid && completion_out.bits == held_result && !command_out.ready && !requests_out.valid)
          else $fatal(1, "completion lifetime broken");
        tick();
      end
      assert(held_result.context_0 == 8'(operation + 17) && held_result.error == (operation == 2 ? 2 : 0))
        else $fatal(1, "completion lost context or error");
      completion_in.ready = 1; tick(); completion_in.ready = 0;
      assert(command_out.ready && !completion_out.valid) else $fatal(1, "transaction failed to retire");
    end
    $display("CHI maintenance requester retry and completion passed");
    $finish;
  end
  initial begin #10000; $fatal(1, "requester timeout"); end
endmodule

module chi_cache_maintenance_wrong_source_tb;
  chi_cache_maintenance_tb #(.WRONG_SOURCE(1)) test();
endmodule

module chi_cache_maintenance_bad_address_tb;
  chi_cache_maintenance_tb #(.BAD_ADDRESS(1)) test();
endmodule
