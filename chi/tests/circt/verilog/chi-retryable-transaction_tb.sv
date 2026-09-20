// Simulates profile-driven CHI retry handshakes and completion milestones.
// SPDX-License-Identifier: Apache-2.0
module chi_retryable_transaction_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } response_in_t;
  typedef struct packed { logic valid; CHIRequestAttempt bits; } attempt_out_t;
  typedef struct packed { logic valid; CHIResponseEvent bits; } event_out_t;

  localparam logic [4:0] RESP_LCRD_RETURN = 5'h00;
  localparam logic [4:0] RETRY_ACK = 5'h03;
  localparam logic [4:0] COMP = 5'h04;
  localparam logic [4:0] COMP_DBID_RESP = 5'h05;
  localparam logic [4:0] DBID_RESP = 5'h06;
  localparam logic [4:0] PCRD_GRANT = 5'h07;

  logic clock = 1'b0;
  logic reset = 1'b1;
  logic start = 1'b0;
  logic finish = 1'b0;
  logic external_progress = 1'b0;
  ready_t attempt_in;
  response_in_t response_in;
  attempt_out_t attempt_out;
  ready_t response_out;
  event_out_t event_out;
  logic active;
  logic attempt_outstanding;
  logic [1:0] received;

  CHIRetryableTransactionControl dut (.*);
  always #5 clock = ~clock;

  task automatic tick;
    begin
      @(posedge clock);
      #1;
    end
  endtask

  task automatic begin_transaction;
    begin
      start = 1'b1;
      tick();
      start = 1'b0;
      if (!active || !attempt_out.valid || attempt_outstanding)
        $fatal(1, "transaction did not expose its first attempt");
    end
  endtask

  task automatic accept_attempt(
    input logic expected_allow_retry,
    input logic [3:0] expected_pcrd_type
  );
    begin
      if (!attempt_out.valid
          || attempt_out.bits.allow_retry != expected_allow_retry
          || attempt_out.bits.pcrd_type != expected_pcrd_type)
        $fatal(1, "unexpected request-attempt metadata");
      attempt_in.ready = 1'b1;
      tick();
      attempt_in.ready = 1'b0;
      if (!attempt_outstanding)
        $fatal(1, "accepted request attempt was not retained");
    end
  endtask

  task automatic send_response(
    input logic [4:0] opcode,
    input logic [3:0] pcrd_type,
    input logic [1:0] expected_effects,
    input logic expected_retry_ack,
    input logic expected_protocol_credit
  );
    begin
      response_in.bits = '0;
      response_in.bits.opcode = opcode;
      response_in.bits.pcrd_type = pcrd_type;
      response_in.valid = 1'b1;
      #1;
      if (!response_out.ready || !event_out.valid
          || event_out.bits.effects != expected_effects
          || event_out.bits.retry_ack != expected_retry_ack
          || event_out.bits.protocol_credit != expected_protocol_credit)
        $fatal(1, "unexpected accepted response event");
      tick();
      response_in.valid = 1'b0;
      response_in.bits = '0;
      #1;
    end
  endtask

  task automatic end_transaction;
    begin
      finish = 1'b1;
      tick();
      finish = 1'b0;
      if (active || attempt_outstanding || received != 2'b00)
        $fatal(1, "finished transaction retained controller state");
    end
  endtask

  initial begin
    attempt_in = '0;
    response_in = '0;
    tick();
    reset = 1'b0;

    response_in.valid = 1'b1;
    response_in.bits.opcode = RESP_LCRD_RETURN;
    #1;
    if (!response_out.ready || event_out.valid)
      $fatal(1, "link credit return did not bypass transaction state");
    tick();
    response_in = '0;

    begin_transaction();
    accept_attempt(1'b1, 4'h0);
    send_response(PCRD_GRANT, 4'h5, 2'b00, 1'b0, 1'b1);
    send_response(RETRY_ACK, 4'h5, 2'b00, 1'b1, 1'b0);
    accept_attempt(1'b0, 4'h5);
    send_response(COMP, 4'h0, 2'b10, 1'b0, 1'b0);
    if (received != 2'b10)
      $fatal(1, "completion milestone was not retained");
    send_response(DBID_RESP, 4'h0, 2'b01, 1'b0, 1'b0);
    if (received != 2'b11)
      $fatal(1, "separate response milestones did not accumulate");
    end_transaction();

    begin_transaction();
    accept_attempt(1'b1, 4'h0);
    send_response(RETRY_ACK, 4'h9, 2'b00, 1'b1, 1'b0);
    send_response(PCRD_GRANT, 4'h9, 2'b00, 1'b0, 1'b1);
    accept_attempt(1'b0, 4'h9);
    send_response(COMP_DBID_RESP, 4'h0, 2'b11, 1'b0, 1'b0);
    if (received != 2'b11)
      $fatal(1, "combined response did not complete both milestones");
    end_transaction();

    $display("chi retryable transaction test passed");
    $finish;
  end
endmodule
