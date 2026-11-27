// SPDX-License-Identifier: Apache-2.0
#include "wide-l1d-contract.hpp"
#include "test-sites.h"
#include "../../../../../rheg/runtime/rheg.h"

void retry_response(unsigned opcode, unsigned txn, unsigned credit) {
  auto &r = chi_in.presponses;
  r.pbits = {}; r.pbits.popcode = opcode; r.pbits.ptxn_uid = txn;
  r.pbits.psrc_uid = 1; r.pbits.ptgt_uid = 3; r.pbits.ppcrd_utype = credit;
  r.pvalid = 1;
  until([] { return chi_out.presponses.pready; });
  tick_model(); r.pvalid = 0;
}

void check_overlapping_loads(const Line &a, const Line &b, bool reverse) {
  // Same bank parity but distinct sets. Four A waiters and two B waiters
  // must consume just two CHI requests, each retaining its own byte offset.
  constexpr unsigned other = base + 128;
  request(base, load, {}, 0xffff, true, 0x10);
  const auto ta = accept_request(base, 2);
  for (unsigned i = 1; i < 4; ++i)
    request(base + 16*i, load, {}, 0xffff, true, 0x10+i);
  request(other + 16, load, {}, 0xffff, true, 0x20);
  const auto tb = accept_request(other, 2);
  CHECK(ta != tb);
  request(other + 48, load, {}, 0xffff, true, 0x21);
  if (reverse) {
    // Untagged grants in the opposite order to tagged RetryAcks.
    retry_response(7, 0, 5); retry_response(7, 0, 6);
    retry_response(3, ta, 6); retry_response(3, tb, 5);
    until([] { return chi_out.prequests.pvalid; });
    CHECK(!chi_out.prequests.pbits.pallow_uretry && chi_out.prequests.pbits.ppcrd_utype == 6);
    CHECK(accept_request(base, 2) == ta);
    until([] { return chi_out.prequests.pvalid; });
    CHECK(!chi_out.prequests.pbits.pallow_uretry && chi_out.prequests.pbits.ppcrd_utype == 5);
    CHECK(accept_request(other, 2) == tb);
  }
  for (unsigned i : {2u, 0u, 3u, 1u})
    refill_packet(reverse ? tb : ta, i, 1, (reverse ? b : a)[i]);
  until([] { return chi_out.prequester_uresponses.pvalid; });
  CHECK(chi_out.prequester_uresponses.pbits.popcode == 2); // CompAck before the other line returns
  if (!reverse) refill_packet(tb, 1, 1, b[1]);
  for (unsigned i = 0; i < 8; ++i) {
    eval();
    CHECK(!core_out.pdrained && !chi_out.prequests.pvalid);
    if (reverse) CHECK(!core_out.presponse.pvalid);
    tick_model();
  }
  if (!reverse) {
    // B has written a partial packet but cannot install. A's waiters must
    // return the correct rows despite B's use of the same data SRAM port.
    for (unsigned i = 0; i < 4; ++i) response(a[i], 0x10+i);
    eval(); CHECK(!core_out.presponse.pvalid && !core_out.pdrained);
  }
  for (unsigned i : {1u, 3u, 0u, 2u})
    if (reverse || i != 1) refill_packet(reverse ? ta : tb, i, 1, (reverse ? a : b)[i]);
  if (reverse) for (unsigned i = 0; i < 4; ++i) response(a[i], 0x10+i);
  response(b[1], 0x20); response(b[3], 0x21);
  eval(); CHECK(core_out.pdrained && !chi_out.prequests.pvalid);
  const auto &graph = rheg::graph();
  std::set<unsigned> rows;
  for (const auto &[parent, child] : graph.edges) {
    if (child.site != FillSite) continue;
    const auto row = graph.field(child, "index").unsigned_value();
    CHECK(row < 4 || (row >= 8 && row < 12));
    CHECK(rows.insert(row).second);
    CHECK(graph.field(parent, "address").unsigned_value() == (row < 4 ? base : other));
  }
  CHECK(rows.size() == 8);
}

void check_demand_window(const Line &a, Line b) {
  constexpr unsigned dst = base + 64;
  request(dst, lr, {}, 0xff, false);
  refill(dst, 7, b);
  response(Row{{b[0].words[0], b[0].words[1], 0, 0}});

  // Retain the leader outside the FIFO. A same-line waiter at its head
  // prevents any following request from retiring until DAT is released.
  request(base, load, {}, 0xffff, true, 0);
  const auto txn = accept_request(base, 2);
  request(base + 16, load, {}, 0xffff, true, 1);
  for (unsigned i = 0; i < 8; ++i) {
    Row value{};
    const auto row = i % 4, lane = i / 4;
    b[row].words[lane] = value.words[lane] = 0xa5000000u + i;
    request(dst + 16*row, store, value, 0xf << (4*lane), true, 2+i);
  }
  auto check_full = [&](unsigned op) {
    auto &req = core_in.prequest;
    req.pbits.paccess = op; req.pvalid = virtual_lookup_in.pvalid = 1;
    virtual_lookup_in.pbits = req.pbits.paddress;
    eval(); CHECK(!core_out.prequest.pready);
    req.pvalid = virtual_lookup_in.pvalid = 0;
  };
  check_full(store); // Operand ring full; seven descriptor slots still free.
  for (unsigned i = 0; i < 7; ++i)
    request(dst + 16*(i%4), load, {}, 0xffff, true, 10+i);
  check_full(load); // Sixteen descriptors, across both physical ends of the ring.
  for (unsigned i = 0; i < 4; ++i) refill_packet(txn, i, 1, a[i]);
  response(a[0], 0); response(a[1], 1);
  for (unsigned i = 0; i < 8; ++i) response({}, 2+i);
  for (unsigned i = 0; i < 7; ++i) response(b[i%4], 10+i);
  until([] { return core_out.pdrained; });
  request(dst, store, a[0], 0xffff, true, 17); response({}, 17);
  request(dst, load, {}, 0xffff, true, 18); response(a[0], 18);
}

void check_invalidated_head(const Line &a, const Line &b) {
  constexpr unsigned cached = base+64, other = base+128;
  request(cached, load); refill(cached, 2, a); response(a[0]);
  request(base, load, {}, 0xffff, true, 1);
  const auto first = accept_request(base, 2);
  request(cached+16, load, {}, 0xffff, true, 2);
  request(other, load, {}, 0xffff, true, 3);
  const auto last = accept_request(other, 2); // The middle load was classified as a hit.
  auto &snp = chi_in.psnoops;
  snp.pbits = {}; snp.pbits.paddress = cached >> 3; snp.pbits.popcode = 9; // SnpCleanInvalid
  snp.pbits.psrc_uid = 1; snp.pbits.ptxn_uid = 0x53; snp.pvalid = 1;
  until([] { return chi_out.psnoops.pready; }); tick_model(); snp.pvalid = 0;
  until([] { return chi_out.prequester_uresponses.pvalid; });
  CHECK(chi_out.prequester_uresponses.pbits.ptxn_uid == 0x53);
  tick_model();
  for (unsigned i = 0; i < 4; ++i) refill_packet(first, i, 1, a[i]);
  response(a[0], 1);
  for (unsigned i = 0; i < 8; ++i) { eval(); CHECK(!core_out.presponse.pvalid); tick_model(); }
  for (unsigned i = 0; i < 4; ++i) refill_packet(last, i, 1, a[i]);
  refill(cached, 2, b); response(b[1], 2); response(a[0], 3);
}

void check_refill_forwarding(Line a, bool probe) {
  a[1].words[1] = 0x800000ab;
  request(base, load, {}, 0xffff, true, 1);
  const auto ta = accept_request(base, 2);
  request(base + 20, load, {}, 0xf0, false, 2, word);
  request(base + 32, load, {}, 0xffff, true, 3);
  for (unsigned i = 0; i < 8; ++i) tick_model();
  // An out-of-order waiter beat cannot pass the demand. It must be replayed.
  refill_packet(ta, 2, 1, a[2]);
  for (unsigned i = 0; i < 4; ++i) { eval(); CHECK(!core_out.presponse.pvalid); tick_model(); }
  refill_packet(ta, 0, 1, a[0]);
  response(a[0], 1); // No last DAT or CompAck yet.
  if (probe) {
    auto &snp = chi_in.psnoops;
    snp.pbits = {}; snp.pbits.paddress = base >> 3; snp.pbits.popcode = 3; // SnpOnce
    snp.pbits.psrc_uid = 1; snp.pbits.ptxn_uid = 0x56; snp.pvalid = 1;
    until([] { return chi_out.psnoops.pready; }); tick_model(); snp.pvalid = 0;
    until([] { return chi_out.prequester_uresponses.pvalid; });
    CHECK(chi_out.prequester_uresponses.pbits.ptxn_uid == 0x56);
    CHECK(chi_out.prequester_uresponses.pbits.presp == 0); // Invalid, not SharedClean.
    tick_model();
  }
  refill_packet(ta, 1, 1, a[1]);
  const Row scalar{{0x800000ab, 0xffffffff, 0, 0}};
  if (!probe) response(scalar, 2);
  for (unsigned i = 0; i < 5; ++i) {
    eval(); CHECK(!core_out.presponse.pvalid && !core_out.pdrained && !chi_out.prequester_uresponses.pvalid);
    tick_model();
  }
  refill_packet(ta, 3, 1, a[3]);
  if (probe) response(scalar, 2);
  response(a[2], 3);
  eval(); CHECK(core_out.pdrained && !core_out.presponse.pvalid);
  const auto &graph = rheg::graph();
  unsigned replies = 0;
  for (const auto &[ref, node] : graph.nodes) {
    if (ref.site != ReplySite) continue;
    const auto ctx = graph.field(ref, "context").unsigned_value();
    CHECK(ctx == ++replies);
    const bool forwarded = ctx == 1 || (ctx == 2 && !probe);
    unsigned parents = 0;
    for (const auto &[parent, child] : graph.edges) {
      if (child.site != ref.site || child.sequence != ref.sequence) continue;
      ++parents;
      CHECK(parent.site == (forwarded ? FillSite : ResolveSite));
      if (forwarded) CHECK(graph.field(parent, "index").unsigned_value() == ctx - 1);
      else CHECK(graph.field(parent, "address").unsigned_value() == base + (ctx == 2 ? 20 : 32));
    }
    CHECK(parents == 1);
  }
  CHECK(replies == 3);
}

void check_full_forward_responses(const Line &a, const Line &b) {
  request(base, load, {}, 0xffff, true, 1);
  const auto ta = accept_request(base, 2);
  request(base + 16, load, {}, 0xffff, true, 2);
  request(base + 32, load, {}, 0xffff, true, 3);
  for (unsigned i = 0; i < 8; ++i) tick_model();
  for (unsigned i = 0; i < 4; ++i) refill_packet(ta, i, 1, a[i]);
  // With both response slots occupied, CHI can still acknowledge and install.
  until([] { return chi_out.prequester_uresponses.pvalid; });
  CHECK(chi_out.prequester_uresponses.pbits.popcode == 2);
  tick_model();
  auto &snp = chi_in.psnoops;
  snp.pbits = {}; snp.pbits.paddress = base >> 3; snp.pbits.popcode = 9;
  snp.pbits.psrc_uid = 1; snp.pbits.ptxn_uid = 0x57; snp.pvalid = 1;
  until([] { return chi_out.psnoops.pready; }); tick_model(); snp.pvalid = 0;
  until([] { return chi_out.prequester_uresponses.pvalid; });
  CHECK(chi_out.prequester_uresponses.pbits.ptxn_uid == 0x57); tick_model();
  response(a[0], 1); response(a[1], 2);
  // The third waiter never received a row. Its lookup sees the invalidation.
  refill(base, 2, b); response(b[2], 3);
  until([] { return core_out.pdrained; });
  CHECK(!core_out.presponse.pvalid);
}

void check_mshr_reuse(const Line &a, const Line &b, bool b_first) {
  constexpr unsigned addr_b = base + 128, addr_c = base + 64;
  request(base + 16, load, {}, 0xffff, true, 0xa1);
  const auto ta = accept_request(base, 2);
  request(addr_b + 32, load, {}, 0xffff, true, 0xb2);
  const auto tb = accept_request(addr_b, 2);
  CHECK(ta != tb);
  request(addr_b + 16, load, {}, 0xffff, true, 0xb1);
  request(addr_c + 48, load, {}, 0xffff, true, 0xc3);
  for (unsigned i : {1u, 0u, 3u, 2u})
    refill_packet(b_first ? tb : ta, i, 1, (b_first ? b : a)[i]);
  const auto tc = accept_request(addr_c, 2);
  CHECK(tc == (b_first ? tb : ta));
  for (unsigned i : {2u, 3u, 0u, 1u})
    refill_packet(tc, i, 1, a[3-i]);
  for (unsigned i = 0; i < 8; ++i) {
    eval(); CHECK(!chi_out.prequests.pvalid && !core_out.pdrained);
    if (b_first) CHECK(!core_out.presponse.pvalid);
    tick_model();
  }
  for (unsigned i = 0; i < 4; ++i)
    refill_packet(b_first ? ta : tb, i, 1, (b_first ? a : b)[i]);
  response(a[1], 0xa1); response(b[2], 0xb2);
  response(b[1], 0xb1); response(a[0], 0xc3);
  eval(); CHECK(core_out.pdrained && !chi_out.prequests.pvalid);
}

void check_miss_barrier(const Line &a, const Line &b, bool reservation_barrier) {
  const unsigned middle = base + (reservation_barrier ? 64 : 256);
  constexpr unsigned last = base + 128;
  request(base, load, {}, 0xffff, true, 1);
  const auto ta = accept_request(base, 2);
  request(middle, reservation_barrier ? lr : load, {}, reservation_barrier ? 0xff : 0xffff, !reservation_barrier, 2);
  request(last + 32, load, {}, 0xffff, true, 3);
  // Neither LR nor a distinct tag in A's set may be skipped to reach C.
  for (unsigned i = 0; i < 12; ++i) {
    eval(); CHECK(!chi_out.prequests.pvalid && !core_out.presponse.pvalid);
    tick_model();
  }
  for (unsigned i = 0; i < 4; ++i) refill_packet(ta, i, 1, a[i]);
  response(a[0], 1);
  refill(middle, reservation_barrier ? 7 : 2, b);
  response(reservation_barrier ? Row{{b[0].words[0], b[0].words[1], 0, 0}} : b[0], 2);
  refill(last, 2, a); response(a[2], 3);
}

void check_store_miss_admission(Line a, Line b, bool invalidate) {
  constexpr unsigned addr_b = base+64, addr_c = base+128;
  const Row patch{{0x11112222, 0x33334444, 0x55556666, 0x77778888}};
  request(base, store, patch, 0x000f, true, 1);
  const auto ta = accept_request(base, 7);
  request(base, store, patch, 0x00f0, true, 2);
  request(base+16, store, patch, 0x0f00, true, 3);
  request(addr_b, store, patch, 0xf000, true, 4);
  const auto tb = accept_request(addr_b, 7);
  CHECK(ta != tb);
  request(addr_c, load, {}, 0xffff, true, 5);
  for (unsigned i : {1u, 3u, 2u, 0u}) refill_packet(tb, i, 2, b[i]);
  const auto tc = accept_request(addr_c, 2);
  CHECK(tc == tb && !core_out.presponse.pvalid);
  for (unsigned i = 0; i < 4; ++i) refill_packet(tc, i, 1, a[i]);
  for (unsigned i = 0; i < 10; ++i) {
    eval(); CHECK(!chi_out.prequests.pvalid && !core_out.presponse.pvalid && !core_out.pdrained);
    tick_model();
  }
  if (invalidate) {
    // SnpOnce proves B is resident and clean; SnpCleanInvalid then removes it.
    for (unsigned opcode : {3u, 9u}) {
      auto &snp = chi_in.psnoops;
      snp.pbits = {}; snp.pbits.paddress = addr_b >> 3; snp.pbits.popcode = opcode;
      snp.pbits.psrc_uid = 1; snp.pbits.ptxn_uid = 0x53; snp.pvalid = 1;
      until([] { return chi_out.psnoops.pready; }); tick_model(); snp.pvalid = 0;
      until([] { return chi_out.prequester_uresponses.pvalid; });
      const auto &rsp = chi_out.prequester_uresponses.pbits;
      CHECK(rsp.ptxn_uid == 0x53 && rsp.presp == (opcode == 3 ? 2 : 0));
      CHECK(!chi_out.prequest_udata.pvalid && !core_out.presponse.pvalid);
      tick_model();
    }
    b[0] = a[2]; // Reacquire different bytes supplied by the new owner.
  }
  for (unsigned i = 0; i < 4; ++i) refill_packet(ta, i, 2, a[i]);
  response({}, 1); response({}, 2); response({}, 3);
  if (invalidate) refill(addr_b, 7, b);
  response({}, 4); response(a[0], 5);
  a[0].words[0] = patch.words[0]; a[0].words[1] = patch.words[1];
  a[1].words[2] = patch.words[2]; b[0].words[3] = patch.words[3];
  snoop_dirty_line(base, a, 0x54);
  snoop_dirty_line(addr_b, b, 0x55);
  eval(); CHECK(!core_out.presponse.pvalid && !chi_out.prequests.pvalid && core_out.pdrained);
}

void check_store_head_under_refill(Line a, const Line &b, unsigned packet_delay) {
  constexpr unsigned other = base + 128;
  request(base, store, b[0], 0xffff, true, 1);
  const auto ta = accept_request(base, 7);
  request(base+16, store, b[1], 0x00f0, true, 2);
  request(base+16, load, {}, 0xffff, true, 3);
  request(other, load, {}, 0xffff, true, 4);
  const auto tb = accept_request(other, 2);
  CHECK(ta != tb);
  for (unsigned i = 0; i < 4; ++i) refill_packet(ta, i, 2, a[i]);
  for (unsigned i = 0; i < packet_delay; ++i) tick_model();
  refill_packet(tb, 1, 1, b[1]); // Compete for the SRAM; leave B incomplete.

  response({}, 1); response({}, 2);
  a[0] = b[0]; a[1].words[1] = b[1].words[1];
  response(a[1], 3);
  eval(); CHECK(!core_out.presponse.pvalid && !core_out.pdrained);
  for (unsigned i : {0u, 2u, 3u}) refill_packet(tb, i, 1, b[i]);
  response(b[0], 4);
  request(other+16, load); response(b[1]);
  snoop_dirty_line(base, a, 0x58);
}

void check_store_replay_permission(Line a, const Line &b, bool invalidate) {
  request(base, load);
  const auto warm = accept_request(base, 2);
  for (unsigned i = 0; i < 4; ++i) refill_packet(warm, i, 2, a[i]); // UniqueClean
  response(a[0]);
  request(base+64, load, {}, 0xffff, true, 1);
  const auto first = accept_request(base+64, 2);
  request(base, store, b[0], 0x000f, true, 2);
  request(base+128, load, {}, 0xffff, true, 3);
  const auto last = accept_request(base+128, 2); // Store has been classified.
  if (invalidate) {
    auto &snp = chi_in.psnoops;
    snp.pbits = {}; snp.pbits.paddress = base >> 3; snp.pbits.popcode = 9;
    snp.pbits.psrc_uid = 1; snp.pbits.ptxn_uid = 0x59; snp.pvalid = 1;
    until([] { return chi_out.psnoops.pready; }); tick_model(); snp.pvalid = 0;
    until([] { return chi_out.prequester_uresponses.pvalid; });
    CHECK(chi_out.prequester_uresponses.pbits.ptxn_uid == 0x59); tick_model();
  }
  for (unsigned i = 0; i < 4; ++i) refill_packet(first, i, 1, a[i]);
  response(a[0], 1);
  if (!invalidate) response({}, 2); // Mark the clean line dirty before last fills.
  for (unsigned i = 0; i < 8; ++i) {
    eval(); CHECK(!core_out.presponse.pvalid); tick_model();
  }
  for (unsigned i = 0; i < 4; ++i) refill_packet(last, i, 1, b[i]);
  if (invalidate) {
    a = b;
    refill(base, 7, a); response({}, 2);
  }
  response(b[0], 3);
  a[0].words[0] = b[0].words[0];
  snoop_dirty_line(base, a, 0x5a);
}

void check_store_behind_load(const Line &a, Line b, bool same_line) {
  const unsigned addr_b = same_line ? base : base+64;
  request(base, load, {}, 0xffff, true, 1);
  const auto ta = accept_request(base, 2);
  request(addr_b, store, a[1], 0x000f, true, 2);
  if (same_line) {
    for (unsigned i = 0; i < 10; ++i) {
      eval(); CHECK(!chi_out.prequests.pvalid); tick_model();
    }
  } else {
    refill(addr_b, 7, b);
    eval(); CHECK(!core_out.presponse.pvalid);
  }
  for (unsigned i = 0; i < 4; ++i) refill_packet(ta, i, 1, a[i]);
  response(a[0], 1);
  if (same_line) refill(addr_b, 7, b);
  response({}, 2);
  b[0].words[0] = a[1].words[0];
  request(addr_b, load); response(b[0]);
}

void check_shared_upgrade_barrier(const Line &a, const Line &b) {
  constexpr unsigned addr_b = base+64, addr_c = base+128;
  request(base, load); const auto ta = accept_request(base, 2);
  request(addr_b, load, {}, 0xffff, true, 1);
  const auto tb = accept_request(addr_b, 2);
  request(addr_b, store, a[0], 0xffff, true, 2);
  request(addr_c, load, {}, 0xffff, true, 3);
  // The clean acquisition for B does not cover B's store. Even after
  // B installs SharedClean, issue cannot pass the blocked upgrade.
  for (unsigned i = 0; i < 4; ++i) refill_packet(tb, i, 1, b[i]);
  for (unsigned i = 0; i < 16; ++i) {
    eval(); CHECK(!chi_out.prequests.pvalid && !core_out.presponse.pvalid); tick_model();
  }
  for (unsigned i = 0; i < 4; ++i) refill_packet(ta, i, 1, a[i]);
  response(a[0]); response(b[0], 1);
  refill(addr_b, 7, b);
  refill(addr_c, 2, b);
  response({}, 2); response(b[0], 3);
  request(addr_b, load); response(a[0]);
}

unsigned prepare_dirty_pair(const Line &a, const Line &b, unsigned mask = 0xffff) {
  reset = 1; tick_model(); reset = 0;
  for (auto off : {0u, 64u, 256u, 320u}) {
    const Line &line = off % 256 == 0 ? a : b;
    request(base+off, store, line[0]); refill(base+off, 7, line); response({});
  }
  request(base+512, store, b[1], mask, true, 1);
  return accept_request(base, 0x1b);
}

void grant_copyback(unsigned txn, unsigned dbid) {
  auto &rsp = chi_in.presponses;
  rsp.pbits = {}; rsp.pbits.popcode = 5; rsp.pbits.psrc_uid = 1;
  rsp.pbits.ptgt_uid = 3; rsp.pbits.ptxn_uid = txn; rsp.pbits.pdbid_uor_ugroup_uid = dbid;
  rsp.pvalid = 1; until([] { return chi_out.presponses.pready; }); tick_model(); rsp.pvalid = 0;
}

void drain_copyback(unsigned dbid, const Line &line, bool valid, unsigned first = 0) {
  for (unsigned i = first; i < 4; ++i) {
    auto &d = chi_out.prequest_udata;
    until([&] { return d.pvalid; });
    CHECK(d.pbits.popcode == 2 && d.pbits.ptxn_uid == dbid && d.pbits.pdata_uid == i);
    CHECK(d.pbits.pbyte_uenable == (valid ? 0xffff : 0));
    if (valid) CHECK(d.pbits.pdata == line[i]);
    chi_in.prequest_udata.pready = 1; tick_model(); chi_in.prequest_udata.pready = 0;
  }
}

std::pair<unsigned, unsigned> accept_buffered_pair() {
  bool saw_read = false, saw_write = false;
  unsigned refill_id = 0, wb_id = 0;
  for (int i = 0; i < 2; ++i) {
    until([] { return chi_out.prequests.pvalid; });
    if (chi_out.prequests.pbits.popcode == 2) {
      refill_id = accept_request(base+576, 2); saw_read = true;
    } else {
      wb_id = accept_request(base+64, 0x1b); saw_write = true;
    }
  }
  CHECK(saw_read && saw_write && refill_id != wb_id);
  return {refill_id, wb_id};
}

void check_buffered_replacement(const Line &a, const Line &b, bool invalidate_replacement) {
  const auto wa = prepare_dirty_pair(a, b);
  request(base+576+32, load, {}, 0xffff, true, 2);
  request(base+576+48, load, {}, 0xffff, true, 3);
  const auto [tb, wb] = accept_buffered_pair();
  // No copyback grant for either victim. All DATs and CompAck must pass.
  for (unsigned i : {3u, 1u, 0u, 2u}) refill_packet(tb, i, 1, a[i]);
  until([] { return chi_out.prequester_uresponses.pvalid; });
  CHECK(chi_out.prequester_uresponses.pbits.popcode == 2); tick_model();
  if (invalidate_replacement) {
    auto &snp = chi_in.psnoops;
    snp.pbits = {}; snp.pbits.paddress = (base+576) >> 3; snp.pbits.popcode = 9;
    snp.pbits.psrc_uid = 1; snp.pbits.ptxn_uid = 0x60; snp.pvalid = 1;
    until([] { return chi_out.psnoops.pready; }); tick_model(); snp.pvalid = 0;
    until([] { return chi_out.prequester_uresponses.pvalid; });
    CHECK(chi_out.prequester_uresponses.pbits.ptxn_uid == 0x60 && chi_out.prequester_uresponses.pbits.presp == 0);
    tick_model();
  } else {
    snoop_dirty_line(base+64, b, 0x53);
  }
  grant_copyback(wa, 0x51);
  const auto ta = accept_request(base+512, 7);
  drain_copyback(0x51, a, true);
  grant_copyback(wb, 0x52); drain_copyback(0x52, b, invalidate_replacement);
  for (unsigned i = 0; i < 16; ++i) { eval(); CHECK(!core_out.presponse.pvalid); tick_model(); }
  for (unsigned i = 0; i < 4; ++i) refill_packet(ta, i, 2, a[i]);
  response({}, 1);
  if (invalidate_replacement) refill(base+576, 2, b);
  const auto &expected = invalidate_replacement ? b : a;
  response(expected[2], 2); response(expected[3], 3);
  for (unsigned i = 0; i < 4; ++i) { request(base+576+16*i, load); response(expected[i]); }
}

void check_two_dirty_misses(const Line &a, const Line &b, bool probe_waiting_victim) {
  const auto wa = prepare_dirty_pair(a, b);
  request(base+576, store, a[1], 0xffff, true, 2);
  const auto wb = accept_request(base+64, 0x1b);
  CHECK(wb != wa);
  if (probe_waiting_victim) {
    retry_response(7, 0, 5);
    retry_response(3, wb, 5);
    until([] { return chi_out.prequests.pvalid; });
    CHECK(!chi_out.prequests.pbits.pallow_uretry && chi_out.prequests.pbits.ppcrd_utype == 5);
    CHECK(accept_request(base+64, 0x1b) == wb);
  }
  grant_copyback(wb, 0x52);
  const auto tb = accept_request(base+576, 7);
  // A's grant must not change the owner of B's stalled victim packet.
  if (!probe_waiting_victim) {
    until([] { return chi_out.prequest_udata.pvalid; });
    grant_copyback(wa, 0x51);
  }
  drain_copyback(0x52, b, true);
  for (unsigned i = 0; i < 4; ++i) refill_packet(tb, i, 2, b[i]);
  for (int i = 0; i < 8; ++i) { eval(); CHECK(!core_out.presponse.pvalid); tick_model(); }
  if (probe_waiting_victim) {
    snoop_dirty_line(base, a, 0x53);
    grant_copyback(wa, 0x51);
  }
  const auto ta = accept_request(base+512, 7);
  CHECK(ta != tb);
  drain_copyback(0x51, a, !probe_waiting_victim);
  for (unsigned i : {2u, 0u, 1u, 3u}) refill_packet(ta, i, 2, a[i]);
  response({}, 1); response({}, 2);
  request(base+512, load); response(b[1]);
  request(base+576, load); response(a[1]);
}

static void check_bank_overlap(const Line& a, const Line& b) {
  reset = 1; tick_model(); reset = 0;
  request(base, load); refill(base, 2, a); response(a[0]);
  request(base+64, load); refill(base+64, 2, b); response(b[0]);
  for (unsigned row : {0u, 1u}) {
    until([] { return core_out.pdrained; });
    auto &probe = pipeline_lookup_in;
    probe.pbits = {};
    probe.pbits.paddress = base+64+16*row; probe.pbits.paccess = load;
    probe.pbits.pwidth = doubleword; probe.pbits.praw_ubeat = 1;
    probe.pbits.pbyte_umask = 0xffff; probe.pvalid = 1;
    // Occupy the fresh lookup lane as A arrives. A must now use replay
    // alongside the next S0 lookup of B, not the fresh-head shortcut.
    request(base, load);
    tick_model(); probe.pvalid = 0;
    pipeline_in.prequest = probe; pipeline_in.prequest.pvalid = 1;
    eval();
    CHECK(pipeline_out.presponse.pvalid);
    CHECK(pipeline_out.presponse.pbits.poutcome == (row ? 1 : 3));
    if (row) CHECK(pipeline_out.presponse.pbits.pdata == b[row]);
    tick_model(); pipeline_in.prequest.pvalid = 0;
    response(a[0]);
  }
}

static void check_victim_refill(const Line& a, const Line& b, int first_row) {
  // In the held case, accept the entire refill behind a stalled victim DAT.
  // A writes only four bytes; its unmasked bytes and all victim bytes survive.
  const auto wa = prepare_dirty_pair(a, b, 0xf);
  request(base+576, load, {}, 0xffff, true, 2);
  const auto [tb, wb] = accept_buffered_pair();
  grant_copyback(wa, 0x51);
  const auto ta = accept_request(base+512, 7);
  drain_copyback(0x51, a, true);
  grant_copyback(wb, 0x52);
  until([] { return chi_out.prequest_udata.pvalid; });
  unsigned sent = 0;
  if (first_row >= 0) {
    refill_packet(ta, first_row, 2, a[first_row]);
    // Accept victim row 0, launching its SRAM read for row 1 on this edge.
    // There is room for a second refill beat only if the first can install.
    offer_packet(ta, 3, 2, a[3]);
    chi_in.prequest_udata.pready = 1;
    eval();
    CHECK(chi_out.prequest_udata.pvalid && chi_out.prequest_udata.pbits.pdata == b[0]);
    CHECK(bool(chi_out.presponse_udata.pready) == (first_row != 1));
    tick_model(); chi_in.prequest_udata.pready = 0;
    if (first_row == 1) {
      until([] { return chi_out.presponse_udata.pready; });
      tick_model();
    }
    chi_in.presponse_udata.pvalid = 0;
    sent = (1u << first_row) | (1u << 3);
  }
  const unsigned held = first_row < 0 ? 0 : 1;
  for (unsigned i : {1u, 0u, 3u, 2u}) {
    if (sent & (1u << i)) continue;
    offer_packet(ta, i, 2, a[i]);
    until([&] {
      const auto& dat = chi_out.prequest_udata;
      CHECK(dat.pvalid && dat.pbits.pdata_uid == held && dat.pbits.pdata == b[held]);
      return chi_out.presponse_udata.pready;
    });
    tick_model(); chi_in.presponse_udata.pvalid = 0;
  }
  drain_copyback(0x52, b, true, held);
  for (unsigned i = 0; i < 4; ++i) refill_packet(tb, i, 1, a[i]);
  response({}, 1); response(a[0], 2);
  Line expected = a;
  expected[0].words[0] = b[1].words[0];
  for (unsigned i = 0; i < 4; ++i) hit(base+512+16*i, load, expected[i]);
}

int main() {
  rheg::graph().bind_manifest(rheg_generated::manifest());
  return run_wide_l1d([](const Line& a, const Line& b) {
    reset = 1; tick_model(); reset = 0;
    for (bool reverse : {true, false}) {
      check_store_replay_permission(a, b, reverse);
      reset = 1; tick_model(); reset = 0;
      check_refill_forwarding(a, reverse);
      reset = 1; tick_model(); reset = 0;
      check_mshr_reuse(a, b, reverse);
      reset = 1; tick_model(); reset = 0;
      check_overlapping_loads(a, b, reverse);
      reset = 1; tick_model(); reset = 0;
      check_miss_barrier(a, b, reverse);
      reset = 1; tick_model(); reset = 0;
      check_store_miss_admission(a, b, reverse);
      check_two_dirty_misses(a, b, reverse);
      check_buffered_replacement(a, b, reverse);
      reset = 1; tick_model(); reset = 0;
      check_store_behind_load(a, b, reverse);
      reset = 1; tick_model(); reset = 0;
    }
    for (unsigned delay = 0; delay < 8; ++delay) {
      check_store_head_under_refill(a, b, delay);
      reset = 1; tick_model(); reset = 0;
    }
    check_full_forward_responses(a, b);
    reset = 1; tick_model(); reset = 0;
    check_demand_window(a, b);
    reset = 1; tick_model(); reset = 0;
    check_invalidated_head(a, b);
    reset = 1; tick_model(); reset = 0;
    check_shared_upgrade_barrier(a, b);
    reset = 1; tick_model(); reset = 0;
    check_bank_overlap(a, b);
    for (int first : {-1, 1, 2}) check_victim_refill(a, b, first);
  });
}
