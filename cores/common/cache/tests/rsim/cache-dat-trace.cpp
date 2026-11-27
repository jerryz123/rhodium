// SPDX-License-Identifier: Apache-2.0
#include "test-sites.h"
#include "wide.hpp"
#include <set>

int main() {
  return run_test([] {
    auto& graph = rheg::graph();
    graph.bind_manifest(rheg_generated::manifest());
    reset = 1;
    tick_model();
    reset = 0;
    std::set<std::pair<rheg::Ref, rheg::Ref>> expected;
    unsigned nodes = 0;
    auto check_graph = [&] {
      CHECK(graph.nodes.size() == nodes && graph.edges == expected);
    };
    auto accepted = [&](unsigned input, unsigned output, unsigned sequence) {
      const rheg::Ref parent{input, sequence}, child{output, sequence};
      expected.emplace(parent, child);
      CHECK(graph.nodes.at(parent).cycle == graph.nodes.at(child).cycle);
      CHECK(!graph.nodes.at(child).ancestry_unknown);
      nodes += 2;
      check_graph();
    };

    for (unsigned line = 0; line < 2; ++line) {
      dbid = 0x120 + line;
      start = 1;
      eval();
      CHECK(can_start);
      tick_model();
      start = 0;
      nodes += 2; // The two production command residency events.
      CHECK(graph.nodes.contains({site_dcache_writeback, line}));
      CHECK(graph.nodes.contains({site_icache_refill, line}));
      check_graph();

      eval();
      CHECK(read_request && write_request);
      tick_model();
      grant_valid = 1;
      // Both packet sources are ready to send before the write grant and dirty lookup.
      victim_packet_in.pvalid = dirty_packet_in.pvalid = 1;
      copyback_dat_in.pready = snoop_dat_in.pready = 1;
      eval();
      CHECK(can_grant && !victim_packet_out.pready && !dirty_packet_out.pready);
      CHECK(!copyback_dat_out.pvalid && !snoop_dat_out.pvalid);
      tick_model();
      check_graph();
      grant_valid = 0;
      copyback_dat_in.pready = snoop_dat_in.pready = 0;

      for (unsigned beat = 0; beat < 4; ++beat) {
        const unsigned sequence = 4 * line + beat;
        const unsigned read_index = (beat + 2) % 4;
        const uint128 value = (uint128(0xaced + line) << 96) + sequence;
        read_dat_in.pvalid = victim_packet_in.pvalid = dirty_packet_in.pvalid = 1;
        read_dat_in.pbits.popcode = 4; // CompData
        read_dat_in.pbits.ptgt_uid = 2;
        read_dat_in.pbits.phome_unid_uor_upbha_uor_umismatched_umecid = 1;
        read_dat_in.pbits.pdata_uid = read_index;
        read_dat_in.pbits.pdata = wide(value);
        victim_packet_in.pbits = wide(value + 16);
        dirty_packet_in.pbits.pindex = beat;
        dirty_packet_in.pbits.pdata = wide(value + 32);

        for (unsigned stall = 0; stall < 2 + beat; ++stall) {
          eval();
          CHECK(!read_dat_out.pready && !victim_packet_out.pready && !dirty_packet_out.pready);
          CHECK(read_packet_out.pvalid && read_packet_out.pbits.pindex == read_index);
          CHECK(wide_value(read_packet_out.pbits.pdata) == value);
          CHECK(copyback_dat_out.pvalid && copyback_dat_out.pbits.pdata_uid == beat);
          CHECK(copyback_dat_out.pbits.popcode == 2 && copyback_dat_out.pbits.pbyte_uenable == 0xffff);
          CHECK(copyback_dat_out.pbits.ptxn_uid == dbid && wide_value(copyback_dat_out.pbits.pdata) == value + 16);
          CHECK(snoop_dat_out.pvalid && snoop_dat_out.pbits.pdata_uid == beat);
          CHECK(snoop_dat_out.pbits.popcode == 1 && snoop_dat_out.pbits.pbyte_uenable == 0xffff);
          CHECK(snoop_dat_out.pbits.ptxn_uid == 9 && wide_value(snoop_dat_out.pbits.pdata) == value + 32);
          CHECK(!snoop_done && !write_done && !read_done);
          tick_model();
          check_graph();
        }

        read_packet_in.pready = 1;
        eval();
        CHECK(read_dat_out.pready && read_packet_out.pvalid);
        tick_model();
        read_dat_in.pvalid = read_packet_in.pready = 0;
        accepted(site_read_dat, site_read_packet, sequence);

        copyback_dat_in.pready = 1;
        eval();
        CHECK(victim_packet_out.pready && copyback_dat_out.pvalid);
        tick_model();
        victim_packet_in.pvalid = copyback_dat_in.pready = 0;
        expected.emplace(rheg::Ref{site_dcache_writeback, line}, rheg::Ref{site_copyback_dat, sequence});
        accepted(site_victim_packet, site_copyback_dat, sequence);

        snoop_dat_in.pready = 1;
        eval();
        CHECK(dirty_packet_out.pready && snoop_dat_out.pvalid);
        CHECK(bool(snoop_done) == (beat == 3));
        CHECK(bool(write_done) == (beat == 3) && bool(read_done) == (beat == 3));
        tick_model();
        dirty_packet_in.pvalid = snoop_dat_in.pready = 0;
        accepted(site_dirty_packet, site_snoop_dat, sequence);
      }
    }
    graph.validate();
  });
}
