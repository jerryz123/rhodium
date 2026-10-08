// Preserves the rv5stage-mmu-replay cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
void tick();
using data_req_bits_t = std::remove_cvref_t<decltype(data_in.prequest.pbits)>;
// Verifies MMU walks, faults, prefetches, vector windows, and split slow accesses.

constexpr std::uint8_t PRIVILEGE_U = UINT64_C(0);
constexpr std::uint8_t PRIVILEGE_S = UINT64_C(1);
constexpr std::uint8_t MEMORY_LOAD = UINT64_C(1);
constexpr std::uint8_t MEMORY_DOUBLE = UINT64_C(3);
constexpr std::uint8_t WRITEBACK_INTEGER_KIND = UINT64_C(1);
constexpr std::uint64_t VIRTUAL_ADDRESS = UINT64_C(16384);
constexpr std::uint64_t FAULT_VIRTUAL_ADDRESS = UINT64_C(32768);
constexpr std::uint64_t PHYSICAL_ADDRESS = UINT64_C(32768);
constexpr std::uint64_t SATP_SV39_ROOT_1 = UINT64_C(0x8000000000000001);
constexpr std::uint64_t LEVEL_2_POINTER = UINT64_C(2049);
constexpr std::uint64_t LEVEL_1_POINTER = UINT64_C(3073);
constexpr std::uint64_t LEVEL_0_LEAF = UINT64_C(8263);

// Defines the RV64 EX/MEM/WB cache protocol used by the direct cache and MMU benches.

constexpr std::uint8_t PIPE_SLOW = 0, PIPE_LOAD_HIT = 1, PIPE_STORE_HIT = 2,
                       PIPE_REPLAY = 3, PIPE_PAGE_FAULT = 4,
                       PIPE_ACCESS_FAULT = 5;

bool instruction_flush;

bool data_request_valid;

bool instruction_request_valid = UINT64_C(0);

std::uint64_t instruction_address = UINT64_C(20480);

bool pte_response_valid;

std::uint64_t pte_response_data;

std::uint8_t pte_requests;

bool translated_request_seen;

bool page_fault_phase;

bool zero_request = UINT64_C(0);

std::uint8_t management_operation = 0;

bool page_fault_pte_seen;

bool memory_ready = UINT64_C(0);

bool memory_idle = UINT64_C(0);

bool ordinary_response_valid = UINT64_C(0);

data_req_bits_t stalled_request;

bool instruction_phase = UINT64_C(0);

bool instruction_blocked = UINT64_C(0);

bool instruction_return_valid = UINT64_C(0);

bool instruction_return_replay = UINT64_C(0);

std::uint32_t instruction_return_word;

int instruction_requests_seen = 0;

int instruction_responses_seen = 0;

int instruction_physical_responses_seen = 0;

bool instruction_fault_expected = 0;

bool instruction_translation_phase = UINT64_C(0);

int instruction_pte_requests = 0;

bool detached_walk_phase = 0;

bool manual_pte_valid = 0;

std::uint64_t manual_pte_data = 0;

int manual_pte_requests = 0;

bool vector_phase = 0, vector_superpage = 0, vector_bad_second = 0,
     vector_no_dirty = 0;

bool vector_napot = 0;

std::uint8_t vector_pbmt = 0;

std::uint64_t napot_fetch_expected = 0;

std::uint64_t vector_scalar_address = 0;

int vector_pte_requests = 0;

bool priority_phase = 0, priority_pipeline_slow = 0;

int priority_core_requests = 0, priority_pte_requests = 0;

bool split_phase = 0, split_store = 0, split_cross_page = 0,
     split_contained = 0;

bool split_response_valid = 0;

std::uint64_t split_response_data = 0;

int split_requests_seen = 0;

int split_completions_seen = 0;

void check_load_pipeline(std::uint64_t address, std::uint8_t expected_hit,
                         std::uint64_t physical_address = 0,
                         std::uint8_t expected_outcome = PIPE_LOAD_HIT,
                         std::uint8_t operation = MEMORY_LOAD) {
  falling();
  pipeline_in.prequest = {
      .pvalid = UINT64_C(1),
      .pbits = {.pbyte_umask = std::uint8_t(0xff << (address % 8)),
                .paddress = address,
                .paccess = operation,
                .pwidth = UINT64_C(3),
                .punsigned = UINT64_C(0),
                .pdata = {}}};
  settle();
  CHECK(pipeline_lookup_out.pvalid &&
        pipeline_lookup_out.pbits.paddress == address &&
        !pipeline_memory_out.prequest.pvalid);
  tick();
  CHECK(pipeline_memory_out.prequest.pvalid == expected_hit &&
        pipeline_out.presponse.pvalid &&
        (pipeline_out.presponse.pbits.poutcome == PIPE_LOAD_HIT) ==
            expected_hit);
  CHECK(pipeline_out.presponse.pbits.poutcome == expected_outcome);
  if (expected_hit)
    CHECK(pipeline_memory_out.prequest.pbits.paddress == physical_address &&
          pipeline_out.presponse.pbits.pdata == UINT64_C(0x123456789abcdef0));
  falling();
  pipeline_in = {};
  settle();
  if (expected_hit)
    CHECK(pipeline_memory_out.prequest.pbits.paddress == physical_address);
  tick();
  CHECK(!pipeline_memory_out.prequest.pvalid &&
        !data_memory_out.prequest.pvalid);
}

void tick() {
  rising();
  settle();
}

void release_window() {
  falling();
  vector_precheck_in.prelease.pvalid = 1;
  tick();
  falling();
  vector_precheck_in.prelease.pvalid = 0;
}

void clear_translations() {
  falling();
  invalidate_all = 1;
  tick();
  falling();
  invalidate_all = 0;
}

void certify_range(std::uint64_t first_address, std::uint64_t last_address,
                   std::uint8_t store, std::uint8_t expected_safe,
                   int expected_ptes, std::uint8_t expect_fast_hit = 0) {
  int before_ptes;
  before_ptes = vector_pte_requests;
  falling();
  vector_precheck_in.prequest = {.pvalid = 1,
                                 .pbits = {.pfirst = first_address,
                                           .plast = last_address,
                                           .pstore = store}};
  tick();
  if (expect_fast_hit)
    CHECK(vector_precheck_out.presponse.pvalid);
  while (!vector_precheck_out.presponse.pvalid &&
         !vector_precheck_out.prequest.pready)
    tick();
  falling();
  vector_precheck_in.prequest.pvalid = 0;
  until([&] { return vector_precheck_out.presponse.pvalid; });
  for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
    tick();
    CHECK(vector_precheck_out.presponse.pvalid &&
          vector_precheck_out.presponse.pbits == expected_safe);
  }
  CHECK(vector_pte_requests - before_ptes == expected_ptes &&
        !data_out.prequest_ufault && !data_out.prequest_uaccess_ufault);
  falling();
  vector_precheck_in.presponse.pready = 1;
  tick();
  falling();
  vector_precheck_in.presponse.pready = 0;
}

void certify_contended_hit() {
  int before_ptes;
  before_ptes = vector_pte_requests;
  falling();
  vector_scalar_address = UINT64_C(16384);
  data_request_valid = 1;
  vector_precheck_in.prequest = {.pvalid = 1,
                                 .pbits = {.pfirst = UINT64_C(16384),
                                           .plast = UINT64_C(16639),
                                           .pstore = 0}};
  tick();
  CHECK(!vector_precheck_out.presponse.pvalid);
  falling();
  data_request_valid = 0;
  vector_precheck_in.prequest.pvalid = 0;
  tick();
  CHECK(vector_precheck_out.presponse.pvalid &&
        vector_precheck_out.presponse.pbits &&
        vector_pte_requests == before_ptes);
  falling();
  vector_precheck_in.presponse.pready = 1;
  tick();
  falling();
  vector_precheck_in.presponse.pready = 0;
}

// Model frontend-owned replay explicitly. The MMU never reissues S0 itself.
void fetch_word(std::uint64_t address) {
  bool complete;
  complete = 0;
  for (int attempts = 0; !complete && attempts < 1000; attempts++) {
    falling();
    instruction_address = address;
    instruction_request_valid = 1;
    settle();
    CHECK(instruction_out.prequest.pready);
    tick();
    instruction_request_valid = 0;
    tick();
    CHECK(instruction_out.presponse.pvalid);
    complete = !instruction_out.presponse.pbits.preplay;
    tick();
  }
  CHECK(complete);
}

void flush_fetch() {
  falling();
  instruction_flush = 1;
  tick();
  falling();
  instruction_flush = 0;
}

// Drive PTE timing explicitly, so a replay can land before acceptance,
// on acceptance, during a delayed response, or on response/completion.
void finish_detached_walk(int flush_at, std::uint8_t fault = 0) {
  int ptes_before, fetches_before, responses_before;
  ptes_before = manual_pte_requests;
  fetches_before = instruction_requests_seen;
  responses_before = instruction_responses_seen;
  falling();
  invalidate_all = 1;
  tick();
  falling();
  invalidate_all = 0;
  instruction_address = VIRTUAL_ADDRESS;
  instruction_request_valid = 1;
  memory_ready = 0;
  memory_idle = 0;
  tick();
  falling();
  instruction_request_valid = 0;
  tick(); // The S1 miss has now been accepted by the walker.
  if (flush_at == 0)
    flush_fetch();
  falling();
  memory_idle = 1;
  for (int level = 0; level < 3; level++) {
    until([&] { return data_memory_out.prequest.pvalid; });
    falling();
    CHECK(data_memory_out.prequest.pbits.pmemory.paddress ==
          (level == 0   ? UINT64_C(4096)
           : level == 1 ? UINT64_C(8192)
                        : UINT64_C(12320)));
    if (flush_at == 1 && level == 0)
      flush_fetch();
    if (flush_at == 2 && level == 1)
      instruction_flush = 1;
    memory_ready = 1;
    tick();
    falling();
    memory_ready = 0;
    memory_idle = 0;
    instruction_flush = 0;
    if (flush_at == 3 && level == 1)
      flush_fetch();
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
      tick();
      CHECK(!data_out.pdrained && !data_out.presponse.pvalid &&
            !data_memory_out.prequest.pvalid);
    }
    falling();
    manual_pte_valid = 1;
    manual_pte_data = level == 0   ? LEVEL_2_POINTER
                      : level == 1 ? LEVEL_1_POINTER
                      : fault      ? UINT64_C(0)
                                   : UINT64_C(8267);
    if (flush_at == 4 && level == 2)
      instruction_flush = 1;
    tick();
    falling();
    manual_pte_valid = 0;
    memory_idle = 1;
    instruction_flush = 0;
    if (flush_at == 5 && level == 2) {
      instruction_flush = 1;
      tick();
      falling();
      instruction_flush = 0;
    }
  }
  until([&] { return data_out.pdrained; });
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    tick();
  CHECK(manual_pte_requests == ptes_before + 3 &&
        instruction_requests_seen == fetches_before &&
        instruction_responses_seen == responses_before);

  // Success warms the ITLB. A detached page fault is cached as a translation
  // outcome but belongs architecturally only to a later live fetch.
  falling();
  instruction_address = VIRTUAL_ADDRESS;
  instruction_request_valid = 1;
  tick();
  falling();
  instruction_request_valid = 0;
  if (fault) {
    instruction_fault_expected = 1;
    tick();
    CHECK(instruction_out.presponse.pvalid &&
          !instruction_out.presponse.pbits.preplay &&
          instruction_out.presponse.pbits.presponse.ppage_ufault &&
          manual_pte_requests == ptes_before + 3);
    tick();
    falling();
    instruction_fault_expected = 0;
    // SFENCE drops the outcome, so the next fetch must walk again.
    invalidate_all = 1;
    tick();
    falling();
    invalidate_all = 0;
    instruction_request_valid = 1;
    tick();
    falling();
    instruction_request_valid = 0;
    until([&] { return data_memory_out.prequest.pvalid; });
    CHECK(data_memory_out.prequest.pbits.pmemory.paddress == UINT64_C(4096));
    instruction_flush = 1;
    invalidate_all = 1;
    tick();
    falling();
    instruction_flush = 0;
    invalidate_all = 0;
  } else {
    until([&] { return instruction_responses_seen == responses_before + 1; });
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      tick();
    CHECK(manual_pte_requests == ptes_before + 3 &&
          instruction_requests_seen == fetches_before + 1);
  }
}

void check_canceled_pte_reply() {
  int accepted_before;
  accepted_before = manual_pte_requests;
  falling();
  invalidate_all = 1;
  tick();
  falling();
  invalidate_all = 0;
  memory_ready = 0;
  instruction_address = VIRTUAL_ADDRESS;
  instruction_request_valid = 1;
  tick();
  falling();
  instruction_request_valid = 0;
  until([&] { return data_memory_out.prequest.pvalid; });
  falling();
  memory_ready = 1;
  tick();
  falling();
  memory_ready = 0;
  CHECK(manual_pte_requests == accepted_before + 1);
  invalidate_all = 1;
  tick();
  falling();
  invalidate_all = 0;
  instruction_request_valid = 1;
  tick();
  falling();
  instruction_request_valid = 0;
  for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
    CHECK(!data_memory_out.prequest.pvalid);
    tick();
  }
  falling();
  manual_pte_valid = 1;
  manual_pte_data = LEVEL_2_POINTER;
  tick();
  falling();
  manual_pte_valid = 0;
  // The shared walker owns the canceled read until this edge. The fetch
  // offered during Drain received replay, so model the frontend retry now.
  instruction_request_valid = 1;
  tick();
  falling();
  instruction_request_valid = 0;
  until([&] { return data_memory_out.prequest.pvalid; });
  CHECK(data_memory_out.prequest.pbits.pmemory.paddress == UINT64_C(4096) &&
        manual_pte_requests == accepted_before + 1);
  invalidate_all = 1; // Cancel the new, still-unaccepted offer.
  tick();
  falling();
  invalidate_all = 0;
}

void check_prefetch(std::uint8_t valid, std::uint64_t address = 0,
                    std::uint8_t operation = 0) {
  CHECK(physical_prefetch_out.pvalid == valid);
  if (valid) {
    CHECK(physical_prefetch_out.pbits.paddress == address &&
          physical_prefetch_out.pbits.poperation == operation);
  }
}

void check_isolated_hint(std::uint64_t address, std::uint8_t operation,
                         std::uint8_t survives,
                         std::uint64_t physical_address = 0) {
  falling();
  prefetch_in = {.pvalid = UINT64_C(1),
                 .pbits = {.paddress = address, .poperation = operation}};
  settle();
  check_prefetch(0);
  tick();
  check_prefetch(0);
  falling();
  prefetch_in = {};
  tick();
  check_prefetch(survives, physical_address, operation);
  tick();
  check_prefetch(0);
  CHECK(!data_memory_out.prequest.pvalid);
}

void drive() {
  {
    pipeline_memory_in.presponse.pvalid = pipeline_memory_out.prequest.pvalid;
    pipeline_memory_in.presponse.pbits = {
        .poutcome = priority_pipeline_slow ? PIPE_SLOW : PIPE_LOAD_HIT,
        .preason = UINT64_C(0),
        .pdata = UINT64_C(0x123456789abcdef0)};
    pipeline_memory_in.pcommit_uready = 1;
    instruction_in = {};
    instruction_in.pflush = instruction_flush;
    instruction_in.prequest.pvalid = instruction_request_valid;
    instruction_in.prequest.pbits.paddress = instruction_address;
    data_in.prequest.pvalid = data_request_valid;
    data_in.presponse.pready = UINT64_C(1);
    data_in.prequest.pbits.paddress =
        (page_fault_phase ? FAULT_VIRTUAL_ADDRESS : VIRTUAL_ADDRESS) +
        (zero_request || management_operation != 0 ? UINT64_C(63)
                                                   : UINT64_C(0));
    if (vector_phase)
      data_in.prequest.pbits.paddress = vector_scalar_address;
    data_in.prequest.pbits.paccess =
        management_operation != 0 ? management_operation
        : zero_request            ? UINT64_C(6)
                                  : ((MEMORY_LOAD)&low_mask(4));
    data_in.prequest.pbits.patomic = {};
    data_in.prequest.pbits.pwidth =
        split_contained ? UINT64_C(1) : MEMORY_DOUBLE;
    data_in.prequest.pbits.pbyte_umask = UINT64_MAX;
    data_in.prequest.pbits.punsigned = UINT64_C(1);
    data_in.prequest.pbits.pdata = {};
    data_in.prequest.pbits.pcontext.pwriteback = memory_integer(UINT64_C(7));
    data_in.prequest.pbits.pcontext.porigin = UINT64_C(0);
    data_in.prequest.pbits.plocality = UINT64_C(3);
    if (split_phase) {
      data_in.prequest.pbits.paddress = split_contained    ? UINT64_C(32769)
                                        : split_cross_page ? UINT64_C(20477)
                                        : split_store      ? UINT64_C(32789)
                                                           : UINT64_C(32771);
      data_in.prequest.pbits.paccess = split_store ? UINT64_C(2) : MEMORY_LOAD;
      data_in.prequest.pbits.pdata = UINT64_C(0x1122334455667788);
      data_in.prequest.pbits.pbyte_umask = UINT64_MAX;
    }
    instruction_memory_in = {};
    instruction_memory_in.presponse.pvalid = instruction_return_valid;
    instruction_memory_in.presponse.pbits = {
        .presponse = {.pword = instruction_return_word,
                      .ppage_ufault = UINT64_C(0),
                      .paccess_ufault = UINT64_C(0)},
        .preplay = instruction_return_replay};
    data_memory_in.prequest.pready = memory_ready;
    data_memory_in.prequest_ufault = UINT64_C(0);
    data_memory_in.prequest_uaccess_ufault = UINT64_C(0);
    data_memory_in.presponse.pvalid = pte_response_valid ||
                                      ordinary_response_valid ||
                                      manual_pte_valid || split_response_valid;
    data_memory_in.presponse.pbits.paccess_ufault = 0;
    data_memory_in.presponse.pbits.pdata =
        split_response_valid      ? split_response_data
        : manual_pte_valid        ? manual_pte_data
        : ordinary_response_valid ? UINT64_C(0xfeedface12345678)
                                  : pte_response_data;
    data_memory_in.presponse.pbits.pcontext.pwriteback =
        ordinary_response_valid || split_response_valid
            ? memory_integer(UINT64_C(7))
            : UINT64_C(0);
    data_memory_in.presponse.pbits.pcontext.porigin =
        !(ordinary_response_valid || split_response_valid);
    data_memory_in.pdrained = memory_idle && !pte_response_valid &&
                              !manual_pte_valid && !split_response_valid;
    data_memory_in.preservation_uvalid = memory_idle;
  }
}

void observe() {
  {
    if (reset) {
      defer(pte_response_valid, UINT64_C(0));
      defer(pte_response_data,
            std::remove_cvref_t<decltype(pte_response_data)>{});
      defer(pte_requests, std::remove_cvref_t<decltype(pte_requests)>{});
      defer(translated_request_seen, UINT64_C(0));
      defer(page_fault_pte_seen, UINT64_C(0));
    } else {
      if (data_memory_out.presponse.pready && !ordinary_response_valid &&
          !manual_pte_valid)
        defer(pte_response_valid, UINT64_C(0));
      if (split_phase && split_response_valid &&
          data_memory_out.presponse.pready)
        defer(split_response_valid, UINT64_C(0));
      if (split_phase && split_valid) {
        CHECK((vector_bad_second && split_cross_page
                   ? split_page_fault && split_fault_address == UINT64_C(20480)
                   : !split_page_fault &&
                         split_response.pdata ==
                             (split_store       ? UINT64_C(0)
                              : split_contained ? UINT64_C(513)
                              : split_cross_page
                                  ? UINT64_C(0xc0b0a0908070605)
                                  : UINT64_C(0xa09080706050403))) &&
              split_response.pcontext.pwriteback ==
                  memory_integer(UINT64_C(7)) &&
              !split_response.paccess_ufault);
        defer(split_completions_seen, split_completions_seen + 1);
      }
      CHECK(data_out.preservation_uvalid == data_memory_in.preservation_uvalid);
      if (data_out.presponse.pvalid)
        CHECK(data_out.presponse.pbits.pcontext.pwriteback ==
              memory_integer(UINT64_C(7)));
      if (data_memory_out.prequest.pvalid)
        CHECK(data_memory_out.prequest.pbits.pmemory.pcontext.pwriteback ==
              (bit_slice(
                   data_memory_out.prequest.pbits.pmemory.pcontext.pwriteback,
                   7, 2) == WRITEBACK_INTEGER_KIND
                   ? memory_integer(UINT64_C(7))
                   : UINT64_C(0)));
      if (pte_response_valid || manual_pte_valid)
        CHECK(!data_out.presponse.pvalid);
      CHECK(instruction_phase || !instruction_memory_out.prequest.pvalid);
      if (data_memory_out.prequest.pvalid && data_memory_in.prequest.pready) {
        CHECK(data_memory_out.prequest.pbits.pmemory.plocality ==
              (bit_slice(
                   data_memory_out.prequest.pbits.pmemory.pcontext.pwriteback,
                   7, 2) == WRITEBACK_INTEGER_KIND
                   ? UINT64_C(3)
                   : UINT64_C(0)));
        CHECK(data_lookup_out.pvalid &&
              bit_slice(data_lookup_out.pbits, 0, 12) ==
                  bit_slice(data_memory_out.prequest.pbits.pmemory.paddress, 0,
                            12));
        if (!data_request_valid &&
            data_memory_out.prequest.pbits.pmemory.pcontext.porigin)
          CHECK(data_lookup_out.pbits ==
                data_memory_out.prequest.pbits.pmemory.paddress);
        if (split_phase &&
            !data_memory_out.prequest.pbits.pmemory.pcontext.porigin) {
          CHECK(data_memory_out.prequest.pbits.ppbmt == 0);
          if (split_contained)
            CHECK(!split_response_valid && split_requests_seen == 7 &&
                  data_memory_out.prequest.pbits.pmemory.paddress ==
                      UINT64_C(32768) &&
                  data_memory_out.prequest.pbits.pmemory.pbyte_umask ==
                      UINT64_C(6));
          else
            CHECK(!split_response_valid && split_requests_seen < 7 &&
                  data_memory_out.prequest.pbits.pmemory.paddress ==
                      (split_cross_page
                           ? (split_requests_seen != 5 ? UINT64_C(36856)
                                                       : UINT64_C(40960))
                       : split_store
                           ? (split_requests_seen == 2 ? UINT64_C(32784)
                                                       : UINT64_C(32792))
                           : (split_requests_seen == 0 ? UINT64_C(32768)
                                                       : UINT64_C(32776))) &&
                  data_memory_out.prequest.pbits.pmemory.pbyte_umask ==
                      (split_cross_page
                           ? (split_requests_seen != 5 ? UINT64_C(224)
                                                       : UINT64_C(31))
                       : split_store
                           ? (split_requests_seen == 2 ? UINT64_C(224)
                                                       : UINT64_C(31))
                           : (split_requests_seen == 0 ? UINT64_C(248)
                                                       : UINT64_C(7))) &&
                  (!split_store ||
                   data_memory_out.prequest.pbits.pmemory.pdata ==
                       (split_requests_seen == 2 ? UINT64_C(0x6677880000000000)
                                                 : UINT64_C(0x1122334455))));
          defer(split_response_valid, UINT64_C(1));
          defer(split_response_data,
                split_store ? UINT64_C(0)
                            : (split_contained || split_requests_seen == 0 ||
                                       split_requests_seen == 4 ||
                                       split_requests_seen == 6
                                   ? UINT64_C(0x706050403020100)
                                   : UINT64_C(0xf0e0d0c0b0a0908)));
          defer(split_requests_seen, split_requests_seen + 1);
        } else if (priority_phase) {
          if (data_memory_out.prequest.pbits.pmemory.pcontext.porigin) {
            CHECK(priority_core_requests == 1 && priority_pte_requests == 0 &&
                  data_memory_out.prequest.pbits.pmemory.paddress ==
                      UINT64_C(4096));
            defer(priority_pte_requests, priority_pte_requests + 1);
          } else {
            CHECK(priority_core_requests ==
                      (priority_pte_requests == 0 ? 0 : 1) &&
                  data_memory_out.prequest.pbits.pmemory.paddress ==
                      PHYSICAL_ADDRESS);
            defer(priority_core_requests, priority_core_requests + 1);
          }
        } else if (vector_phase) {
          if (data_memory_out.prequest.pbits.pmemory.pcontext.pwriteback == 0) {
            defer(vector_pte_requests, vector_pte_requests + 1);
            defer(pte_response_valid, 1);
            if (vector_napot &&
                data_memory_out.prequest.pbits.pmemory.paddress >=
                    UINT64_C(12288) &&
                data_memory_out.prequest.pbits.pmemory.paddress <
                    UINT64_C(16384))
              defer(
                  pte_response_data,
                  UINT64_C(
                      9223372036854915279)); // One 64-KiB mapping at PA 0x80000.
            else
              switch (data_memory_out.prequest.pbits.pmemory.paddress) {
              case UINT64_C(4096): {
                defer(pte_response_data,
                      vector_superpage ? UINT64_C(207) : LEVEL_2_POINTER);
              } break;
              case UINT64_C(8192): {
                defer(pte_response_data, LEVEL_1_POINTER);
              } break;
              case UINT64_C(12320): {
                defer(pte_response_data,
                      (vector_no_dirty ? UINT64_C(8263) : UINT64_C(8391)) |
                          (((vector_pbmt)&low_mask(64))
                           << 61)); // VA 0x4000 -> PA 0x8000, RWA[D].
              } break;
              case UINT64_C(12328): {
                {
                  if (split_phase)
                    CHECK(!split_response_valid &&
                          split_requests_seen == (vector_bad_second ? 7 : 5));
                  defer(pte_response_data,
                        vector_bad_second ? UINT64_C(0)
                                          : UINT64_C(10439)); // Nonadjacent PA.
                }
              } break;
              default: {
                defer(pte_response_data, UINT64_C(16583));
              } break;
              }
          }
        } else if (detached_walk_phase) {
          CHECK(bit_slice(
                    data_memory_out.prequest.pbits.pmemory.pcontext.pwriteback,
                    7, 2) == 0);
          defer(manual_pte_requests, manual_pte_requests + 1);
        } else if (instruction_translation_phase) {
          switch (instruction_pte_requests) {
          case 0:
          case 4: {
            {
              CHECK(data_memory_out.prequest.pbits.pmemory.paddress ==
                    UINT64_C(4096));
              defer(pte_response_data, LEVEL_2_POINTER);
            }
          } break;
          case 1:
          case 5: {
            {
              CHECK(data_memory_out.prequest.pbits.pmemory.paddress ==
                    UINT64_C(8192));
              defer(pte_response_data, LEVEL_1_POINTER);
            }
          } break;
          case 2: {
            {
              CHECK(data_memory_out.prequest.pbits.pmemory.paddress ==
                    UINT64_C(12320));
              defer(pte_response_data,
                    UINT64_C(8267)); // Valid, readable, executable, accessed.
            }
          } break;
          case 3: {
            {
              CHECK(data_memory_out.prequest.pbits.pmemory.paddress ==
                    UINT64_C(4096));
              defer(pte_response_data, 0);
            }
          } break;
          case 6: {
            {
              CHECK(data_memory_out.prequest.pbits.pmemory.paddress ==
                    UINT64_C(12328));
              defer(pte_response_data,
                    UINT64_C(10315)); // Maps VA 0x5000 to PA 0xa000.
            }
          } break;
          default: {
            fail(1, "ITLB hit unnecessarily restarted the walker");
          } break;
          }
          defer(pte_response_valid, UINT64_C(1));
          defer(instruction_pte_requests, instruction_pte_requests + 1);
        } else if (page_fault_phase) {
          CHECK(!page_fault_pte_seen &&
                data_memory_out.prequest.pbits.pmemory.paddress ==
                    UINT64_C(4096));
          defer(pte_response_valid, UINT64_C(1));
          defer(pte_response_data, UINT64_C(0));
          defer(page_fault_pte_seen, UINT64_C(1));
        } else if (pte_requests == 0) {
          CHECK(data_memory_out.prequest.pbits.pmemory.paddress ==
                UINT64_C(4096));
          defer(pte_response_valid, UINT64_C(1));
          defer(pte_response_data, LEVEL_2_POINTER);
          defer(pte_requests, 1);
        } else if (pte_requests == 1) {
          CHECK(data_memory_out.prequest.pbits.pmemory.paddress ==
                UINT64_C(8192));
          defer(pte_response_valid, UINT64_C(1));
          defer(pte_response_data, LEVEL_1_POINTER);
          defer(pte_requests, 2);
        } else if (pte_requests == 2) {
          CHECK(data_memory_out.prequest.pbits.pmemory.paddress ==
                UINT64_C(12320));
          defer(pte_response_valid, UINT64_C(1));
          defer(pte_response_data, LEVEL_0_LEAF);
          defer(pte_requests, 3);
        } else {
          CHECK(data_request_valid &&
                data_memory_out.prequest.pbits.pmemory.paddress ==
                    PHYSICAL_ADDRESS + (management_operation != 0
                                            ? UINT64_C(63)
                                            : UINT64_C(0)) &&
                bit_slice(
                    data_memory_out.prequest.pbits.pmemory.pcontext.pwriteback,
                    7, 2) == WRITEBACK_INTEGER_KIND &&
                memory_rd(data_memory_out.prequest.pbits.pmemory.pcontext
                              .pwriteback) == UINT64_C(7));
          defer(translated_request_seen, UINT64_C(1));
        }
      }
    }
  }
  {
    defer(instruction_return_valid,
          instruction_memory_out.prequest.pvalid && !instruction_flush);
    defer(instruction_return_replay, instruction_blocked);
    if (instruction_phase && !instruction_flush) {
      if (instruction_memory_out.prequest.pvalid && !instruction_blocked) {
        CHECK(vector_napot ? instruction_memory_out.prequest.pbits.paddress ==
                                 napot_fetch_expected
              : detached_walk_phase
                  ? instruction_memory_out.prequest.pbits.paddress ==
                        PHYSICAL_ADDRESS
                  : instruction_requests_seen <
                            (instruction_translation_phase ? 5 : 2) &&
                        instruction_memory_out.prequest.pbits.paddress ==
                            (instruction_requests_seen == 4
                                 ? UINT64_C(40960)
                                 : UINT64_C(32768) +
                                       4 * ((instruction_requests_seen % 2) &
                                            low_mask(64))));
        defer(instruction_return_valid, UINT64_C(1));
        defer(instruction_return_word,
              UINT64_C(256) + ((instruction_requests_seen)&low_mask(32)));
        defer(instruction_requests_seen, instruction_requests_seen + 1);
      }
      if (instruction_out.presponse.pvalid &&
          !instruction_out.presponse.pbits.preplay) {
        if (instruction_fault_expected) {
          CHECK(instruction_out.presponse.pbits.presponse.ppage_ufault &&
                !instruction_out.presponse.pbits.presponse.paccess_ufault);
        } else {
          CHECK(instruction_out.presponse.pbits.presponse.pword ==
                    UINT64_C(256) +
                        ((instruction_physical_responses_seen)&low_mask(32)) &&
                !instruction_out.presponse.pbits.presponse.ppage_ufault &&
                !instruction_out.presponse.pbits.presponse.paccess_ufault);
          defer(instruction_physical_responses_seen,
                instruction_physical_responses_seen + 1);
        }
        defer(instruction_responses_seen, instruction_responses_seen + 1);
      }
    }
  }
}

void falling_update() {}

void stimulus() {
  reset = UINT64_C(1);
  instruction_lookup_in = {.pready = UINT64_C(1)};
  pipeline_in = {};
  ordered_busy = 0;
  replay_pending = 0;
  replay_pc = 0;
  pbmte = 0;
  pipeline_vector = 0;
  pipeline_physical = 0;
  vector_precheck_in = {};

  {
    data_request_valid = UINT64_C(0);
    page_fault_phase = UINT64_C(0);
    prefetch_in = {};
    privilege = PRIVILEGE_S;
    mstatus = {};
    satp = SATP_SV39_ROOT_1;
    invalidate_all = UINT64_C(0);
    instruction_flush = UINT64_C(0);
    for (int repeat_index = 0; repeat_index < (2); ++repeat_index)
      rising();
    settle();
    reset = UINT64_C(0);

    falling();
    data_request_valid = UINT64_C(1);
    settle();
    CHECK(!data_out.prequest.pready && !data_out.prequest_ufault &&
          !data_out.prequest_uaccess_ufault &&
          !data_memory_out.prequest.pvalid);
    CHECK(data_lookup_out.pvalid && data_lookup_out.pbits == VIRTUAL_ADDRESS);
    rising();
    settle();
    data_request_valid = UINT64_C(0);

    // A pending PTE may offer while older work drains; its own acceptance is
    // still controlled by request readiness, not whole-port ownership.
    for (int repeat_index = 0; repeat_index < (2); ++repeat_index) {
      tick();
      CHECK(data_memory_out.prequest.pvalid && !data_out.pdrained);
    }
    falling();
    ordinary_response_valid = UINT64_C(1);
    settle();
    CHECK(data_out.presponse.pvalid &&
          same_memory_response(data_out.presponse.pbits,
                               data_memory_in.presponse.pbits));
    tick();
    falling();
    ordinary_response_valid = UINT64_C(0);
    memory_idle = UINT64_C(1);
    tick();
    CHECK(data_memory_out.prequest.pvalid &&
          data_memory_out.prequest.pbits.pmemory.paddress == UINT64_C(4096));
    stalled_request = data_memory_out.prequest.pbits.pmemory;
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
      tick();
      CHECK(data_memory_out.prequest.pvalid &&
            same_memory_request(data_memory_out.prequest.pbits.pmemory,
                                stalled_request) &&
            pte_requests == 0);
    }
    falling();
    memory_ready = UINT64_C(1);

    until([&] { return pte_requests == 3 && data_out.pdrained; });
    falling();
    memory_ready = UINT64_C(0);
    data_request_valid = UINT64_C(1);
    settle();
    stalled_request = data_memory_out.prequest.pbits.pmemory;
    for (int repeat_index = 0; repeat_index < (2); ++repeat_index) {
      CHECK(!data_out.prequest.pready && data_memory_out.prequest.pvalid &&
            same_memory_request(data_memory_out.prequest.pbits.pmemory,
                                stalled_request) &&
            !translated_request_seen);
      tick();
    }
    falling();
    memory_ready = UINT64_C(1);
    settle();
    CHECK(data_out.prequest.pready && data_memory_out.prequest.pvalid &&
          data_memory_out.prequest.pbits.pmemory.paddress == PHYSICAL_ADDRESS);
    CHECK(data_lookup_out.pvalid && data_lookup_out.pbits == VIRTUAL_ADDRESS);
    rising();
    settle();
    data_request_valid = UINT64_C(0);
    CHECK(translated_request_seen);
    falling();
    ordinary_response_valid = UINT64_C(1);
    settle();
    CHECK(data_out.presponse.pvalid &&
          same_memory_response(data_out.presponse.pbits,
                               data_memory_in.presponse.pbits));
    tick();
    falling();
    ordinary_response_valid = UINT64_C(0);

    // An older MEM slow request must reach WB ahead of a pending wrong-path
    // instruction PTE read. The accepted PTE still owns its eventual reply.
    priority_phase = 1;
    memory_ready = 0;
    instruction_address = UINT64_C(20480);
    instruction_request_valid = 1;
    tick();
    falling();
    instruction_request_valid = 0;
    tick();
    until([&] { return data_memory_out.prequest.pvalid; });
    falling();
    priority_pipeline_slow = 1;
    pipeline_in.prequest = {.pvalid = UINT64_C(1),
                            .pbits = {.pbyte_umask = UINT64_C(255),
                                      .paddress = VIRTUAL_ADDRESS,
                                      .paccess = MEMORY_LOAD,
                                      .pwidth = MEMORY_DOUBLE,
                                      .punsigned = UINT64_C(0),
                                      .pdata = {}}};
    tick();
    falling();
    memory_ready = 0;
    settle();
    CHECK(pipeline_out.presponse.pvalid &&
          pipeline_out.presponse.pbits.poutcome == PIPE_SLOW &&
          data_memory_out.prequest.pvalid &&
          data_memory_out.prequest.pbits.pmemory.pcontext.porigin);
    falling();
    pipeline_in = {};
    tick();
    falling();
    data_request_valid = 1;
    memory_ready = 1;
    settle();
    CHECK(data_memory_out.prequest.pvalid &&
          data_memory_out.prequest.pbits.pmemory.paddress == PHYSICAL_ADDRESS &&
          data_out.prequest.pready && data_lookup_out.pbits == VIRTUAL_ADDRESS);
    tick();
    falling();
    data_request_valid = 0;
    memory_ready = 0;
    memory_idle = 0;
    ordinary_response_valid = 1;
    settle();
    CHECK(data_out.presponse.pvalid &&
          data_out.presponse.pbits.pdata == UINT64_C(0xfeedface12345678) &&
          data_memory_out.prequest.pvalid &&
          data_memory_out.prequest.pbits.pmemory.pcontext.porigin);
    tick();
    falling();
    ordinary_response_valid = 0;
    memory_idle = 1;
    memory_ready = 1;
    until([&] { return priority_pte_requests == 1; });
    falling();
    memory_idle = 0;
    data_request_valid = 1;
    settle();
    CHECK(data_out.prequest.pready && data_memory_out.prequest.pvalid &&
          !data_memory_out.prequest.pbits.pmemory.pcontext.porigin &&
          !data_out.presponse.pvalid);
    tick();
    falling();
    data_request_valid = 0;
    manual_pte_valid = 1;
    manual_pte_data = 0; // The wrong-path instruction walk faults.
    settle();
    CHECK(!data_out.presponse.pvalid);
    tick();
    falling();
    manual_pte_valid = 0;
    ordinary_response_valid = 1;
    settle();
    CHECK(data_out.presponse.pvalid &&
          data_out.presponse.pbits.pdata == UINT64_C(0xfeedface12345678));
    tick();
    falling();
    ordinary_response_valid = 0;
    memory_idle = 1;
    until([&] { return data_out.pdrained; });
    CHECK(priority_core_requests == 2);
    flush_fetch();
    priority_phase = 0;
    priority_pipeline_slow = 0;

    check_load_pipeline(VIRTUAL_ADDRESS, 1, PHYSICAL_ADDRESS);
    check_load_pipeline(VIRTUAL_ADDRESS + 8, 1, PHYSICAL_ADDRESS + 8);
    check_load_pipeline(VIRTUAL_ADDRESS + UINT64_C(4096), 0, 0,
                        PIPE_SLOW); // miss must wait for WB
    memory_idle = 0;
    check_load_pipeline(
        VIRTUAL_ADDRESS, 1,
        PHYSICAL_ADDRESS); // pending cache stores do not block translation
    ordered_busy = 1;
    check_load_pipeline(VIRTUAL_ADDRESS, 0, 0,
                        PIPE_REPLAY); // ordered IO does block a younger hit
    ordered_busy = 0;
    memory_idle = 1;
    privilege = UINT64_C(0);
    check_load_pipeline(VIRTUAL_ADDRESS, 0, 0,
                        PIPE_PAGE_FAULT); // supervisor leaf denied to U
    privilege = PRIVILEGE_S;
    check_load_pipeline(VIRTUAL_ADDRESS, 0, 0, PIPE_PAGE_FAULT,
                        UINT64_C(2)); // store requires the dirty PTE bit

    // A writable but non-dirty leaf may serve loads, but CBO.ZERO must fault
    // under the core's fault-on-A/D policy, even on a nonaligned TLB hit.
    falling();
    zero_request = UINT64_C(1);
    data_request_valid = UINT64_C(1);
    settle();
    CHECK(data_out.prequest.pready && data_out.prequest_ufault &&
          !data_memory_out.prequest.pvalid);
    rising();
    settle();
    data_request_valid = UINT64_C(0);
    zero_request = UINT64_C(0);

    // Management uses a distinct translation class: A is required, D is not.
    // The original byte offset survives translation for precise trap metadata.
    for (int operation = 7; operation <= 9; operation++) {
      falling();
      management_operation = ((operation)&low_mask(4));
      data_request_valid = 1;
      settle();
      CHECK(data_out.prequest.pready && !data_out.prequest_ufault &&
            data_memory_out.prequest.pvalid &&
            data_memory_out.prequest.pbits.pmemory.paccess ==
                ((operation)&low_mask(4)) &&
            data_memory_out.prequest.pbits.pmemory.paddress ==
                PHYSICAL_ADDRESS + 63);
      tick();
      falling();
      data_request_valid = 0;
    }
    // MPRV affects translation even though xenvcfg authorization uses current M.
    privilege = UINT64_C(3);
    mstatus = UINT64_C(0x20000);
    data_request_valid = 1;
    settle();
    CHECK(data_out.prequest_ufault && !data_memory_out.prequest.pvalid);
    tick();
    falling();
    data_request_valid = 0;
    management_operation = 0;
    privilege = PRIVILEGE_S;
    mstatus = 0;

    // The leaf is readable and accessed but not dirty. PREFETCH.W is still
    // permitted because prefetch translation accepts any R/W/X permission and
    // ignores A/D state.
    falling();
    prefetch_in.pvalid = UINT64_C(1);
    prefetch_in.pbits.paddress = VIRTUAL_ADDRESS;
    prefetch_in.pbits.poperation = UINT64_C(3);
    settle();
    check_prefetch(0);
    tick();
    check_prefetch(0);

    // Adjacent hints retain their own address/operation and emerge one per
    // cycle, but neither can bypass either register boundary.
    falling();
    prefetch_in.pbits.paddress = VIRTUAL_ADDRESS + UINT64_C(127);
    prefetch_in.pbits.poperation = UINT64_C(2);
    tick();
    check_prefetch(1, PHYSICAL_ADDRESS, UINT64_C(3));

    // A prefetch miss is dropped and must not claim the page-table walker.
    falling();
    prefetch_in.pvalid = UINT64_C(1);
    prefetch_in.pbits.paddress = VIRTUAL_ADDRESS + UINT64_C(4096);
    prefetch_in.pbits.poperation = UINT64_C(2);
    settle();
    check_prefetch(1, PHYSICAL_ADDRESS, UINT64_C(3));
    tick();
    check_prefetch(1, PHYSICAL_ADDRESS + UINT64_C(64), UINT64_C(2));
    falling();
    prefetch_in = {};
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index) {
      tick();
      check_prefetch(0);
      CHECK(!data_memory_out.prequest.pvalid);
    }

    // This address hits only DTLB: an instruction hint must not borrow it.
    check_isolated_hint(VIRTUAL_ADDRESS, UINT64_C(1), 0);
    check_isolated_hint(VIRTUAL_ADDRESS, UINT64_C(0), 0);
    falling();
    privilege = PRIVILEGE_U;
    tick();
    check_isolated_hint(VIRTUAL_ADDRESS, UINT64_C(2), 0);
    falling();
    privilege = PRIVILEGE_S;
    tick();

    falling();
    page_fault_phase = UINT64_C(1);
    data_request_valid = UINT64_C(1);
    settle();
    CHECK(!data_out.prequest.pready && !data_out.prequest_ufault &&
          !data_out.prequest_uaccess_ufault &&
          !data_memory_out.prequest.pvalid);
    rising();
    settle();
    data_request_valid = UINT64_C(0);

    until([&] { return page_fault_pte_seen; });
    // Fetch recovery must not discard a data walk's completion or fault.
    falling();
    instruction_flush = 1;
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick();
    falling();
    instruction_flush = 0;

    // The translated entry is supervisor-only. A user-mode lookup therefore
    // faults directly in the DTLB, but must not consume the pending walker
    // fault for FAULT_VIRTUAL_ADDRESS.
    falling();
    page_fault_phase = UINT64_C(0);
    privilege = PRIVILEGE_U;
    data_request_valid = UINT64_C(1);
    settle();
    CHECK(data_out.prequest.pready && data_out.prequest_ufault &&
          !data_out.prequest_uaccess_ufault &&
          !data_memory_out.prequest.pvalid);
    rising();
    settle();
    data_request_valid = UINT64_C(0);
    CHECK(data_out.pdrained);

    falling();
    page_fault_phase = UINT64_C(1);
    privilege = PRIVILEGE_S;
    data_request_valid = UINT64_C(1);
    settle();
    CHECK(data_out.prequest.pready && data_out.prequest_ufault &&
          !data_out.prequest_uaccess_ufault &&
          !data_memory_out.prequest.pvalid);
    rising();
    settle();
    data_request_valid = UINT64_C(0);
    CHECK(data_out.pdrained);

    // Bare hints keep the same latency and line alignment, including I hints.
    falling();
    satp = {};
    tick();
    check_isolated_hint(PHYSICAL_ADDRESS + UINT64_C(127), UINT64_C(1), 1,
                        PHYSICAL_ADDRESS + UINT64_C(64));
    // The test physical map covers only the CHI physical-address width.
    check_isolated_hint(UINT64_C(0x8000000000000000), UINT64_C(2), 0);

    // Flush/reset and each relevant translation-context change cancel either
    // occupied stage at the edge. Already presented physical hints are not
    // withdrawn combinationally, so cancellation cannot reopen a demand path.
    for (int cancellation = 0; cancellation < 7; cancellation++) {
      for (int stage = 1; stage <= 2; stage++) {
        falling();
        prefetch_in = {
            .pvalid = UINT64_C(1),
            .pbits = {.paddress = PHYSICAL_ADDRESS, .poperation = UINT64_C(2)}};
        tick();
        check_prefetch(0);
        if (stage == 2) {
          falling();
          prefetch_in = {};
          tick();
          check_prefetch(1, PHYSICAL_ADDRESS, UINT64_C(2));
        }
        falling();
        // Leave ingress valid while canceling the first stage: new hints on
        // the cancellation edge must also be discarded.
        switch (cancellation) {
        case 0: {
          instruction_flush = 1;
        } break;
        case 1: {
          invalidate_all = 1;
        } break;
        case 2: {
          satp = satp ^ UINT64_C(1);
        } break;
        case 3: {
          privilege = privilege == PRIVILEGE_S ? PRIVILEGE_U : PRIVILEGE_S;
        } break;
        case 4: {
          mstatus = mstatus ^ (UINT64_C(1) << 18);
        } break;
        case 5: {
          mstatus = mstatus ^ (UINT64_C(1) << 19);
        } break;
        case 6: {
          reset = 1;
        } break;
        }
        settle();
        check_prefetch(stage == 2, PHYSICAL_ADDRESS, UINT64_C(2));
        tick();
        check_prefetch(0);
        falling();
        prefetch_in = {};
        instruction_flush = 0;
        invalidate_all = 0;
        reset = 0;
        for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
          tick();
          check_prefetch(0);
          CHECK(!data_memory_out.prequest.pvalid &&
                !instruction_memory_out.prequest.pvalid);
        }
      }
    }
    check_isolated_hint(PHYSICAL_ADDRESS, UINT64_C(3), 1, PHYSICAL_ADDRESS);
    // ITLB miss and physical execute denial must not suppress the early read.
    falling();
    satp = SATP_SV39_ROOT_1;
    privilege = PRIVILEGE_S;
    instruction_request_valid = 1;
    settle();
    CHECK(instruction_lookup_out.pvalid &&
          instruction_lookup_out.pbits == instruction_address &&
          instruction_out.prequest.pready &&
          !instruction_memory_out.prequest.pvalid);
    instruction_flush = 1;
    settle();
    CHECK(instruction_lookup_out.pvalid && instruction_out.prequest.pready);
    instruction_request_valid = 0;
    tick();
    falling();
    satp = 0;
    instruction_address = UINT64_C(0x8000000000000000);
    instruction_request_valid = 1;
    settle();
    CHECK(instruction_flush && instruction_lookup_out.pvalid &&
          instruction_out.prequest.pready &&
          !instruction_memory_out.prequest.pvalid);
    tick();
    instruction_flush = 0;
    instruction_request_valid = 0;
    // Translation and fault classification use the admitted S1 address, not
    // the live S0 payload, and publish the fault through S2 ownership.
    instruction_address = UINT64_C(0x80000000);
    tick();
    CHECK(instruction_out.presponse.pvalid &&
          instruction_out.presponse.pbits.presponse.paccess_ufault);
    tick();

    // Consecutive S0 attempts retain their own S1 addresses and return replay
    // when physical service is blocked. The frontend explicitly retries them.
    falling();
    instruction_phase = 1;
    instruction_blocked = 1;
    instruction_address = UINT64_C(32768);
    instruction_request_valid = 1;
    settle();
    CHECK(instruction_out.prequest.pready &&
          !instruction_memory_out.prequest.pvalid);
    tick();
    instruction_address = UINT64_C(32772);
    settle();
    CHECK(instruction_out.prequest.pready &&
          instruction_memory_out.prequest.pvalid &&
          instruction_memory_out.prequest.pbits.paddress == UINT64_C(32768));
    tick();
    instruction_request_valid = 0;
    instruction_address = UINT64_C(0xdead0000);
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      tick();
    instruction_blocked = 0;
    fetch_word(UINT64_C(32768));
    fetch_word(UINT64_C(32772));
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      tick();
    CHECK(instruction_requests_seen == 2 &&
          !instruction_memory_out.prequest.pvalid);

    falling();
    instruction_blocked = 1;
    instruction_request_valid = 1;
    instruction_address = UINT64_C(32776);
    tick();
    instruction_request_valid = 0;
    instruction_flush = 1;
    tick();
    instruction_flush = 0;
    instruction_blocked = 0;
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      tick();
    CHECK(instruction_requests_seen == 2 && !instruction_lookup_out.pvalid);

    // A real ITLB miss returns replay while one walk owns its PTE traffic.
    // Explicit retries of both words must reuse the resulting translation.
    // The earlier wrong-path walk saw an invalid PTE for 0x5000; the model now
    // supplies a mapping, so invalidate that cached fault before retrying.
    clear_translations();
    falling();
    instruction_translation_phase = 1;
    satp = SATP_SV39_ROOT_1;
    privilege = PRIVILEGE_S;
    mstatus = 0;
    instruction_address = VIRTUAL_ADDRESS;
    instruction_request_valid = 1;
    settle();
    CHECK(instruction_out.prequest.pready &&
          !instruction_memory_out.prequest.pvalid);
    tick();
    instruction_address = VIRTUAL_ADDRESS + 4;
    settle();
    CHECK(instruction_out.prequest.pready &&
          !instruction_memory_out.prequest.pvalid);
    tick();
    instruction_request_valid = 0;
    instruction_address = UINT64_C(0xdead0000);
    until([&] { return instruction_pte_requests == 3 && data_out.pdrained; });
    fetch_word(VIRTUAL_ADDRESS);
    fetch_word(VIRTUAL_ADDRESS + 4);
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      tick();
    CHECK(instruction_pte_requests == 3 && instruction_requests_seen == 4 &&
          !instruction_lookup_out.pvalid &&
          !instruction_memory_out.prequest.pvalid);

    // Retain a walker fault while the reread port is unavailable, then redirect.
    // The canceled fault must not strand the walker for a subsequent ITLB miss.
    falling();
    instruction_address = UINT64_C(36864);
    instruction_request_valid = 1;
    tick();
    instruction_request_valid = 0;
    instruction_lookup_in.pready = 0;
    until([&] { return instruction_pte_requests == 4; });
    for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
      tick();
    CHECK(!instruction_out.presponse.pvalid && instruction_requests_seen == 4);
    instruction_flush = 1;
    tick();
    instruction_flush = 0;
    instruction_lookup_in.pready = 1;
    fetch_word(UINT64_C(20480));
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      tick();
    CHECK(instruction_pte_requests == 7 && instruction_requests_seen == 5);
    detached_walk_phase = 1;
    for (int flush_at = 0; flush_at < 6; flush_at++)
      finish_detached_walk(flush_at);
    finish_detached_walk(3, 1); // Fault discovered after an earlier redirect.
    finish_detached_walk(5, 1); // Redirect coincides with fault completion.
    // An older replay may refetch a straddling instruction, but a younger
    // speculative page cannot start another walk before that owner resolves.
    clear_translations();
    falling();
    replay_pending = 1;
    replay_pc = UINT64_C(12286);
    memory_ready = 0;
    instruction_address = VIRTUAL_ADDRESS;
    instruction_request_valid = 1;
    tick();
    falling();
    instruction_request_valid = 0;
    tick();
    CHECK(instruction_out.presponse.pvalid &&
          instruction_out.presponse.pbits.preplay &&
          !data_memory_out.prequest.pvalid);
    falling();
    instruction_address = UINT64_C(12288);
    instruction_request_valid = 1;
    tick();
    falling();
    instruction_request_valid = 0;
    until([&] { return data_memory_out.prequest.pvalid; });
    CHECK(data_memory_out.prequest.pbits.pmemory.paddress == UINT64_C(4096));
    instruction_flush = 1;
    invalidate_all = 1;
    tick();
    falling();
    instruction_flush = 0;
    invalidate_all = 0;
    replay_pending = 0;
    check_canceled_pte_reply();
    // Suppressing a younger instruction walk must leave the shared walker
    // available to translate the replaying instruction's data access.
    clear_translations();
    falling();
    replay_pending = 1;
    replay_pc = UINT64_C(12284);
    instruction_address = VIRTUAL_ADDRESS;
    instruction_request_valid = 1;
    tick();
    falling();
    instruction_request_valid = 0;
    data_request_valid = 1;
    tick();
    CHECK(data_memory_out.prequest.pvalid &&
          data_memory_out.prequest.pbits.pmemory.paddress == UINT64_C(4096));
    falling();
    data_request_valid = 0;
    instruction_flush = 1;
    invalidate_all = 1;
    tick();
    falling();
    instruction_flush = 0;
    invalidate_all = 0;
    replay_pending = 0;
    detached_walk_phase = 0;
    instruction_phase = 0;
    instruction_translation_phase = 0;
    vector_phase = 1;
    memory_ready = 1;
    memory_idle = 1;
    page_fault_phase = 0;
    management_operation = 0;
    zero_request = 0;
    clear_translations();
    certify_range(UINT64_C(20416), UINT64_C(20543), 1, 1, 6);
    release_window();
    // The two warm 4 KiB translations must be reused without another walk.
    certify_range(UINT64_C(20416), UINT64_C(20543), 1, 1, 0);
    pipeline_vector = 1;
    check_load_pipeline(UINT64_C(20472), 1, UINT64_C(36856));
    check_load_pipeline(UINT64_C(20480), 1, UINT64_C(40960));
    pipeline_vector = 0;
    // Evict both source DTLB entries using unrelated scalar demand walks.
    for (int page = 16; page < 26; page++) {
      falling();
      vector_scalar_address = ((page)&low_mask(64)) << 12;
      data_request_valid = 1;
      tick();
      falling();
      data_request_valid = 0;
      until([&] { return !data_out.pdrained; });
      until([&] { return data_out.pdrained; });
      for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
        tick();
    }
    check_load_pipeline(UINT64_C(16384), 0, 0, PIPE_SLOW);
    // A carried physical mapping bypasses replacement and a different live
    // fallback window, but the same sideband must never bypass a scalar lookup.
    pipeline_physical = 1;
    check_load_pipeline(UINT64_C(16384), 0, 0, PIPE_SLOW);
    pipeline_vector = 1;
    check_load_pipeline(UINT64_C(49152), 1, UINT64_C(49152));
    pipeline_physical = 0;
    pipeline_vector = 1;
    check_load_pipeline(UINT64_C(20472), 1, UINT64_C(36856));
    check_load_pipeline(UINT64_C(20480), 1, UINT64_C(40960));
    pipeline_vector = 0;
    release_window();
    clear_translations();
    vector_superpage = 1;
    certify_range(UINT64_C(20416), UINT64_C(20543), 0, 1, 1);
    pipeline_vector = 1;
    check_load_pipeline(UINT64_C(20480), 1, UINT64_C(20480));
    pipeline_vector = 0;
    release_window();
    clear_translations();
    vector_superpage = 0;
    vector_bad_second = 1;
    certify_range(UINT64_C(20416), UINT64_C(20543), 0, 0, 6);
    release_window();
    // A failed conservative precheck is not a trap. Its valid first page remains reusable.
    check_load_pipeline(UINT64_C(20416), 1, UINT64_C(36800));
    clear_translations();
    vector_no_dirty = 1;
    certify_range(UINT64_C(16384), UINT64_C(16639), 0, 1, 3);
    release_window();
    // Warm DTLB translations certify without a request/response staging cycle.
    certify_range(UINT64_C(16384), UINT64_C(16639), 0, 1, 0, 1);
    release_window();
    certify_contended_hit();
    release_window();
    // A TLB hit still checks this macro's store permission, including D.
    certify_range(UINT64_C(16384), UINT64_C(16639), 1, 0, 0, 1);
    release_window();
    // One NAPOT leaf authorizes both 4-KiB window pages and fills a compact DTLB entry.
    clear_translations();
    vector_napot = 1;
    vector_bad_second = 0;
    vector_no_dirty = 0;
    certify_range(UINT64_C(20416), UINT64_C(20543), 1, 1, 3);
    pipeline_vector = 1;
    check_load_pipeline(UINT64_C(20472), 1, UINT64_C(0x84ff8));
    check_load_pipeline(UINT64_C(20480), 1, UINT64_C(0x85000));
    pipeline_vector = 0;
    release_window();
    check_load_pipeline(UINT64_C(61432), 1, UINT64_C(0x8eff8));
    check_isolated_hint(UINT64_C(49280), UINT64_C(2), 1, UINT64_C(0x8c080));
    // A 64-KiB boundary needs a second leaf, even when both leaves use the same PPN.
    clear_translations();
    certify_range(UINT64_C(65472), UINT64_C(0x1003f), 0, 1, 6);
    pipeline_vector = 1;
    check_load_pipeline(UINT64_C(65528), 1, UINT64_C(0x8fff8));
    check_load_pipeline(UINT64_C(0x10000), 1, UINT64_C(0x80000));
    pipeline_vector = 0;
    release_window();
    clear_translations();
    // ITLB misses use the same compact mapping, then every subpage hits without walking.
    instruction_phase = 1;
    {
      int before_ptes = vector_pte_requests;
      for (int page = 0; page < 16; page++) {
        napot_fetch_expected =
            UINT64_C(0x80000) + ((page * 4096) & low_mask(64));
        fetch_word(((page * 4096) & low_mask(64)));
      }
      CHECK(vector_pte_requests == before_ptes + 3);
      check_isolated_hint(UINT64_C(53376), UINT64_C(1), 1, UINT64_C(0x8d080));
    }
    instruction_phase = 0;
    vector_napot = 0;
    pbmte = 1;
    for (int kind = 1; kind <= 2; kind++) {
      vector_pbmt = ((kind)&low_mask(2));
      clear_translations();
      certify_range(UINT64_C(16384), UINT64_C(16639), 0, 0, 3);
      release_window();
      certify_range(UINT64_C(16384), UINT64_C(16639), 1, 0, 0, 1);
      release_window();
      check_load_pipeline(UINT64_C(16384), 0, 0, PIPE_SLOW);
      check_load_pipeline(UINT64_C(16384), 0, 0, PIPE_SLOW, UINT64_C(2));
      check_isolated_hint(UINT64_C(16384), UINT64_C(2), 0);
      check_isolated_hint(UINT64_C(16384), UINT64_C(3), 0);
      falling();
      vector_scalar_address = UINT64_C(16384);
      data_request_valid = 1;
      settle();
      CHECK(data_memory_out.prequest.pvalid &&
            data_memory_out.prequest.pbits.ppbmt == ((kind)&low_mask(2)));
      tick();
      falling();
      data_request_valid = 0;
    }
    // A WB-authorized misaligned access owns two physical words until its
    // single architectural completion. Vector and scalar use this same port.
    falling();
    vector_pbmt = 0;
    vector_phase = 0;
    vector_napot = 0;
    privilege = UINT64_C(3);
    mstatus = 0;
    satp = 0;
    memory_idle = 1;
    memory_ready = 1;
    split_phase = 1;
    split_store = 0;
    data_request_valid = 1;
    settle();
    CHECK(data_out.prequest.pready && !data_memory_out.prequest.pvalid);
    tick();
    falling();
    data_request_valid = 0;
    memory_ready = 0;
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
      tick();
      CHECK(
          data_memory_out.prequest.pvalid &&
          data_memory_out.prequest.pbits.pmemory.paddress == UINT64_C(32768) &&
          data_memory_out.prequest.pbits.pmemory.pbyte_umask == UINT64_C(248) &&
          split_requests_seen == 0 && !split_valid);
    }
    falling();
    memory_ready = 1;
    for (int cycle = 0; cycle < 20 && split_completions_seen == 0; cycle++)
      tick();
    CHECK(split_requests_seen == 2 && split_completions_seen == 1);
    falling();
    split_store = 1;
    data_request_valid = 1;
    settle();
    CHECK(data_out.prequest.pready && !data_memory_out.prequest.pvalid);
    tick();
    falling();
    data_request_valid = 0;
    for (int cycle = 0; cycle < 20 && split_completions_seen == 1; cycle++)
      tick();
    CHECK(split_requests_seen == 4 && split_completions_seen == 2);
    falling();
    split_store = 0;
    split_cross_page = 1;
    vector_phase = 1;
    vector_superpage = 0;
    vector_bad_second = 0;
    vector_no_dirty = 0;
    privilege = PRIVILEGE_S;
    satp = SATP_SV39_ROOT_1;
    clear_translations();
    falling();
    data_request_valid = 1;
    settle();
    for (int cycle = 0; cycle < 80 && !data_out.prequest.pready; cycle++)
      tick();
    CHECK(data_out.prequest.pready && !data_out.prequest_ufault &&
          !data_out.prequest_uaccess_ufault && split_requests_seen == 4);
    tick();
    falling();
    data_request_valid = 0;
    for (int cycle = 0; cycle < 160 && split_completions_seen == 2; cycle++)
      tick();
    CHECK(split_requests_seen == 6 && split_completions_seen == 3);
    vector_bad_second = 1;
    clear_translations();
    falling();
    data_request_valid = 1;
    settle();
    CHECK(data_out.prequest.pready && !data_out.prequest_ufault);
    tick();
    falling();
    data_request_valid = 0;
    for (int cycle = 0; cycle < 160 && split_completions_seen == 3; cycle++)
      tick();
    CHECK(split_requests_seen == 7 && split_completions_seen == 4);
    falling();
    split_cross_page = 0;
    split_contained = 1;
    vector_phase = 0;
    privilege = UINT64_C(3);
    satp = 0;
    clear_translations();
    falling();
    data_request_valid = 1;
    settle();
    CHECK(data_out.prequest.pready && !data_memory_out.prequest.pvalid);
    tick();
    falling();
    data_request_valid = 0;
    for (int cycle = 0; cycle < 20 && split_completions_seen == 4; cycle++)
      tick();
    CHECK(split_requests_seen == 8 && split_completions_seen == 5);

    throw Finished{};
  }
}

int main() {
  return run_test([] {
    cycle_limit = 2003;
    try {
      stimulus();
      for (;;)
        rising();
    } catch (const Finished &) {
    }
  });
}
