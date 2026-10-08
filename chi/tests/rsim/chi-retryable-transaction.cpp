// Simulates profile-driven CHI retry handshakes and completion milestones.
// SPDX-License-Identifier: Apache-2.0
#include "request.hpp"
#include "test.hpp"
constexpr std::uint8_t RESP_LCRD_RETURN = UINT64_C(0);
constexpr std::uint8_t RETRY_ACK = UINT64_C(3);
constexpr std::uint8_t COMP = UINT64_C(4);
constexpr std::uint8_t COMP_DBID_RESP = UINT64_C(5);
constexpr std::uint8_t DBID_RESP = UINT64_C(6);
constexpr std::uint8_t PCRD_GRANT = UINT64_C(7);

void begin_transaction() {
  {
    start = UINT64_C(1);
    tick_model();
    start = UINT64_C(0);
    if (!active || !attempt_out.pvalid || attempt_outstanding)
      fail(1, "transaction did not expose its first attempt");
  }
}

void accept_attempt(bool expected_allow_retry,
                    std::uint8_t expected_pcrd_type) {
  {
    if (!attempt_out.pvalid ||
        attempt_out.pbits.pallow_uretry != expected_allow_retry ||
        attempt_out.pbits.ppcrd_utype != expected_pcrd_type)
      fail(1, "unexpected request-attempt metadata");
    attempt_in.pready = UINT64_C(1);
    tick_model();
    attempt_in.pready = UINT64_C(0);
    if (!attempt_outstanding)
      fail(1, "accepted request attempt was not retained");
  }
}

void send_response(std::uint8_t opcode, std::uint8_t pcrd_type,
                   std::uint8_t expected_effects, bool expected_retry_ack,
                   bool expected_protocol_credit) {
  {
    response_in.pbits = {};
    response_in.pbits.popcode = opcode;
    response_in.pbits.ppcrd_utype = pcrd_type;
    response_in.pvalid = UINT64_C(1);
    eval();
    if (!response_out.pready || !event_out.pvalid ||
        event_out.pbits.peffects != expected_effects ||
        event_out.pbits.pretry_uack != expected_retry_ack ||
        event_out.pbits.pprotocol_ucredit != expected_protocol_credit)
      fail(1, "unexpected accepted response event");
    tick_model();
    response_in.pvalid = UINT64_C(0);
    response_in.pbits = {};
    eval();
  }
}

void end_transaction() {
  {
    finish = UINT64_C(1);
    tick_model();
    finish = UINT64_C(0);
    if (active || attempt_outstanding || received != UINT64_C(0))
      fail(1, "finished transaction retained controller state");
  }
}

void run_case() {
  attempt_in = {};
  response_in = {};
  tick_model();
  reset = UINT64_C(0);

  response_in.pvalid = UINT64_C(1);
  response_in.pbits.popcode = RESP_LCRD_RETURN;
  eval();
  if (!response_out.pready || event_out.pvalid)
    fail(1, "link credit return did not bypass transaction state");
  tick_model();
  response_in = {};

  begin_transaction();
  accept_attempt(UINT64_C(1), UINT64_C(0));
  send_response(PCRD_GRANT, UINT64_C(5), UINT64_C(0), UINT64_C(0), UINT64_C(1));
  send_response(RETRY_ACK, UINT64_C(5), UINT64_C(0), UINT64_C(1), UINT64_C(0));
  accept_attempt(UINT64_C(0), UINT64_C(5));
  send_response(COMP, UINT64_C(0), UINT64_C(2), UINT64_C(0), UINT64_C(0));
  if (received != UINT64_C(2))
    fail(1, "completion milestone was not retained");
  send_response(DBID_RESP, UINT64_C(0), UINT64_C(1), UINT64_C(0), UINT64_C(0));
  if (received != UINT64_C(3))
    fail(1, "separate response milestones did not accumulate");
  end_transaction();

  begin_transaction();
  accept_attempt(UINT64_C(1), UINT64_C(0));
  send_response(RETRY_ACK, UINT64_C(9), UINT64_C(0), UINT64_C(1), UINT64_C(0));
  send_response(PCRD_GRANT, UINT64_C(9), UINT64_C(0), UINT64_C(0), UINT64_C(1));
  accept_attempt(UINT64_C(0), UINT64_C(9));
  send_response(COMP_DBID_RESP, UINT64_C(0), UINT64_C(3), UINT64_C(0),
                UINT64_C(0));
  if (received != UINT64_C(3))
    fail(1, "combined response did not complete both milestones");
  end_transaction();
}

int main() { return run_test(run_case); }
