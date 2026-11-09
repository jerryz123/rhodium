// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

using Row = decltype(core_in.prequest.pbits.pdata);
using Line = std::array<Row, 4>;
constexpr unsigned load = 1, store = 2, lr = 3, sc = 4, amo = 5;
constexpr unsigned base = 0x1000;

template <class F> void until(F done) {
  for (unsigned n = 0; n < 128; ++n) {
    eval();
    if (done()) return;
    tick_model();
  }
  CHECK(false && "L1D handshake timed out");
}

void request(unsigned address, unsigned op, Row row = {}, unsigned mask = 0xffff, bool raw = true) {
  auto &req = core_in.prequest;
  req.pbits = {};
  req.pbits.praw_ubeat = raw;
  req.pbits.paddress = address;
  req.pbits.paccess = op;
  req.pbits.pwidth = 3;
  req.pbits.pbyte_umask = mask;
  req.pbits.pdata = row;
  req.pbits.pcontext = 0x97;
  virtual_lookup_in.pvalid = 1;
  virtual_lookup_in.pbits = address;
  req.pvalid = 1;
  until([] { return core_out.prequest.pready; });
  tick_model();
  req.pvalid = 0;
  virtual_lookup_in.pvalid = 0;
}

unsigned accept_request(unsigned address, unsigned opcode) {
  until([] { return chi_out.prequests.pvalid; });
  CHECK(chi_out.prequests.pbits.paddress == address && chi_out.prequests.pbits.popcode == opcode);
  const auto txn = chi_out.prequests.pbits.ptxn_uid;
  chi_in.prequests.pready = 1; tick_model(); chi_in.prequests.pready = 0;
  return txn;
}

void refill_packet(unsigned txn, unsigned i, unsigned resp, Row row) {
  auto &dat = chi_in.presponse_udata;
  dat.pbits = {};
  dat.pbits.popcode = 4; // CompData
  dat.pbits.pdata_uid = i;
  dat.pbits.presp = resp;
  dat.pbits.ptxn_uid = txn;
  dat.pbits.phome_unid_uor_upbha_uor_umismatched_umecid = 1;
  dat.pbits.psrc_uid = 1;
  dat.pbits.ptgt_uid = 3;
  dat.pbits.pbyte_uenable = 0xffff;
  dat.pbits.pdata = row;
  dat.pvalid = 1;
  until([] { return chi_out.presponse_udata.pready; });
  tick_model(); dat.pvalid = 0;
}

void refill(unsigned address, unsigned opcode, const Line &line) {
  const auto txn = accept_request(address, opcode);
  for (unsigned i : {3u, 1u, 0u, 2u})
    refill_packet(txn, i, opcode == 7 ? 2 : 1, line[i]);
}

void response(Row expected) {
  until([] { return core_out.presponse.pvalid; });
  for (int i = 0; i < 2; ++i) {
    CHECK(core_out.presponse.pvalid && core_out.presponse.pbits.pdata == expected);
    CHECK(!core_out.presponse.pbits.paccess_ufault && core_out.presponse.pbits.pcontext == 0x97);
    tick_model(); eval();
  }
  core_in.presponse.pready = 1; tick_model(); core_in.presponse.pready = 0;
}

void hit(unsigned address, unsigned op, Row value, unsigned mask = 0xffff, bool raw = true, bool commit = true) {
  auto &lookup = pipeline_lookup_in;
  lookup.pbits = {};
  lookup.pbits.paddress = address;
  lookup.pbits.paccess = op;
  lookup.pbits.pwidth = 3;
  lookup.pbits.praw_ubeat = raw;
  lookup.pbits.pbyte_umask = mask;
  lookup.pbits.pdata = value;
  lookup.pvalid = 1;
  tick_model(); lookup.pvalid = 0;
  pipeline_in.prequest = lookup; pipeline_in.prequest.pvalid = 1;
  eval(); CHECK(pipeline_out.presponse.pvalid);
  CHECK(pipeline_out.presponse.pbits.poutcome == (op == load ? 1 : 2));
  if (op == load) CHECK(pipeline_out.presponse.pbits.pdata == value);
  tick_model(); pipeline_in.prequest.pvalid = 0;
  if (op == store && commit) {
    pipeline_in.pcommit = 1;
    eval(); CHECK(pipeline_out.pcommit_uready);
    tick_model(); pipeline_in.pcommit = 0;
    until([] { return core_out.pdrained; });
  }
}

void check_backing_handoff(const Line &victim, const Line &probed) {
  reset = 1; tick_model(); reset = 0;
  Line replacement{};
  for (unsigned i = 0; i < 4; ++i)
    for (unsigned j = 0; j < 4; ++j)
      replacement[i].words[j] = 0xc0010000 + 4*i + j;

  request(base, store, victim[0]); refill(base, 7, victim); response({});
  request(base+256, store, probed[0]); refill(base+256, 7, probed); response({});
  request(base+512+32, load);
  auto &rsp = chi_in.presponses;
  rsp.pbits = {};
  rsp.pbits.ptxn_uid = accept_request(base, 0x1b); // WriteBackFull
  rsp.pbits.popcode = 5; // CompDBIDResp
  rsp.pbits.psrc_uid = 1;
  rsp.pbits.ptgt_uid = 3;
  rsp.pbits.pdbid_uor_ugroup_uid = 0x55;
  rsp.pvalid = 1;
  until([] { return chi_out.presponses.pready; });
  tick_model(); rsp.pvalid = 0;

  const auto &dat = chi_out.prequest_udata;
  auto drain_line = [&](const Line &line, unsigned op, unsigned txn) {
    for (unsigned i = 0; i < 4; ++i) {
      until([&] { return dat.pvalid; });
      for (unsigned stall = 0; stall < 3; ++stall) {
        CHECK(dat.pvalid && dat.pbits.popcode == op && dat.pbits.ptxn_uid == txn);
        CHECK(dat.pbits.pbyte_uenable == 0xffff && dat.pbits.pdata_uid == i && dat.pbits.pdata == line[i]);
        CHECK(!chi_out.prequests.pvalid);
        tick_model(); eval();
      }
      chi_in.prequest_udata.pready = 1; tick_model(); chi_in.prequest_udata.pready = 0;
    }
  };
  drain_line(victim, 2, 0x55);
  const auto txn = accept_request(base+512, 2); // ReadClean
  refill_packet(txn, 3, 1, replacement[3]);

  auto &snp = chi_in.psnoops;
  snp.pbits = {};
  snp.pbits.paddress = (base+256) >> 3; snp.pbits.popcode = 9;
  snp.pbits.psrc_uid = 1; snp.pbits.ptxn_uid = 0x32; snp.pvalid = 1;
  until([] { return chi_out.psnoops.pready; }); tick_model(); snp.pvalid = 0;
  until([&] { return dat.pvalid; });
  chi_in.prequester_uresponses.pready = 0;
  for (unsigned i : {1u, 0u, 2u}) {
    CHECK(dat.pvalid && dat.pbits.pdata == probed[0]);
    refill_packet(txn, i, 1, replacement[i]);
    eval();
  }
  until([] { return chi_out.prequester_uresponses.pvalid; });
  CHECK(!core_out.presponse.pvalid);
  chi_in.prequester_uresponses.pready = 1; tick_model();
  drain_line(probed, 1, 0x32);
  response(replacement[2]);
  for (unsigned i = 0; i < 4; ++i)
    hit(base+512+i*16, load, replacement[i]);
}

int main() {
  return run_test([] {
    reset = 1; tick_model(); reset = 0;
    node_id = 3;
    chi_in.prequester_uresponses.pready = 1;
    Line a{}, b{};
    for (unsigned i = 0; i < 4; ++i)
      for (unsigned j = 0; j < 4; ++j) {
        a[i].words[j] = 0x01020304u * (1 + i * 4 + j);
        b[i].words[j] = ~a[i].words[j];
      }
    // Read a whole row on a miss. Retain the result under backpressure.
    request(base + 256 + 48, load);
    refill(base + 256, 2, b); response(b[3]);
    // Allocate the same set's other way with a write-miss merge.
    Row patch{{0xaabbccdd, 0x11223344, 0x55667788, 0xdeadbeef}};
    request(base + 32, store, patch, 0xf00f); refill(base, 7, a); response({});
    a[2].words[0] = patch.words[0]; a[2].words[3] = patch.words[3];
    for (unsigned i = 0; i < 4; ++i) {
      hit(base + i*16, load, a[i]);
      hit(base + 256 + i*16, load, b[i]);
    }
    hit(base, store, patch); a[0] = patch;
    hit(base, store, b[0], 0xffff, true, false); // No WB authorization.
    hit(base, load, a[0]);
    // Scalar SD in the high half must leave the low half alone.
    hit(base + 8, store, patch, 0xff00, false);
    a[0].words[2] = patch.words[0]; a[0].words[3] = patch.words[1];
    hit(base, load, a[0]);
    hit(base + 8, load, Row{{patch.words[0], patch.words[1], 0, 0}}, 0xff00, false);
    request(base + 16, store, patch); response({}); a[1] = patch;
    request(base + 16, load); response(a[1]);
    // AMOSWAP.D and LR/SC remain scalar and affect only their eight lanes.
    Row high{{a[3].words[2], a[3].words[3], 0, 0}};
    request(base + 56, amo, patch, 0xff00, false); response(high);
    request(base + 56, lr, {}, 0xff00, false);
    response(Row{{patch.words[0], patch.words[1], 0, 0}});
    request(base + 56, sc, b[0], 0xff00, false); response({});
    request(base + 56, sc, {}, 0xff00, false); response(Row{{1, 0, 0, 0}});
    a[3].words[2] = b[0].words[0]; a[3].words[3] = b[0].words[1];
    hit(base + 48, load, a[3]);
    // SnpCleanInvalid returns every byte of the dirty line, not duplicated low halves.
    auto &snp = chi_in.psnoops;
    snp.pbits.paddress = base >> 3; snp.pbits.popcode = 9; snp.pbits.psrc_uid = 1;
    snp.pbits.ptxn_uid = 0x32; snp.pvalid = 1;
    until([] { return chi_out.psnoops.pready; }); tick_model(); snp.pvalid = 0;
    for (unsigned i = 0; i < 4; ++i) {
      until([] { return chi_out.prequest_udata.pvalid; });
      auto &dat = chi_out.prequest_udata.pbits;
      CHECK(dat.popcode == 1 && dat.ptxn_uid == 0x32 && dat.pdata_uid == i);
      CHECK(dat.pbyte_uenable == 0xffff && dat.pdata == a[i]);
      chi_in.prequest_udata.pready = 1; tick_model(); chi_in.prequest_udata.pready = 0;
    }
    check_backing_handoff(a, b);
  });
}
