// Drives independent cache and backing-memory models for both Home maintenance
// implementations.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "request.hpp"
#include "test.hpp"
#include "wide.hpp"
constexpr std::uint64_t A = 0x80000000, B = 0x80000100, C = 0x80000200;
using Request = std::remove_cvref_t<decltype(port_in.prequester.prequests)>;
using Data = std::remove_cvref_t<decltype(port_in.prequester.prequest_udata)>;
using Dispatch =
    std::remove_cvref_t<decltype(port_out.prequester.psnoops.pbits)>;
Request issue{};
Data write_data{};
struct Environment {
  int cycles = 0, writes = 0, reads = 0, snoops = 0;
  uint128 ram[256]{}, cache_data[4][4]{}, observed[4]{};
  bool cached[4]{}, dirty[4]{};
  std::uint64_t cache_address[4]{};
  bool snp_active = false, snp_data = false, snp_hit = false,
       hold_snoops = false;
  bool stalled_snoop = false, maintenance_active = false;
  unsigned expected_snoops = 0;
  Dispatch stalled_dispatch{};
  int snp_target = 0, snp_packet = 0, snp_opcode = 0, snp_txn = 0;
  int mem_phase = 0, mem_packet = 0, mem_packets = 0, mem_delay = 0;
  std::uint64_t mem_address = 0;
  int mem_transaction = 0, write_error = 0, observed_count = 0;
} environment;
inline uint128 repeated_byte(unsigned value) {
  uint128 result = 0;
  for (unsigned i = 0; i < 16; ++i)
    result |= uint128(value & 255) << (8 * i);
  return result;
}
void settle() {
  const auto &e = environment;
  identity = {};
  identity.phome_unode_uid = 5;
  identity.psubordinate_unode_uid = 9;
  identity.pservice_ubase = A;
  port_in = {};
  auto &rn = port_in.prequester;
  auto &sn = port_in.psubordinate;
  rn.prequests = issue;
  rn.prequest_udata = write_data;
  rn.presponses.pready = e.cycles % 5 == 0;
  rn.presponse_udata.pready = e.cycles % 3 != 0;
  rn.psnoops.pready = !e.hold_snoops && !e.snp_active && e.cycles % 3 != 0;
  if (e.snp_active) {
    if (e.snp_data) {
      rn.prequest_udata = {};
      rn.prequest_udata.pvalid = 1;
      auto &d = rn.prequest_udata.pbits;
      d.popcode = 1;
      d.psrc_uid = e.snp_target;
      d.ptgt_uid = 5;
      d.ptxn_uid = e.snp_txn;
      d.pdata_uid = e.snp_packet;
      d.pbyte_uenable = 65535;
      d.pdata = wide(e.cache_data[e.snp_target][e.snp_packet]);
      d.presp = e.snp_opcode == 8 ? 5 : 4;
    } else {
      rn.prequester_uresponses.pvalid = 1;
      auto &r = rn.prequester_uresponses.pbits;
      r.popcode = 1;
      r.psrc_uid = e.snp_target;
      r.ptgt_uid = 5;
      r.ptxn_uid = e.snp_txn;
      r.presp = e.snp_hit && e.snp_opcode == 8 ? 1 : 0;
    }
  }
  sn.preq.pready = e.mem_phase == 0 && e.cycles % 3 != 0;
  if ((e.mem_phase == 1 || e.mem_phase == 3) && e.mem_delay == 0) {
    sn.prsp.pvalid = 1;
    auto &r = sn.prsp.pbits;
    r.popcode = e.mem_phase == 1 ? 6 : 4;
    r.psrc_uid = 9;
    r.ptgt_uid = 5;
    r.ptxn_uid = e.mem_transaction;
    r.pdbid_uor_ugroup_uid = 0x55;
    r.presp_uerr = e.mem_phase == 3 ? e.write_error : 0;
  }
  sn.pdat.prequest.pready = e.mem_phase == 2 && e.cycles % 3 != 0;
  if (e.mem_phase == 4) {
    sn.pdat.presponse.pvalid = 1;
    auto &d = sn.pdat.presponse.pbits;
    d.popcode = 4;
    d.psrc_uid = 9;
    d.ptgt_uid = 5;
    d.ptxn_uid = e.mem_transaction;
    d.pdata_uid = e.mem_packet;
    d.pbyte_uenable = 65535;
    d.pdata = wide(e.ram[((e.mem_address >> 4) & 255) + e.mem_packet]);
  }
  eval();
}
void maintenance_tick() {
  settle();
  // Compute every host-model transition from the same pre-edge snapshot.
  const auto &e = environment;
  auto next = e;
  const auto &rn = port_out.prequester;
  const auto &sn = port_out.psubordinate;
  const auto &ri = port_in.prequester;
  const auto &si = port_in.psubordinate;
  if (reset) {
    next.stalled_snoop = false;
    next.maintenance_active = false;
    next.expected_snoops = 0;
  } else {
    if (e.stalled_snoop)
      CHECK(rn.psnoops.pvalid &&
            rn.psnoops.pbits.ptarget_uid == e.stalled_dispatch.ptarget_uid &&
            same_snoop(rn.psnoops.pbits.pflit, e.stalled_dispatch.pflit));
    next.stalled_snoop = rn.psnoops.pvalid && !ri.psnoops.pready;
    next.stalled_dispatch = rn.psnoops.pbits;
    if (e.snp_active && !INCLUSIVE)
      CHECK(!rn.psnoops.pvalid);
    if (issue.pvalid && rn.prequests.pready) {
      const auto &r = issue.pbits;
      next.maintenance_active =
          r.popcode == 8 || r.popcode == 9 || r.popcode == 10;
      next.expected_snoops = 0;
      for (unsigned target = 2; target <= 3; ++target)
        if ((r.psrc_uid != target || r.pexcl_usnoop_ume_ucah) &&
            (!INCLUSIVE ||
             (e.cached[target] && e.cache_address[target] == r.paddress)))
          next.expected_snoops |= 1u << (target - 2);
    }
    if (e.maintenance_active && rn.psnoops.pvalid && ri.psnoops.pready) {
      CHECK(e.expected_snoops &&
            rn.psnoops.pbits.ptarget_uid == ((e.expected_snoops & 1) ? 2 : 3));
      next.expected_snoops =
          e.expected_snoops & (rn.psnoops.pbits.ptarget_uid == 2 ? 2 : 1);
    }
    if (e.maintenance_active && rn.presponses.pvalid && ri.presponses.pready) {
      CHECK(e.expected_snoops == 0);
      next.maintenance_active = false;
    }
    next.cycles = e.cycles + 1;
    CHECK(e.cycles < 10000);
    if (rn.psnoops.pvalid && ri.psnoops.pready) {
      const auto &s = rn.psnoops.pbits;
      CHECK(s.ptarget_uid == 2 || s.ptarget_uid == 3);
      next.snoops = e.snoops + 1;
      next.snp_active = true;
      next.snp_target = s.ptarget_uid;
      next.snp_packet = 0;
      next.snp_opcode = s.pflit.popcode;
      next.snp_txn = s.pflit.ptxn_uid;
      bool hit = e.cached[s.ptarget_uid] && (e.cache_address[s.ptarget_uid] >>
                                             6) == (s.pflit.paddress >> 3);
      next.snp_hit = hit;
      next.snp_data = hit && e.dirty[s.ptarget_uid] && s.pflit.popcode != 10;
    }
    if (e.snp_active &&
        ((e.snp_data && ri.prequest_udata.pvalid && rn.prequest_udata.pready) ||
         (!e.snp_data && ri.prequester_uresponses.pvalid &&
          rn.prequester_uresponses.pready))) {
      if (!e.snp_data || e.snp_packet == 3) {
        next.snp_active = false;
        if (e.snp_hit) {
          next.dirty[e.snp_target] = false;
          if (e.snp_opcode != 8)
            next.cached[e.snp_target] = false;
        }
      } else
        next.snp_packet = e.snp_packet + 1;
    }
    if (e.mem_delay > 0)
      next.mem_delay = e.mem_delay - 1;
    if (sn.preq.pvalid && si.preq.pready) {
      const auto &r = sn.preq.pbits;
      next.mem_address = r.paddress;
      next.mem_transaction = r.ptxn_uid;
      next.mem_packets = r.psize_uor_unum_ureq == 6 ? 4 : 1;
      next.mem_packet = 0;
      if (r.popcode == 4) {
        next.reads = e.reads + 1;
        next.mem_phase = 4;
      } else {
        CHECK(r.popcode == 0x1d);
        next.writes = e.writes + 1;
        next.mem_phase = 1;
        next.mem_delay = 3;
      }
    }
    if (si.prsp.pvalid && sn.prsp.pready)
      next.mem_phase = e.mem_phase == 1 ? 2 : 0;
    if (sn.pdat.prequest.pvalid && si.pdat.prequest.pready) {
      const auto &d = sn.pdat.prequest.pbits;
      CHECK(d.ptxn_uid == 0x55);
      for (unsigned b = 0; b < 16; ++b)
        if (((d.pbyte_uenable >> b) & 1) && e.write_error == 0) {
          auto &word = next.ram[((e.mem_address >> 4) & 255) + e.mem_packet];
          word = (word & ~(uint128(255) << (b * 8))) |
                 (uint128((d.pdata.words[b / 4] >> ((b % 4) * 8)) & 255)
                  << (b * 8));
        }
      if (e.mem_packet == e.mem_packets - 1) {
        next.mem_phase = 3;
        next.mem_delay = 5;
      } else
        next.mem_packet = e.mem_packet + 1;
    }
    if (si.pdat.presponse.pvalid && sn.pdat.presponse.pready) {
      if (e.mem_packet == e.mem_packets - 1)
        next.mem_phase = 0;
      else
        next.mem_packet = e.mem_packet + 1;
    }
    if (rn.presponse_udata.pvalid && ri.presponse_udata.pready) {
      next.observed[rn.presponse_udata.pbits.pdata_uid] =
          wide_value(rn.presponse_udata.pbits.pdata);
      next.observed_count = e.observed_count + 1;
    }
  }
  tick_model();
  environment = next;
  settle();
}

void send(int source, std::uint8_t opcode, std::uint64_t address,
          bool snoop_me) {
  issue = {};
  issue.pbits.psrc_uid = ((source)&low_mask(7));
  issue.pbits.ptgt_uid = 5;
  issue.pbits.ptxn_uid = UINT64_C(49);
  issue.pbits.preturn_unid_uor_ustash_unid_uor_udata_utarget =
      ((source)&low_mask(7));
  issue.pbits.preturn_utxn_uid_uor_ustash_ulpid = UINT64_C(49);
  issue.pbits.popcode = opcode;
  issue.pbits.paddress = address;
  issue.pbits.psize_uor_unum_ureq = 6;
  issue.pbits.pexcl_usnoop_ume_ucah = snoop_me;
  issue.pvalid = 1;
  settle();
  while (!port_out.prequester.prequests.pready)
    maintenance_tick();
  maintenance_tick();
  issue = {};
}
void finish(std::uint8_t error_code) {
  settle();
  while (!(port_out.prequester.presponses.pvalid &&
           port_in.prequester.presponses.pready))
    maintenance_tick();
  CHECK(port_out.prequester.presponses.pbits.popcode == 4 &&
        port_out.prequester.presponses.pbits.ptxn_uid == UINT64_C(49) &&
        port_out.prequester.presponses.pbits.presp_uerr == error_code);
  CHECK(!environment.snp_active && environment.mem_phase == 0);
  maintenance_tick();
}
void install(int target, std::uint64_t address, bool is_dirty,
             std::uint8_t value) {
  if (INCLUSIVE) {
    environment.observed_count = 0;
    send(target, is_dirty ? UINT64_C(7) : UINT64_C(2), address, 0);
    while (environment.observed_count < 4)
      maintenance_tick();
  }
  environment.cached[target] = 1;
  environment.dirty[target] = is_dirty;
  environment.cache_address[target] = address;
  for (int p = 0; p < 4; p++)
    environment.cache_data[target][p] = repeated_byte(value);
}
void check_ram(std::uint64_t address, std::uint8_t value) {
  for (int p = 0; p < 4; p++)
    CHECK(environment.ram[((address >> 4) & 255) + p] == repeated_byte(value));
}
void read_line(std::uint64_t address, std::uint8_t value) {
  environment.observed_count = 0;
  send(1, 4, address, 0);
  while (environment.observed_count < 4)
    maintenance_tick();
  for (int p = 0; p < 4; p++)
    CHECK(environment.observed[p] == repeated_byte(value));
}
void write_line(std::uint64_t address, std::uint8_t value) {
  send(1, UINT64_C(29), address, 0);
  while (!(port_out.prequester.presponses.pvalid &&
           port_in.prequester.presponses.pready))
    maintenance_tick();
  CHECK(port_out.prequester.presponses.pbits.popcode == 6);
  maintenance_tick();
  for (int p = 0; p < 4; p++) {
    write_data = {};
    write_data.pvalid = 1;
    write_data.pbits.popcode = 3;
    write_data.pbits.psrc_uid = 1;
    write_data.pbits.ptgt_uid = 5;
    write_data.pbits.pdata_uid = ((p)&low_mask(2));
    write_data.pbits.pbyte_uenable = UINT64_MAX;
    write_data.pbits.pdata = wide(repeated_byte(value));
    settle();
    while (!port_out.prequester.prequest_udata.pready)
      maintenance_tick();
    maintenance_tick();
    write_data = {};
  }
  finish(0);
}

void run_case() {
  reset = 1;
  issue = {};
  write_data = {};
  for (int i = 0; i < 256; i++)
    environment.ram[i] = repeated_byte(UINT64_C(17));
  for (int i = 0; i < 4; i++) {
    environment.cached[i] = 0;
    environment.dirty[i] = 0;
    environment.cache_address[i] = 0;
  }
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    maintenance_tick();
  reset = 0;
  maintenance_tick();

  for (int accept_first = 0; accept_first < 2; accept_first++) {
    if (INCLUSIVE) {
      install(2, A, 0, UINT64_C(17));
      install(3, A, 0, UINT64_C(17));
    }
    environment.hold_snoops = 1;
    send(1, 8, A, 0);
    while (!port_out.prequester.psnoops.pvalid)
      maintenance_tick();
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      maintenance_tick();
    CHECK(port_out.prequester.psnoops.pbits.ptarget_uid == UINT64_C(2));
    if (accept_first != 0) {
      environment.hold_snoops = 0;
      settle();
      while (!port_in.prequester.psnoops.pready)
        maintenance_tick();
      maintenance_tick();
      CHECK(environment.snp_active);
    }
    reset = 1;
    maintenance_tick();
    environment.snp_active = 0;
    environment.snp_data = 0;
    for (int i = 0; i < 4; i++) {
      environment.cached[i] = 0;
      environment.dirty[i] = 0;
    }
    environment.hold_snoops = 0;
    reset = 0;
    maintenance_tick();
    if (INCLUSIVE)
      maintenance_tick();
    CHECK(port_out.prequester.prequests.pready &&
          !port_out.prequester.psnoops.pvalid);
  }

  install(3, A, 1, UINT64_C(34));
  send(3, 8, A, 1);
  finish(0);
  check_ram(A, UINT64_C(34));
  CHECK(environment.cached[3] && !environment.dirty[3]);

  install(2, A, 0, UINT64_C(34));
  send(1, 8, A, 0);
  finish(0);
  CHECK(environment.cached[2] && environment.cached[3]);

  install(2, A, 1, UINT64_C(51));
  send(3, 8, A, 0);
  finish(0);
  check_ram(A, UINT64_C(51));

  install(2, A, 0, UINT64_C(51));
  install(3, A, 1, UINT64_C(68));
  send(1, 9, A, 0);
  finish(0);
  check_ram(A, UINT64_C(68));
  CHECK(!environment.cached[2] && !environment.cached[3]);

  install(3, A, 1, UINT64_C(85));
  {
    int before_writes;
    before_writes = environment.writes;
    send(3, 10, A, 1);
    finish(0);
    CHECK(environment.writes == before_writes && !environment.cached[3]);
    check_ram(A, UINT64_C(68));
  }

  read_line(A, UINT64_C(68));
  read_line(B, UINT64_C(17));
  {
    int before_reads, before_writes;
    before_reads = environment.reads;
    before_writes = environment.writes;
    send(1, 9, C, 0);
    finish(0);
    CHECK(environment.reads == before_reads &&
          environment.writes == before_writes);
    read_line(A, UINT64_C(68));
    read_line(B, UINT64_C(17));
    if (INCLUSIVE)
      CHECK(environment.reads == before_reads);
  }

  write_line(A, UINT64_C(102));
  if (INCLUSIVE)
    check_ram(A, UINT64_C(68));
  send(1, 8, A, 0);
  finish(0);
  check_ram(A, UINT64_C(102));
  read_line(A, UINT64_C(102));
  write_line(A, UINT64_C(119));
  send(1, 9, A, 0);
  finish(0);
  check_ram(A, UINT64_C(119));
  {
    int before_reads;
    before_reads = environment.reads;
    read_line(A, UINT64_C(119));
    CHECK(environment.reads == before_reads + 1);
  }
  if (INCLUSIVE) {
    write_line(A, UINT64_C(136));
    send(1, 10, A, 0);
    finish(0);
    check_ram(A, UINT64_C(119));
    read_line(A, UINT64_C(119));
  }

  install(3, A, 1, UINT64_C(153));
  environment.write_error = 2;
  send(1, 8, A, 0);
  finish(2);
  check_ram(A, UINT64_C(119));
}

int main() { return run_test(run_case); }
