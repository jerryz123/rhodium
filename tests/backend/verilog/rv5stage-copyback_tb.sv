// Checks copyback retry, grant/snoop ordering, all DAT widths, and stalled retirement.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_copyback_tb;
  logic clock = 0, reset = 1;
  logic [2:0] enabled = 0;
  logic command_valid = 0, request_ready = 0, response_valid = 0;
  logic [4:0] response_opcode = 0;
  logic [3:0] response_pcrd = 0;
  logic snoop_pending = 0;
  logic [2:0] state = 4;
  logic data_ready = 0, completion_ready = 0;
  logic [511:0] line;
  CopyBackObservation [2:0] observed;
  RV5StageCopyBackFixture dut(.*);
  always #5 clock = ~clock;
  task automatic tick;
    @(posedge clock); #1;
  endtask
  task automatic response(input int lane, input logic [4:0] opcode, input logic [3:0] credit = 0);
    response_opcode = opcode; response_pcrd = credit; response_valid = 1; #1;
    assert(observed[lane].response_ready) else $fatal(1, "response blocked");
    tick(); response_valid = 0;
  endtask
  initial begin
    for (int b = 0; b < 64; b++) line[b*8+:8] = 8'(b + 1);
    tick(); reset = 0;
    for (int lane = 0; lane < 3; lane++) begin
      for (int state_case = 0; state_case < 5; state_case++) begin
        int width_bits, packets;
        logic [2:0] expected_resp;
        width_bits = 128 << lane; packets = 512 / width_bits;
        state = state_case == 0 ? 4 : state_case == 1 ? 3 : state_case == 2 ? 2 : state_case == 3 ? 1 : 0;
        expected_resp = state_case == 0 ? 6 : state_case == 1 ? 7 : state;
        enabled = 3'(1 << lane); command_valid = 1;
        tick(); command_valid = 0;
        repeat (3) begin
          assert(observed[lane].request_valid && observed[lane].opcode == 7'h1b &&
                 observed[lane].size == 6 && observed[lane].address == 44'h80001240 &&
                 observed[lane].txn == 1 && observed[lane].allow_retry && !observed[lane].data_valid)
            else $fatal(1, "bad first copyback request");
          tick();
        end
        request_ready = 1; tick(); request_ready = 0;
        // Exercise retry before every grant, including credit-first arrival.
        if (state_case[0]) begin response(lane, 7, 4'hb); response(lane, 3, 4'hb); end
        else begin response(lane, 3, 4'hb); response(lane, 7, 4'hb); end
        #1;
        assert(observed[lane].request_valid && !observed[lane].allow_retry && observed[lane].pcrd == 4'hb)
          else $fatal(1, "bad copyback reissue");
        request_ready = 1; tick(); request_ready = 0;
        // An outstanding snoop blocks CompDBID, so its state update is sampled first.
        snoop_pending = 1; response_opcode = 5; response_valid = 1;
        repeat (3) begin
          #1; assert(!observed[lane].response_ready && !observed[lane].data_valid)
            else $fatal(1, "copyback overtook snoop");
          tick();
        end
        snoop_pending = 0; tick(); response_valid = 0;
        // Once granted, later changes in the live victim state cannot alter DAT.
        state = ~state;
        for (int packet = 0; packet < packets; packet++) begin
          logic [511:0] expected_data, width_mask;
          logic [63:0] expected_mask;
          width_mask = '1; width_mask >>= 512 - width_bits;
          expected_data = expected_resp == 0 ? 0 : (line >> (packet * width_bits)) & width_mask;
          expected_mask = '1; expected_mask >>= 64 - width_bits/8;
          if (expected_resp == 0) expected_mask = 0;
          repeat (3) begin
            #1;
            assert(observed[lane].data_valid && observed[lane].data_opcode == 2 &&
                   observed[lane].data_id == 2'(packet * (width_bits/128)) &&
                   observed[lane].data_txn == 'h55 && observed[lane].data_resp == expected_resp &&
                   observed[lane].data == expected_data && observed[lane].mask == expected_mask &&
                   !observed[lane].done) else $fatal(1, "copyback packet mismatch lane=%0d packet=%0d", lane, packet);
            tick();
          end
          data_ready = 1; tick(); data_ready = 0;
        end
        repeat (3) begin
          assert(observed[lane].done && observed[lane].cookie == 'ha5 &&
                 !observed[lane].command_ready && !observed[lane].data_valid)
            else $fatal(1, "copyback completion not retained");
          tick();
        end
        completion_ready = 1; tick(); completion_ready = 0;
        assert(observed[lane].command_ready && !observed[lane].done) else $fatal(1, "copyback did not retire");
      end
    end
    $display("RV5Stage line copyback at 128/256/512 bits passed");
    $finish;
  end
endmodule
