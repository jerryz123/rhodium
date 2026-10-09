// SPDX-License-Identifier: Apache-2.0
#include "../../../../chi/tests/rsim/inclusive-home-behavior.hpp"
extern "C" void event_home_bind();
extern "C" void event_home_expect_backing(unsigned owner_age);
extern "C" void event_home_sample(
    unsigned reset, unsigned request_fire, std::uint64_t address,
    unsigned request_opcode, unsigned request_txn, unsigned request_src,
    unsigned return_txn, unsigned return_nid, unsigned request_size,
    unsigned response_fire, unsigned response_opcode, unsigned response_txn,
    unsigned response_tgt, unsigned response_dbid, unsigned data_fire,
    unsigned data_opcode, unsigned data_txn, unsigned data_tgt,
    unsigned data_id, unsigned request_data_fire, unsigned request_data_opcode,
    unsigned request_data_txn, unsigned request_data_id, unsigned backing_fire,
    std::uint64_t backing_address, unsigned backing_opcode,
    unsigned backing_txn, unsigned backing_data_fire, unsigned backing_data_txn,
    unsigned backing_data_id, unsigned backing_response_fire,
    unsigned backing_response_opcode, unsigned backing_response_txn,
    unsigned backing_response_dbid, unsigned output_stalled);
extern "C" void event_home_check();
extern "C" void event_home_finish();
int main() {
  return run_test([] {
    event_home_bind();
    home_before_tick = [] {
      event_home_sample(
          ((reset)&low_mask(32)),
          ((requester_requests_in.pvalid &&
            port_out.prequester.prequests.pready) &
           low_mask(32)),
          ((requester_requests_in.pbits.paddress) & low_mask(64)),
          ((requester_requests_in.pbits.popcode) & low_mask(32)),
          ((requester_requests_in.pbits.ptxn_uid) & low_mask(32)),
          ((requester_requests_in.pbits.psrc_uid) & low_mask(32)),
          ((requester_requests_in.pbits.preturn_utxn_uid_uor_ustash_ulpid) &
           low_mask(32)),
          ((requester_requests_in.pbits
                .preturn_unid_uor_ustash_unid_uor_udata_utarget) &
           low_mask(32)),
          ((requester_requests_in.pbits.psize_uor_unum_ureq) & low_mask(32)),
          ((port_out.prequester.presponses.pvalid &&
            requester_responses_ready_in.pready) &
           low_mask(32)),
          ((port_out.prequester.presponses.pbits.popcode) & low_mask(32)),
          ((port_out.prequester.presponses.pbits.ptxn_uid) & low_mask(32)),
          ((port_out.prequester.presponses.pbits.ptgt_uid) & low_mask(32)),
          ((port_out.prequester.presponses.pbits.pdbid_uor_ugroup_uid) &
           low_mask(32)),
          ((port_out.prequester.presponse_udata.pvalid &&
            response_data_ready_in.pready) &
           low_mask(32)),
          ((port_out.prequester.presponse_udata.pbits.popcode) & low_mask(32)),
          ((port_out.prequester.presponse_udata.pbits.ptxn_uid) & low_mask(32)),
          ((port_out.prequester.presponse_udata.pbits.ptgt_uid) & low_mask(32)),
          ((port_out.prequester.presponse_udata.pbits.pdata_uid) &
           low_mask(32)),
          ((request_data_in.pvalid &&
            port_out.prequester.prequest_udata.pready) &
           low_mask(32)),
          ((request_data_in.pbits.popcode) & low_mask(32)),
          ((request_data_in.pbits.ptxn_uid) & low_mask(32)),
          ((request_data_in.pbits.pdata_uid) & low_mask(32)),
          ((port_out.psubordinate.preq.pvalid &&
            subordinate_requests_ready_in.pready) &
           low_mask(32)),
          ((port_out.psubordinate.preq.pbits.paddress) & low_mask(64)),
          ((port_out.psubordinate.preq.pbits.popcode) & low_mask(32)),
          ((port_out.psubordinate.preq.pbits.ptxn_uid) & low_mask(32)),
          ((port_out.psubordinate.pdat.prequest.pvalid &&
            subordinate_data_ready_in.pready) &
           low_mask(32)),
          ((port_out.psubordinate.pdat.prequest.pbits.ptxn_uid) & low_mask(32)),
          ((port_out.psubordinate.pdat.prequest.pbits.pdata_uid) &
           low_mask(32)),
          ((subordinate_responses_in.pvalid &&
            port_out.psubordinate.prsp.pready) &
           low_mask(32)),
          ((subordinate_responses_in.pbits.popcode) & low_mask(32)),
          ((subordinate_responses_in.pbits.ptxn_uid) & low_mask(32)),
          ((subordinate_responses_in.pbits.pdbid_uor_ugroup_uid) &
           low_mask(32)),
          (((port_out.prequester.presponses.pvalid &&
             !requester_responses_ready_in.pready) ||
            (port_out.prequester.presponse_udata.pvalid &&
             !response_data_ready_in.pready)) &
           low_mask(32)));
    };
    home_after_tick = event_home_check;
    home_expect_backing = event_home_expect_backing;
    run_case();
    event_home_finish();
  });
}
