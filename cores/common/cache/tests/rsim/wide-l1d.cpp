// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

using Row = decltype(core_in.prequest.pbits.pdata);
using Line = std::array<Row, 4>;
constexpr unsigned load = 1, store = 2, lr = 3, sc = 4, amo = 5;
constexpr unsigned word = 2, doubleword = 3, atomic_swap = 0, atomic_add = 1;
constexpr unsigned base = 0x1000;

template <class F> void until(F done) {
  for (unsigned n = 0; n < 128; ++n) {
    eval();
    if (done()) return;
    tick_model();
  }
  CHECK(false && "L1D handshake timed out");
}

void request(std::uint64_t address, unsigned op, Row row = {}, unsigned mask = 0xffff, bool raw = true,
             unsigned context = 0x97, unsigned width = doubleword, unsigned atomic_op = atomic_swap) {
  auto &req = core_in.prequest;
  req.pbits = {};
  req.pbits.praw_ubeat = raw;
  req.pbits.paddress = address;
  req.pbits.paccess = op;
  req.pbits.pwidth = width;
  req.pbits.patomic = atomic_op;
  req.pbits.pbyte_umask = mask;
  req.pbits.pdata = row;
  req.pbits.pcontext = context;
  virtual_lookup_in.pvalid = 1;
  virtual_lookup_in.pbits = address;
  req.pvalid = 1;
  until([] { return core_out.prequest.pready; });
  tick_model();
  req.pvalid = 0;
  virtual_lookup_in.pvalid = 0;
}

unsigned accept_request(std::uint64_t address, unsigned opcode) {
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

void refill(std::uint64_t address, unsigned opcode, const Line &line) {
  const auto txn = accept_request(address, opcode);
  for (unsigned i : {3u, 1u, 0u, 2u})
    refill_packet(txn, i, opcode == 7 ? 2 : 1, line[i]);
}

void response(Row expected, unsigned context = 0x97, bool access_fault = false) {
  until([] { return core_out.presponse.pvalid; });
  for (int i = 0; i < 2; ++i) {
    CHECK(core_out.presponse.pvalid && core_out.presponse.pbits.pdata == expected);
    CHECK(core_out.presponse.pbits.paccess_ufault == access_fault && core_out.presponse.pbits.pcontext == context);
    tick_model(); eval();
  }
  core_in.presponse.pready = 1; tick_model(); core_in.presponse.pready = 0;
}

void check_consecutive_hits(unsigned address, const Line &line) {
  request(address, load, {}, 0xffff, true, 0x21);
  const auto first_accept = test_cycles;
  request(address+32, load, {}, 0xffff, true, 0x62);
  CHECK(test_cycles == first_accept + 1);
  core_in.presponse.pready = 1;
  until([] { return core_out.presponse.pvalid; });
  for (unsigned i : {0u, 2u}) {
    const auto &rsp = core_out.presponse;
    CHECK(rsp.pvalid && !rsp.pbits.paccess_ufault && rsp.pbits.pdata == line[i]);
    CHECK(rsp.pbits.pcontext == (i == 0 ? 0x21 : 0x62));
    CHECK(!chi_out.prequests.pvalid);
    tick_model(); eval();
  }
  CHECK(!core_out.presponse.pvalid);
  core_in.presponse.pready = 0;

  // Fill both response slots, leave a third hit stalled in S4, and queue a
  // fourth from a different row. Each hit must return its own full beat.
  for (unsigned i : {3u, 0u, 2u, 1u})
    request(address + 16*i, load, {}, 0xffff, true, 0x30+i);
  for (unsigned stall = 0; stall < 5; ++stall) {
    tick_model(); eval();
    CHECK(core_out.presponse.pvalid && core_out.presponse.pbits.pdata == line[3]);
    CHECK(!chi_out.prequests.pvalid);
  }
  for (unsigned i : {3u, 0u, 2u, 1u})
    response(line[i], 0x30+i);
}

void check_maintenance_retry(const Line &younger_line) {
  constexpr unsigned clean = 8, clean_shared = 8, retry_ack = 3, credit_grant = 7, comp = 4;
  constexpr auto clean_line = (UINT64_C(1) << 32) + base + 64;
  constexpr unsigned load_line = base + 128;

  // Bit 33 is inside CHI but outside data RAM. Dropping it before admission
  // would turn this rejected clean into a command for the mapped high line.
  auto &req = core_in.prequest;
  req.pbits = {};
  req.pbits.paddress = (UINT64_C(1) << 33) | (clean_line + 24);
  req.pbits.paccess = clean;
  req.pvalid = 1;
  for (unsigned cycle = 0; cycle < 4; ++cycle) {
    eval();
    CHECK(core_out.prequest_uaccess_ufault && !core_out.prequest.pready);
    CHECK(!chi_out.prequests.pvalid && !core_out.presponse.pvalid);
    tick_model();
  }
  req.pvalid = 0;
  eval(); CHECK(core_out.pdrained);

  request(clean_line + 24, clean, {}, 0, false, 0x59);
  req.pbits.paddress = base + 64 + 24;
  until([] { return chi_out.prequests.pvalid; });
  const auto txn = chi_out.prequests.pbits.ptxn_uid;
  auto accept_clean = [&](bool first_attempt) {
    const auto &offer = chi_out.prequests;
    until([&] { return offer.pvalid; });
    for (unsigned cycle = 0; cycle < 4; ++cycle) {
      CHECK(offer.pvalid && offer.pbits.paddress == clean_line && offer.pbits.popcode == clean_shared);
      CHECK(offer.pbits.ptgt_uid == 1 && offer.pbits.ptxn_uid == txn);
      CHECK(offer.pbits.pallow_uretry == first_attempt && !core_out.presponse.pvalid);
      if (!first_attempt) CHECK(offer.pbits.ppcrd_utype == 5);
      tick_model(); eval();
    }
    CHECK(accept_request(clean_line, clean_shared) == txn);
  };
  accept_clean(true);
  request(load_line + 32, load, {}, 0xffff, true, 0xa6);

  auto send_rsp = [&](unsigned opcode, unsigned id, unsigned credit_type, unsigned error) {
    auto &rsp = chi_in.presponses;
    rsp.pbits = {};
    rsp.pbits.psrc_uid = 1;
    rsp.pbits.ptgt_uid = 3;
    rsp.pbits.ptxn_uid = id;
    rsp.pbits.popcode = opcode;
    rsp.pbits.ppcrd_utype = credit_type;
    rsp.pbits.presp_uerr = error;
    rsp.pvalid = 1;
    until([] { return chi_out.presponses.pready; });
    tick_model(); rsp.pvalid = 0;
  };
  send_rsp(retry_ack, txn, 5, 0);
  send_rsp(credit_grant, 0, 5, 0);
  accept_clean(false);
  send_rsp(comp, txn, 0, 2);

  // Keep the failed clean at the response head while the new miss takes ownership.
  until([] { return core_out.presponse.pvalid; });
  refill(load_line, 2, younger_line);
  response({}, 0x59, true);
  response(younger_line[2], 0xa6);
}

void hit(std::uint64_t address, unsigned op, Row value, unsigned mask = 0xffff, bool raw = true, bool commit = true) {
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

void check_backing_handoff(const Line &victim, Line probed) {
  reset = 1; tick_model(); reset = 0;
  Line replacement{};
  for (unsigned i = 0; i < 4; ++i)
    for (unsigned j = 0; j < 4; ++j)
      replacement[i].words[j] = 0xc0010000 + 4*i + j;

  request(base, store, victim[0]); refill(base, 7, victim); response({});
  request(base+256, store, probed[0]); refill(base+256, 7, probed); response({});
  request(base+64, load); refill(base+64, 2, replacement); response(replacement[0]);

  // Queue a miss behind an owned AMOSWAP.D without accepting its response.
  // The AMO changes way one; the miss must evict way zero and keep its own context.
  Row old{{probed[1].words[2], probed[1].words[3], 0, 0}};
  request(base+256+24, amo, replacement[0], 0xff00, false, 0x41);
  probed[1].words[2] = replacement[0].words[0];
  probed[1].words[3] = replacement[0].words[1];
  request(base+512+32, load, {}, 0xffff, true, 0xb2);
  auto &rsp = chi_in.presponses;
  rsp.pbits = {};
  rsp.pbits.ptxn_uid = accept_request(base, 0x1b); // WriteBackFull
  // While copyback waits for its grant, the other set can still hit.
  hit(base+64+48, load, replacement[3]);
  response(old, 0x41);
  // A queued load to a different row must not replace the active miss request.
  request(base+512+48, load, {}, 0xffff, true, 0xd3);
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
  response(replacement[2], 0xb2);
  response(replacement[3], 0xd3);
  for (unsigned i = 0; i < 4; ++i)
    hit(base+512+i*16, load, replacement[i]);
}

void check_retained_address(const Line &low, const Line &high) {
  constexpr auto ram = (UINT64_C(1) << 32) | base;
  constexpr auto alias = (UINT64_C(1) << 33) | base; // Within CHI, beyond data RAM.
  for (bool matching_probe : {false, true}) {
    reset = 1; tick_model(); reset = 0;
    // No demand, reservation or resident line can suppress this cold hint.
    prefetch_in.pbits.poperation = 2;
    prefetch_in.pbits.paddress = alias + 64;
    prefetch_in.pvalid = 1; tick_model(); prefetch_in.pvalid = 0;
    for (unsigned cycle = 0; cycle < 8; ++cycle) {
      eval(); CHECK(!chi_out.prequests.pvalid && core_out.pdrained);
      tick_model();
    }

    request(base, lr, {}, 0xff, false);
    refill(base, 7, low);
    response(Row{{low[0].words[0], low[0].words[1], 0, 0}});
    CHECK(core_out.preservation_uvalid);

    auto &req = core_in.prequest;
    req.pbits.paddress = alias;
    req.pbits.paccess = sc;
    req.pvalid = 1;
    eval();
    CHECK(core_out.prequest_uaccess_ufault && !core_out.prequest.pready);
    tick_model(); req.pvalid = 0;
    for (unsigned cycle = 0; cycle < 140; ++cycle) {
      eval(); CHECK(!chi_out.prequests.pvalid && !core_out.presponse.pvalid && core_out.preservation_uvalid);
      tick_model();
    }

    auto &snp = chi_in.psnoops;
    snp.pbits = {};
    snp.pbits.paddress = (matching_probe ? base : alias) >> 3;
    snp.pbits.popcode = 9; // SnpCleanInvalid
    snp.pbits.psrc_uid = 1; snp.pbits.ptxn_uid = 0x37;
    snp.pvalid = 1;
    until([] { return chi_out.psnoops.pready; }); tick_model(); snp.pvalid = 0;
    until([] { return chi_out.prequester_uresponses.pvalid; });
    CHECK(chi_out.prequester_uresponses.pbits.ptxn_uid == 0x37);
    tick_model();
    request(base, sc, {}, 0xff, false);
    response(Row{{unsigned(matching_probe), 0, 0, 0}});
    CHECK(!core_out.preservation_uvalid);
  }

  // Hold a mapped high refill at REQ while the live input names the low alias.
  request(ram + 32, load);
  core_in.prequest.pbits.paddress = base + 32;
  until([] { return chi_out.prequests.pvalid; });
  for (unsigned cycle = 0; cycle < 5; ++cycle) {
    CHECK(chi_out.prequests.pvalid && chi_out.prequests.pbits.paddress == ram);
    tick_model(); eval();
  }
  refill(ram, 2, high);
  response(high[2]);
  request(ram + 48, load); response(high[3]);
}

void check_high_store_rows(const Line &low, const Line &high) {
  reset = 1; tick_model(); reset = 0;
  constexpr auto ram = (UINT64_C(1) << 32) | base;
  // Own both lines in the same set, with only the low line reserved.
  request(ram, lr, {}, 0xff, false); refill(ram, 7, high);
  response(Row{{high[0].words[0], high[0].words[1], 0, 0}});
  request(base, lr, {}, 0xff, false); refill(base, 7, low);
  response(Row{{low[0].words[0], low[0].words[1], 0, 0}});
  hit(ram + 48, store, high[1]);
  CHECK(core_out.preservation_uvalid);
  hit(base + 48, load, low[3]);
  hit(ram + 48, load, high[1]);

  // The same buffered store must clear the high reservation on enqueue.
  request(ram, lr, {}, 0xff, false);
  response(Row{{high[0].words[0], high[0].words[1], 0, 0}});
  CHECK(core_out.preservation_uvalid);
  hit(ram + 48, store, high[2]);
  CHECK(!core_out.preservation_uvalid);
  hit(ram + 48, load, high[2]);
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
    check_consecutive_hits(base + 256, b);
    check_maintenance_retry(a);
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
    // AMOADD.W in both halves of an XLEN word must sign-extend the old value
    // and update only its four byte lanes, even while responses are stalled.
    a[1] = Row{{0x80000001, 0xf0000002, 0x13579bdf, 0x2468ace0}};
    hit(base + 16, store, a[1]);
    for (unsigned half : {0u, 1u}) {
      const auto old = a[1].words[half];
      request(base + 16 + 4*half, amo, Row{{3, 0, 0, 0}}, 0xf << (4*half),
              false, 0x80+half, word, atomic_add);
      response(Row{{old, 0xffffffff, 0, 0}}, 0x80+half);
      a[1].words[half] += 3;
      hit(base + 16, load, a[1]);
    }
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
    check_retained_address(a, b);
    check_high_store_rows(a, b);
  });
}
