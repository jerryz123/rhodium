// Simulates both sides of a two-subordinate CHI SN NoC attachment.
// SPDX-License-Identifier: Apache-2.0
#include "request.hpp"
#include "test.hpp"
#include "wide.hpp"
using CHIReqFlit = std::remove_cvref_t<decltype(home_in.preq.pbits)>;
using CHIRspFlit = std::remove_cvref_t<decltype(first_in.prsp.pbits)>;
using CHIDatFlit = std::remove_cvref_t<decltype(home_in.pdat.prequest.pbits)>;
using dat_t = std::remove_cvref_t<decltype(home_in.pdat.prequest)>;
using rsp_t = std::remove_cvref_t<decltype(first_in.prsp)>;
using req_t = std::remove_cvref_t<decltype(home_in.preq)>;
constexpr std::uint8_t HOME_ID = UINT64_C(5);
constexpr std::uint8_t FIRST_ID = UINT64_C(9);
constexpr std::uint8_t SECOND_ID = UINT64_C(10);

void send_request(bool target_lane, std::uint16_t txn_id) {
  int cycles;
  {

    first_in.preq.pready = target_lane;
    second_in.preq.pready = !target_lane;
    home_in.preq = {};
    home_in.preq.pbits.popcode = UINT64_C(4);
    home_in.preq.pbits.psrc_uid = HOME_ID;
    home_in.preq.pbits.ptgt_uid = target_lane ? SECOND_ID : FIRST_ID;
    home_in.preq.pbits.ptxn_uid = txn_id;
    home_in.preq.pbits.paddress =
        target_lane ? UINT64_C(2415919104) : UINT64_C(2147483648);
    home_in.preq.pvalid = UINT64_C(1);
    eval();
    while (!home_out.preq.pready)
      tick_model();
    tick_model();
    home_in.preq = {};

    for (cycles = 0; cycles < 8; cycles = cycles + 1) {
      eval();
      if (target_lane ? second_out.preq.pvalid : first_out.preq.pvalid)
        break;
      tick_model();
    }
    CHECK(cycles < 8);
    CHECK(!(target_lane ? first_out.preq.pvalid : second_out.preq.pvalid));
    CHECK((target_lane ? second_out.preq.pbits.ptgt_uid
                       : first_out.preq.pbits.ptgt_uid) ==
              (target_lane ? SECOND_ID : FIRST_ID) &&
          (target_lane ? second_out.preq.pbits.ptxn_uid
                       : first_out.preq.pbits.ptxn_uid) == txn_id);
    tick_model();
    CHECK(target_lane ? second_out.preq.pvalid : first_out.preq.pvalid);
    if (target_lane)
      second_in.preq.pready = UINT64_C(1);
    else
      first_in.preq.pready = UINT64_C(1);
    tick_model();
    first_in.preq.pready = UINT64_C(0);
    second_in.preq.pready = UINT64_C(0);
  }
}

void send_response(bool source_lane, std::uint16_t txn_id) {
  int cycles;
  {
    home_in.prsp.pready = UINT64_C(0);
    if (source_lane) {
      second_in.prsp = {};
      second_in.prsp.pbits.popcode = UINT64_C(4);
      second_in.prsp.pbits.psrc_uid = SECOND_ID;
      second_in.prsp.pbits.ptgt_uid = HOME_ID;
      second_in.prsp.pbits.ptxn_uid = txn_id;
      second_in.prsp.pvalid = UINT64_C(1);
      eval();
      for (cycles = 0; cycles < 8 && !second_out.prsp.pready;
           cycles = cycles + 1)
        tick_model();
      CHECK(second_out.prsp.pready);
      tick_model();
      second_in.prsp = {};
    } else {
      first_in.prsp = {};
      first_in.prsp.pbits.popcode = UINT64_C(4);
      first_in.prsp.pbits.psrc_uid = FIRST_ID;
      first_in.prsp.pbits.ptgt_uid = HOME_ID;
      first_in.prsp.pbits.ptxn_uid = txn_id;
      first_in.prsp.pvalid = UINT64_C(1);
      eval();
      for (cycles = 0; cycles < 8 && !first_out.prsp.pready;
           cycles = cycles + 1)
        tick_model();
      CHECK(first_out.prsp.pready);
      tick_model();
      first_in.prsp = {};
    }
    for (cycles = 0; cycles < 8 && !home_out.prsp.pvalid; cycles = cycles + 1)
      tick_model();
    CHECK(home_out.prsp.pvalid &&
          home_out.prsp.pbits.psrc_uid ==
              (source_lane ? SECOND_ID : FIRST_ID) &&
          home_out.prsp.pbits.ptxn_uid == txn_id);
    tick_model();
    CHECK(home_out.prsp.pvalid);
    home_in.prsp.pready = UINT64_C(1);
    tick_model();
    home_in.prsp.pready = UINT64_C(0);
  }
}

void send_request_data(bool target_lane, std::uint16_t txn_id,
                       unsigned __int128 payload) {
  int cycles;
  {
    if (target_lane)
      second_in.pdat.prequest.pready = UINT64_C(1);
    else
      first_in.pdat.prequest.pready = UINT64_C(1);
    home_in.pdat.prequest = {};
    home_in.pdat.prequest.pbits.popcode = UINT64_C(3);
    home_in.pdat.prequest.pbits.psrc_uid = HOME_ID;
    home_in.pdat.prequest.pbits.ptgt_uid = target_lane ? SECOND_ID : FIRST_ID;
    home_in.pdat.prequest.pbits.ptxn_uid = txn_id;
    home_in.pdat.prequest.pbits.pdata = wide(payload);
    home_in.pdat.prequest.pvalid = UINT64_C(1);
    eval();
    while (!home_out.pdat.prequest.pready)
      tick_model();
    tick_model();
    home_in.pdat.prequest = {};

    for (cycles = 0; cycles < 8; cycles = cycles + 1) {
      eval();
      if (target_lane ? second_out.pdat.prequest.pvalid
                      : first_out.pdat.prequest.pvalid)
        break;
      tick_model();
    }
    CHECK(cycles < 8);
    CHECK(!(target_lane ? first_out.pdat.prequest.pvalid
                        : second_out.pdat.prequest.pvalid));
    CHECK((target_lane ? second_out.pdat.prequest.pbits.pdata
                       : first_out.pdat.prequest.pbits.pdata) == wide(payload));
    tick_model();
    first_in.pdat.prequest.pready = UINT64_C(0);
    second_in.pdat.prequest.pready = UINT64_C(0);
  }
}

void send_response_data(bool source_lane, std::uint16_t txn_id,
                        unsigned __int128 payload) {
  int cycles;
  {
    home_in.pdat.presponse.pready = UINT64_C(1);
    if (source_lane) {
      second_in.pdat.presponse = {};
      second_in.pdat.presponse.pbits.popcode = UINT64_C(4);
      second_in.pdat.presponse.pbits.psrc_uid = SECOND_ID;
      second_in.pdat.presponse.pbits.ptgt_uid = HOME_ID;
      second_in.pdat.presponse.pbits.ptxn_uid = txn_id;
      second_in.pdat.presponse.pbits.pdata = wide(payload);
      second_in.pdat.presponse.pvalid = UINT64_C(1);
      eval();
      while (!second_out.pdat.presponse.pready)
        tick_model();
      tick_model();
      second_in.pdat.presponse = {};
    } else {
      first_in.pdat.presponse = {};
      first_in.pdat.presponse.pbits.popcode = UINT64_C(4);
      first_in.pdat.presponse.pbits.psrc_uid = FIRST_ID;
      first_in.pdat.presponse.pbits.ptgt_uid = HOME_ID;
      first_in.pdat.presponse.pbits.ptxn_uid = txn_id;
      first_in.pdat.presponse.pbits.pdata = wide(payload);
      first_in.pdat.presponse.pvalid = UINT64_C(1);
      eval();
      while (!first_out.pdat.presponse.pready)
        tick_model();
      tick_model();
      first_in.pdat.presponse = {};
    }

    for (cycles = 0; cycles < 8; cycles = cycles + 1) {
      eval();
      if (home_out.pdat.presponse.pvalid)
        break;
      tick_model();
    }
    CHECK(cycles < 8);
    CHECK(home_out.pdat.presponse.pbits.psrc_uid ==
              (source_lane ? SECOND_ID : FIRST_ID) &&
          home_out.pdat.presponse.pbits.ptgt_uid == HOME_ID &&
          home_out.pdat.presponse.pbits.ptxn_uid == txn_id &&
          home_out.pdat.presponse.pbits.pdata == wide(payload));
    tick_model();
    home_in.pdat.presponse.pready = UINT64_C(0);
  }
}

void run_case() {
  reset = UINT64_C(1);
  home_in = {};
  first_in = {};
  second_in = {};
  tick_model();
  reset = UINT64_C(0);

  send_request(UINT64_C(0), UINT64_C(257));
  send_request(UINT64_C(1), UINT64_C(258));
  send_response(UINT64_C(0), UINT64_C(513));
  send_response(UINT64_C(1), UINT64_C(514));
  send_request_data(UINT64_C(0), UINT64_C(769),
                    ((uint128(UINT64_C(4822678189205111)) << 64) |
                     UINT64_C(9843086184167632639)));
  send_request_data(UINT64_C(1), UINT64_C(770),
                    ((uint128(UINT64_C(18441921395520346504)) << 64) |
                     UINT64_C(8603657889541918976)));
  send_response_data(UINT64_C(0), UINT64_C(1025),
                     ((uint128(UINT64_C(81985529216486895)) << 64) |
                      UINT64_C(81985529216486895)));
  send_response_data(UINT64_C(1), UINT64_C(1026),
                     ((uint128(UINT64_C(18364758544493064720)) << 64) |
                      UINT64_C(18364758544493064720)));
}

int main() { return run_test(run_case); }
