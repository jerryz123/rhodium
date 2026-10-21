// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <deque>

using Descriptor = std::remove_cvref_t<decltype(request_in.pbits)>;
std::deque<Descriptor> pending;
unsigned serial = 0;

Descriptor descriptor(unsigned kind) {
  Descriptor value{};
  const unsigned id = ++serial;
  value.ppc = 0x80000000 + id * 4;
  const unsigned encoding = kind == 1 ? 0x02000007 : kind == 2 ? 0x02000027 : kind == 3 ? 0x02001057 : 0x02000057;
  value.pinstruction = encoding | (id % 32) << 7;
  value.pscalar = 0x90000000 + id * 4096;
  value.pfloating_uscalar = id * 17;
  value.pstride = id * 8;
  value.pstate.pvl = 1 + id % (kind == 3 ? 4 : 8);
  value.pstate.pvtype = kind == 3 ? 16 : (id % 2) * 8;
  value.pstate.pvstart = id % 2;
  value.pstate.pvxrm = id % 4;
  value.pstate.pvxsat = id % 2;
  value.pmasked = id % 2;
  if (value.pmasked) value.pinstruction &= ~(1u << 25);
  value.prounding_umode = id % 4;
  value.pcontrol.pinteger = kind == 0;
  value.pcontrol.pmemory = kind == 1 || kind == 2;
  value.pcontrol.pstore = kind == 2;
  value.pcontrol.pfloating_upoint = kind == 3;
  value.pprecertified = value.pcontrol.pmemory;
  value.pcertificate.pppn = value.pscalar >> 12;
  value.pcertificate.prange.pfirst = value.pscalar;
  value.pcertificate.prange.plast = value.pscalar + 4095;
  value.pcertificate.prange.pstore = value.pcontrol.pstore;
  value.pcertificate.pinvalidation_uepoch = id % 4;
  return value;
}

void check_descriptor(const Descriptor &got, const Descriptor &expected) {
  CHECK(got.ppc == expected.ppc && got.pinstruction == expected.pinstruction);
  CHECK(got.pscalar == expected.pscalar && got.pstride == expected.pstride);
  CHECK(got.pfloating_uscalar == expected.pfloating_uscalar);
  CHECK(got.pstate.pvl == expected.pstate.pvl && got.pstate.pvtype == expected.pstate.pvtype);
  CHECK(got.pstate.pvstart == expected.pstate.pvstart && got.pstate.pvxrm == expected.pstate.pvxrm);
  CHECK(got.pstate.pvxsat == expected.pstate.pvxsat && got.pmasked == expected.pmasked);
  CHECK(got.prounding_umode == expected.prounding_umode);
  CHECK(got.pcontrol.pmemory == expected.pcontrol.pmemory && got.pcontrol.pstore == expected.pcontrol.pstore);
  CHECK(got.pcontrol.pinteger == expected.pcontrol.pinteger && got.pcontrol.pfloating_upoint == expected.pcontrol.pfloating_upoint);
  CHECK(got.pprecertified == expected.pprecertified);
  CHECK(got.pcertificate.pppn == expected.pcertificate.pppn);
  CHECK(got.pcertificate.prange.pfirst == expected.pcertificate.prange.pfirst);
  CHECK(got.pcertificate.prange.plast == expected.pcertificate.prange.plast);
  CHECK(got.pcertificate.pinvalidation_uepoch == expected.pcertificate.pinvalidation_uepoch);
}

void step() {
  eval();
  CHECK(request_out.pready == (pending.size() < capacity));
  CHECK(available == request_out.pready);
  CHECK(dispatch_out.pvalid == (!pending.empty() || request_in.pvalid));
  CHECK(active == dispatch_out.pvalid && !certification_pending);
  bool load = false, store = false, fp = false;
  for (const auto &value : pending) {
    load |= value.pcontrol.pmemory && !value.pcontrol.pstore;
    store |= value.pcontrol.pmemory && value.pcontrol.pstore;
    fp |= value.pcontrol.pfloating_upoint;
  }
  CHECK(loads_pending == load && stores_pending == store && fp_pending == fp);
  CHECK(!precheck_out.prequest.pvalid);
  if (dispatch_out.pvalid) {
    const auto &expected = pending.empty() ? request_in.pbits : pending.front();
    CHECK(dispatch_out.pbits.pcontext == expected.ppc);
    check_descriptor(dispatch_out.pbits.pdescriptor, expected);
  }
  const bool push = request_in.pvalid && request_out.pready;
  const bool pop = dispatch_out.pvalid && dispatch_in.pready;
  if (push) pending.push_back(request_in.pbits);
  if (pop) pending.pop_front();
  tick_model();
}

void initialize() {
  reset = 1;
  request_in = {};
  dispatch_in = {};
  precise_in = {};
  precheck_in = {};
  sequencing_done_in = {};
  requests_idle = 1;
  sequencing = issue_valid = 0;
  tick_model();
  reset = 0;
  pending.clear();
  eval();
}

int main() {
  return run_test([] {
    initialize();
    // Every kind reaches the full capacity: four/eight must not wrap two-bit counters.
    for (unsigned kind = 0; kind < 4; ++kind) {
      dispatch_in.pready = 0;
      for (unsigned index = 0; index < capacity; ++index) {
        request_in = {1, descriptor(kind)};
        step();
      }
      request_in = {1, descriptor((kind + 1) % 4)};
      step();
      CHECK(pending.size() == capacity);
      // Registered credit rejects replacement at full occupancy even with a pop.
      dispatch_in.pready = 1;
      step();
      CHECK(pending.size() == capacity - 1);
      step();
      request_in.pvalid = 0;
      while (!pending.empty()) step();
      step();
    }
    // Empty bypass and mixed enqueue/dequeue wrap both pointers while inputs change.
    dispatch_in.pready = 1;
    request_in = {1, descriptor(0)};
    step();
    CHECK(pending.empty());
    for (unsigned cycle = 0; cycle < 400; ++cycle) {
      request_in = {cycle % 7 != 0, descriptor((cycle / 3) % 4)};
      dispatch_in.pready = cycle % 5 >= 2;
      step();
    }
    request_in.pvalid = 0;
    dispatch_in.pready = 1;
    while (!pending.empty()) step();
    step();
    // Reset discards every resident kind and restores all queue credits.
    dispatch_in.pready = 0;
    for (unsigned index = 0; index < capacity; ++index) {
      request_in = {1, descriptor(index % 4)};
      step();
    }
    initialize();
    step();
    // An uncertified memory descriptor still closes admission until its registered outcome.
    auto memory = descriptor(1);
    memory.pprecertified = 0;
    memory.pstate.pvtype = memory.pstate.pvstart = 0;
    memory.pstate.pvl = 1;
    memory.pmasked = 0;
    request_in = {1, memory};
    precheck_in.prequest.pready = 1;
    eval();
    CHECK(request_out.pready && precheck_out.prequest.pvalid);
    tick_model();
    request_in = {};
    eval();
    CHECK(!request_out.pready && certification_pending);
    CHECK(!dispatch_out.pvalid);
    precheck_in.presponse = {1, 1};
    eval();
    CHECK(precheck_out.presponse.pready);
    tick_model();
    precheck_in.presponse = {};
    eval();
    CHECK(outcome_out.pvalid && outcome_out.pbits.ppc == memory.ppc);
    CHECK(!request_out.pready && !certification_pending);
    tick_model();
    eval();
    CHECK(request_out.pready == (capacity > 1));
    CHECK(dispatch_out.pvalid);
    check_descriptor(dispatch_out.pbits.pdescriptor, memory);
    dispatch_in.pready = 1;
    tick_model();
    eval();
    CHECK(request_out.pready && !active && !loads_pending);
  });
}
