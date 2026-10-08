// Checks fragment metadata, packet order, and stalls at every supported DAT
// width and option set.
// SPDX-License-Identifier: Apache-2.0
#include "request.hpp"
#include "test.hpp"
#include <bit>
#include <random>
template <unsigned DATA_WIDTH, class Input, class Output>
void check_variant(Input &upstream_in, const Output &upstream_out,
                   Output &downstream_in, const Input &downstream_out) {
  constexpr unsigned DATA_BYTES = DATA_WIDTH / 8;
  using Data = std::remove_cvref_t<decltype(upstream_in.pdat.prequest.pbits)>;
  Data packets[4]{}, expected_data{};
  std::remove_cvref_t<decltype(downstream_in.prsp.pbits)> response{},
      expected_response{};
  std::remove_cvref_t<decltype(upstream_in.preq.pbits)> request{},
      expected_request{};
  unsigned parent_dbid, child_dbid, children, first_id, packet_id;
  std::mt19937 engine(0x46524147 + DATA_WIDTH);
  int sample_index = 0;
  auto random = [&]() -> std::uint32_t {
    return sample_index == 0 ? UINT32_MAX : engine();
  };
  upstream_in = {};
  downstream_in = {};
  tick_model();
  for (int sample = 0; sample < 16; sample++) {
    sample_index = sample;

    request = {};
    request.popcode = UINT64_C(28);
    request.psrc_uid = UINT64_C(5);
    request.ptgt_uid = UINT64_C(9);
    request.ptxn_uid = ((sample)&low_mask(12));
    request.paddress =
        UINT64_C(2147483648) +
        ((sample * 64 + (sample % 2 == 0 ? 0 : 48)) & low_mask(44));
    request.psize_uor_unum_ureq = sample % 2 == 0 ? UINT64_C(6) : UINT64_C(2);
    children = sample % 2 == 0 ? 64 / DATA_BYTES : 1;
    first_id =
        (int(slice(request.paddress, 5, 0)) / DATA_BYTES) * (DATA_BYTES / 16);
    parent_dbid = UINT64_C(2048) + ((sample * 4) & low_mask(12));
    upstream_in.preq.pbits = request;
    upstream_in.preq.pvalid = 1;
    eval();
    while (!upstream_out.preq.pready)
      tick_model();
    tick_model();
    upstream_in.preq.pvalid = 0;

    for (int child = 0; child < children; child++) {
      child_dbid = parent_dbid + ((child)&low_mask(12));
      expected_request = request;
      expected_request.paddress =
          request.paddress + ((child * DATA_BYTES) & low_mask(44));
      if (children > 1)
        expected_request.psize_uor_unum_ureq =
            ((std::countr_zero(unsigned(DATA_BYTES))) & low_mask(6));
      while (!downstream_out.preq.pvalid)
        tick_model();
      for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
        CHECK(downstream_out.preq.pvalid &&
              same_request(downstream_out.preq.pbits, expected_request));
        tick_model();
      }
      downstream_in.preq.pready = 1;
      tick_model();
      downstream_in.preq.pready = 0;
      fill_response(response, random);
      response.popcode = UINT64_C(6);
      response.pdbid_uor_ugroup_uid = child_dbid;
      downstream_in.prsp.pbits = response;
      downstream_in.prsp.pvalid = 1;
      eval();
      if (child == 0) {
        for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
          CHECK(upstream_out.prsp.pvalid &&
                same_response(upstream_out.prsp.pbits, response) &&
                !downstream_out.prsp.pready);
          tick_model();
        }
        upstream_in.prsp.pready = 1;
      } else
        CHECK(!upstream_out.prsp.pvalid);
      eval();
      while (!downstream_out.prsp.pready)
        tick_model();
      tick_model();
      downstream_in.prsp.pvalid = 0;
      upstream_in.prsp.pready = 0;

      if (child == 0) {

        for (int packet = children - 1; packet >= 0; packet--) {
          packet_id = first_id + packet * (DATA_BYTES / 16);
          fill_data<DATA_WIDTH>(expected_data, random);
          expected_data.popcode = UINT64_C(3);
          expected_data.ptxn_uid = parent_dbid;
          expected_data.pdata_uid = ((packet_id)&low_mask(2));
          expected_data.preplicate = 1;
          expected_data.pnum_udat = 3;
          packets[packet_id] = expected_data;
          upstream_in.pdat.prequest.pbits = expected_data;
          upstream_in.pdat.prequest.pvalid = 1;
          eval();
          while (!upstream_out.pdat.prequest.pready)
            tick_model();
          tick_model();
          upstream_in.pdat.prequest.pvalid = 0;
        }
      }

      packet_id = first_id + child * (DATA_BYTES / 16);
      expected_data = packets[packet_id];
      expected_data.preplicate = 0;
      expected_data.pnum_udat = 0;
      expected_data.ptxn_uid = child_dbid;
      while (!downstream_out.pdat.prequest.pvalid)
        tick_model();
      for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
        CHECK(downstream_out.pdat.prequest.pvalid &&
              same_data(downstream_out.pdat.prequest.pbits, expected_data));
        tick_model();
      }
      downstream_in.pdat.prequest.pready = 1;
      tick_model();
      downstream_in.pdat.prequest.pready = 0;
      fill_response(response, random);
      response.popcode = UINT64_C(4);

      response.pdbid_uor_ugroup_uid = child_dbid ^ UINT64_C(1024);
      expected_response = response;
      expected_response.pdbid_uor_ugroup_uid = parent_dbid;
      downstream_in.prsp.pbits = response;
      downstream_in.prsp.pvalid = 1;
      eval();
      if (child == children - 1) {
        for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
          CHECK(upstream_out.prsp.pvalid &&
                same_response(upstream_out.prsp.pbits, expected_response) &&
                !downstream_out.prsp.pready);
          tick_model();
        }
        upstream_in.prsp.pready = 1;
      } else
        CHECK(!upstream_out.prsp.pvalid);
      eval();
      while (!downstream_out.prsp.pready)
        tick_model();
      tick_model();
      downstream_in.prsp.pvalid = 0;
      upstream_in.prsp.pready = 0;
    }
    CHECK(upstream_out.preq.pready);
  }
}
int main() {
  return run_test([] {
    reset = 1;
    tick_model();
    tick_model();
    tick_model();
    reset = 0;
    check_variant<128>(upstream_w128_in, upstream_w128_out, downstream_w128_in,
                       downstream_w128_out);
    check_variant<128>(upstream_o128_in, upstream_o128_out, downstream_o128_in,
                       downstream_o128_out);
    check_variant<256>(upstream_w256_in, upstream_w256_out, downstream_w256_in,
                       downstream_w256_out);
    check_variant<256>(upstream_o256_in, upstream_o256_out, downstream_o256_in,
                       downstream_o256_out);
    check_variant<512>(upstream_w512_in, upstream_w512_out, downstream_w512_in,
                       downstream_w512_out);
    check_variant<512>(upstream_o512_in, upstream_o512_out, downstream_o512_in,
                       downstream_o512_out);
  });
}
