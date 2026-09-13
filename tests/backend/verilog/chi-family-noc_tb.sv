// Checks ordered payload delivery and queued backpressure across family-remapped CHI attachments.
// SPDX-License-Identifier: Apache-2.0
module chi_family_noc_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } request_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  request_t request_in_in;
  ready_t request_out_in;
  ready_t request_in_out;
  request_t request_out_out;

  CHIFamilyNoCFixture dut (.*);
  always #5 clock = ~clock;

  task automatic tick;
    begin
      @(posedge clock);
      #1;
    end
  endtask

  function automatic CHIReqFlit request_payload(input integer index);
    CHIReqFlit result;
    result = '1;
    result.tgt_id = 7'd5;
    result.src_id = 7'd3;
    result.txn_id = 12'(index);
    result.opcode = 7'h01;
    result.address = 44'h1234_5600 + 44'(index * 64);
    return result;
  endfunction

  initial begin
    integer cycles, sent, received;
    logic accepted, delivered, stalled;
    CHIReqFlit held;
    request_in_in = '0;
    request_out_in.ready = 1'b0;
    tick();
    tick();
    reset = 1'b0;
    sent = 0;
    received = 0;
    stalled = 1'b0;
    held = '0;
    for (cycles = 0; cycles < 400 && received < 24; cycles++) begin
      @(negedge clock);
      request_in_in.valid = sent < 24;
      request_in_in.bits = request_payload(sent);
      // First fill the ejection queue with its sink stopped, then repeatedly
      // stall and drain it while upstream routers still have traffic.
      request_out_in.ready = cycles >= 12 && (cycles % 5 < 2);
      #1;
      if (stalled)
        assert (request_out_out.valid && request_out_out.bits == held)
          else $fatal(1, "queued CHI request changed or disappeared while stalled");
      if (request_out_out.valid)
        assert (request_out_out.bits == request_payload(received))
          else $fatal(1, "family CHI attachment lost, reordered, or changed a packet");
      if (cycles == 11)
        assert (request_out_out.valid && !request_in_out.ready)
          else $fatal(1, "stalled ejection did not fill and backpressure the path");
      accepted = request_in_in.valid && request_in_out.ready;
      delivered = request_out_out.valid && request_out_in.ready;
      stalled = request_out_out.valid && !request_out_in.ready;
      held = request_out_out.bits;
      tick();
      if (accepted) sent++;
      if (delivered) received++;
    end
    assert (sent == 24 && received == 24)
      else $fatal(1, "family CHI attachment did not resume after backpressure");
    request_in_in.valid = 1'b0;
    request_out_in.ready = 1'b1;
    repeat (8) begin
      tick();
      assert (!request_out_out.valid)
        else $fatal(1, "family CHI attachment duplicated a packet");
    end

    $display("CHI family NoC simulation passed");
    $finish;
  end
endmodule
