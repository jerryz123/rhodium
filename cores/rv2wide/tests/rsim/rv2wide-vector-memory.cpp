// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void step(unsigned cycles = 1) {
  while (cycles--) {
    eval();
    tick_model();
    eval();
  }
}
void initialize() {
  reset = 1;
  issue_valid = response_valid = split_valid = request_fault = 0;
  certified = packed = is_store = truncate = ordered_busy = 0;
  split_page_fault = split_access_fault = split_guest_fault = 0;
  enabled = last = lookup_valid = commit_ready = completion_ready = 1;
  request_ready = split_ready = 1;
  lookup_outcome = 1;
  lookup_data = UINT64_C(0x8877665544332211);
  width = 0;
  byte_mask = 2;
  address = 0x1001;
  physical_address = 0x8001;
  pc = 0x4000;
  tag = 0;
  store_data = 0xab;
  step(2);
  reset = 0;
  eval();
  CHECK(idle);
  CHECK(memory_start_ready);
}
void launch() {
  issue_valid = 1;
  eval();
  CHECK(issue_ready);
  step();
  issue_valid = 0;
  eval();
}
void precise_wb() {
  launch();
  CHECK(!feedback_valid);
  step(2);
  CHECK(feedback_valid);
}
void drain() {
  step(2);
  CHECK(idle);
}
int main() {
  return run_test([] {
    // Faultable stores are a fixed pipeline, not one outstanding transaction.
    // A rejected/faulting middle commit flushes younger attempts before side effects.
    for (int reject = 0; capacity >= 4 && reject < 3; ++reject) {
      initialize();
      is_store = 1;
      lookup_outcome = 2;
      int accepted = 0;
      for (int cycle = 0; cycle < 9; ++cycle) {
        issue_valid = cycle < 4;
        tag = cycle % unsigned(capacity);
        address = 0x1000 + cycle;
        last = cycle == 3;
        commit_ready = !(reject == 1 && cycle == 4);
        lookup_outcome = reject == 2 && cycle == 3 ? 4 : 2;
        eval();
        if (cycle < 3)
          CHECK(!feedback_valid);
        if (cycle == 3 || (!reject && cycle >= 4 && cycle <= 6)) {
          CHECK(feedback_valid && feedback.pdisposition == 0 && committed);
          CHECK(feedback.ptag == unsigned(cycle - 3) % unsigned(capacity));
          accepted++;
        } else if (reject && cycle == 4) {
          CHECK(feedback_valid && feedback.pdisposition == unsigned(reject) &&
                feedback.ptag == 1 % unsigned(capacity) && !issue_ready);
          if (reject == 2)
            CHECK(!committed && outcome_valid);
        } else if (cycle >= 5 && reject) {
          CHECK(!feedback_valid && !committed && !requested);
        }
        step();
      }
      CHECK(accepted == (reject ? 1 : 4));
      drain();
    }

    initialize();
    const auto raw = std::uint64_t(lookup_data);
    const auto full_word = raw & low_mask(word_bytes * 8);

    // Prepared packed loads launch in their issue cycle and return two cycles
    // later. Precise fallback and certified capture keep distinct feedback.
    for (unsigned certificate = 0; certificate < 2; ++certificate) {
      initialize();
      certified = certificate;
      packed = 1;
      address = physical_address = 0x8000;
      width = word_bytes == 8 ? 3 : 2;
      byte_mask = low_mask(word_bytes);
      issue_valid = 1;
      eval();
      CHECK(issue_ready && lookup && lookup_address == 0x8000);
      CHECK(!memory_start_ready);
      step();
      issue_valid = 0;
      eval();
      CHECK(!lookup && !completion_valid);
      CHECK(bool(feedback_valid) == bool(certificate));
      step();
      if (certificate)
        CHECK(completion_valid && completed.ptag == 0 && completed.pdata == full_word);
      else
        CHECK(feedback_valid && !feedback.pslow && feedback.pdata == full_word);
      CHECK(bool(memory_start_ready) == bool(certificate));
      if (certificate) {
        completion_ready = 0;
        eval();
        CHECK(!memory_start_ready); // A blocked hit is a retry, not release credit.
        completion_ready = 1;
        eval();
        CHECK(memory_start_ready);
      }
      drain();
    }

    if (capacity > 1) {
      // Older ordinary EX ownership wins over a younger packed bypass.
      // A rejected store rewinds both retained attempts, not just its own tag.
      for (unsigned rejection = 0; rejection < 2; ++rejection) {
        initialize();
        certified = packed = is_store = 1;
        address = physical_address = 0x8000;
        width = word_bytes == 8 ? 3 : 2;
        byte_mask = low_mask(word_bytes);
        lookup_outcome = 2;
        launch();
        CHECK(lookup && lookup_address == 0x8000);
        is_store = 0;
        tag = 1;
        address = physical_address = 0x9000;
        issue_valid = 1;
        eval();
        CHECK(issue_ready && lookup && lookup_address == 0x8000);
        step();
        issue_valid = 0;
        eval();
        CHECK(lookup && lookup_address == 0x9000);
        commit_ready = !rejection;
        step();
        CHECK(committed && !completion_valid);
        if (rejection) {
          CHECK(!store_completed_valid);
          step();
          CHECK(!completion_valid);
          commit_ready = 1;
          step();
          CHECK(lookup && lookup_address == 0x8000);
          step();
          CHECK(lookup && lookup_address == 0x9000 && !completion_valid);
          step();
          CHECK(committed && store_completed_valid && store_completed_tag == 0);
        } else {
          CHECK(store_completed_valid && store_completed_tag == 0);
        }
        lookup_outcome = 1;
        step();
        CHECK(completion_valid && completed.ptag == 1 && completed.pdata == full_word);
        drain();
      }
    }

    initialize();
    // Subword lookup and delayed responses both extract the retained byte offset.
    precise_wb();
    CHECK(outcome_valid && feedback.pdisposition == 0 && !feedback.pslow);
    CHECK(feedback.pdata == ((raw >> 8) & 255) && feedback.ppc == 0x4000);
    drain();
    lookup_outcome = 0;
    precise_wb();
    CHECK(requested && feedback.pslow && feedback.pdisposition == 0);
    CHECK(request_address == 0x1001 && request_mask == 2);
    step();
    address = 0x2000;
    response_valid = 1;
    response_data = raw;
    completion_ready = 0;
    eval();
    CHECK(completion_valid && !response_ready && completed.pdata == ((raw >> 8) & 255));
    step(3);
    CHECK(completion_valid && completed.ptag == 0 && !idle);
    completion_ready = 1;
    eval();
    CHECK(response_ready);
    step();
    response_valid = 0;
    drain();

    // A rejected attempt replays without reserving a response or retiring a macro.
    request_ready = 0;
    precise_wb();
    CHECK(feedback.pdisposition == 1 && !outcome_valid && !feedback.pslow);
    drain();
    request_ready = 1;
    request_fault = 1;
    fault_cause = 13;
    fault_address = 0x3000;
    precise_wb();
    CHECK(feedback.pdisposition == 2 && feedback.pfault_uaddress == 0x3000);
    CHECK(feedback.pcause == 13 && !feedback.pslow && outcome_valid);
    drain();
    request_fault = 0;

    // Stores position byte data for the raw-word cache port and retry failed commits.
    is_store = 1;
    address = 0x1001;
    lookup_outcome = 2;
    launch();
    CHECK(lookup && lookup_store == 0xab00);
    step(2);
    CHECK(committed && feedback_valid && feedback.pdisposition == 0);
    drain();
    commit_ready = 0;
    precise_wb();
    CHECK(committed && feedback.pdisposition == 1);
    drain();
    commit_ready = 1;

    // Split service owns the attempt until its outcome; it receives unshifted data.
    lookup_outcome = 0;
    address = 0x1fff;
    width = 1;
    store_data = 0x1234;
    launch();
    CHECK(!lookup);
    step(2);
    CHECK(split_requested && !feedback_valid && split_store == 0x1234);
    step();
    eval();
    CHECK(!issue_ready && !idle);
    step(4);
    split_valid = 1;
    split_page_fault = 1;
    split_fault_address = 0x2000;
    eval();
    CHECK(feedback_valid && outcome_valid && feedback.pdisposition == 2);
    CHECK(feedback.pcause == 15 && feedback.pfault_uaddress == 0x2000);
    CHECK(!completion_valid);
    step();
    split_valid = split_page_fault = 0;
    drain();

    // Fault-first guest failure keeps the faulting fragment, not the element base.
    is_store = 0;
    truncate = 1;
    launch();
    step(3);
    split_valid = split_guest_fault = 1;
    eval();
    CHECK(feedback_valid && feedback.pdisposition == 3 && feedback.pcause == 21);
    CHECK(feedback.pfault_uaddress == 0x2000 && guest.pguest_uvirtual_uaddress);
    CHECK(guest.pguest_uphysical_uaddress == 0x2345);
    step();
    split_valid = split_guest_fault = truncate = 0;
    drain();
    // A successful split authorizes first, then returns its already aligned value.
    launch();
    step(3);
    split_data = 0x4321;
    split_valid = 1;
    eval();
    CHECK(feedback_valid && feedback.pslow && feedback.pdisposition == 0);
    step();
    split_valid = 0;
    completion_ready = 0;
    eval();
    CHECK(completion_valid && completed.pdata == 0x4321);
    step(2);
    completion_ready = 1;
    drain();

    initialize();
    enabled = 0;
    lookup_outcome = 4;
    request_ready = 0;
    launch();
    CHECK(!lookup);
    step(2);
    CHECK(feedback_valid && feedback.pdisposition == 0 && outcome_valid);
    CHECK(!requested && !committed && !split_requested);
    drain();

    // A certificate makes capture irrevocable. Cache replay stays inside the LSU,
    // even if live scalar controls and the next descriptor change afterward.
    initialize();
    certified = 1;
    lookup_outcome = 3;
    launch();
    CHECK(feedback_valid && feedback.pslow && feedback.pdisposition == 0);
    CHECK(lookup && lookup_physical && lookup_address == 0x8001);
    pc = 0x5000;
    is_store = 1;
    physical_address = 0x9000;
    step();
    CHECK(!feedback_valid);
    step(5);
    CHECK(!feedback_valid && !outcome_valid && !idle);
    lookup_outcome = 1;
    completion_ready = 0;
    for (unsigned i = 0; !completion_valid && i < 12; ++i)
      step();
    CHECK(completion_valid && completed.pdata == ((raw >> 8) & 255));
    completion_ready = 1;
    step();
    drain();

    // Store-hit completion bypasses load-return backpressure entirely.
    initialize();
    certified = is_store = 1;
    lookup_outcome = 2;
    completion_ready = 0;
    launch();
    CHECK(feedback_valid && feedback.pslow);
    step(2);
    CHECK(committed && store_completed_valid && store_completed_tag == 0);
    step();
    CHECK(!committed && !completion_valid && !store_completed_valid);
    step(5);
    CHECK(!committed && !completion_valid && !store_completed_valid && idle);
    completion_ready = 1;
    drain();

    if (capacity > 1) {
      // A store hit and an older load response complete together even when
      // the data-return lane was backpressured up to that commit cycle.
      initialize();
      certified = 1;
      lookup_outcome = 0;
      completion_ready = 0;
      launch();
      step(2);
      CHECK(requested);
      step();
      is_store = 1;
      tag = 1;
      lookup_outcome = 2;
      launch();
      step(2);
      response_valid = 1;
      response_data = raw;
      eval();
      CHECK(committed && store_completed_valid && store_completed_tag == 1);
      CHECK(completion_valid && completed.ptag == 0 && !response_ready);
      completion_ready = 1;
      eval();
      CHECK(store_completed_valid && completion_valid && response_ready);
      step();
      response_valid = 0;
      drain();

      // Retrying a younger certified beat must neither reissue the accepted
      // prefix nor erase its independently backpressured response owner.
      initialize();
      certified = 1;
      lookup_outcome = 0;
      completion_ready = 0;
      launch();
      CHECK(feedback_valid && feedback.ptag == 0);
      tag = 1;
      physical_address = 0x9002;
      address = 0x2002;
      launch();
      CHECK(feedback_valid && feedback.ptag == 1);
      step();
      CHECK(requested && request_address == 0x8001);
      step();
      request_ready = 0;
      response_valid = 1;
      response_data = raw;
      eval();
      CHECK(requested && request_address == 0x9002 && !feedback_valid);
      CHECK(completion_valid && completed.ptag == 0 && !response_ready);
      step();
      request_ready = 1;
      for (unsigned wait = 0; !requested && wait < 12; ++wait)
        step();
      CHECK(requested && request_address == 0x9002);
      CHECK(completion_valid && completed.ptag == 0 && completed.pdata == ((raw >> 8) & 255));
      completion_ready = 1;
      step();
      response_valid = 0;
      step(2);
      CHECK(!requested && !idle);
      response_valid = 1;
      response_data = raw;
      eval();
      CHECK(completion_valid && completed.ptag == 1 && completed.pdata == ((raw >> 16) & 255));
      step();
      response_valid = 0;
      drain();
    }
  });
}
