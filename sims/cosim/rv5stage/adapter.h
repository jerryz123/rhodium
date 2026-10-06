// Reconstructs one RV5Stage hart's ordered scalar, FP, and vector observations.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "vector.h"
#include <array>
#include <type_traits>

namespace rhodium::cosim::rv5stage {
struct ScalarCycle { Word xlen, flen, vector, wb_retained_memory_accepted, wb_reservation_wait_accept, wb_vector_admission, wb_vector_return, wb_commit_valid, wb_memory_pending, wb_reservation_wait_pending, wb_pending_exception_valid, wb_current_exception_wait, wb_memory_accepted, wb_attempt, wb_data_fault, wb_split_accepted, wb_split_done, wb_commit_has_multiply, wb_commit_has_divide, response_fire, write_valid, fault_address, commit_rd, commit_value, write_rd, write_data, offer_request_valid; };
struct HeaderSample { Word pc, instruction, exception, cause; };
struct BoundarySample { Word csr_interrupt_take, csr_synchronous_trap, csr_commit, csr_retired, privilege, virtualized, cause, epc, tval, target_pc, target_privilege, target_virtualized, guest_valid, htval, htinst, next_pc, next_privilege, next_virtualized, interrupts, time, interrupt_boundary, hpm_enabled, hpm_counter, hpm_overflow; };
struct RequestSample { Word index, address, access, width, atomic, data, writeback, integer, fp, rd; };
struct ResponseSample { Word data, fault, writeback, fp, split_data, split_fault, split_fault_address; };
struct PhysicalSample { Word valid, address, pipeline_valid, pipeline_address, fragment_valid, fragment_address, fragment_virtual; };
struct ArithmeticSample { Word index, accepted, issue_rd, completed, rd, data; };
struct FpSample { Word issued, completed, issue_fp, issue_integer, issue_rd, complete_fp, complete_integer, rd, integer_value, fp_value, flags_valid, exception_flags, fp_load_valid, fp_load_rd, fp_load_data; };

// All captures are pre-edge facts. One native owner orders scalar and vector
// events; no callback can allocate identities or depend on callback order.
class HartAdapter final : public observation::DpiAdapter {
 public:
  VectorAdapter vector;
  template<class Event> void capture(Word sample, Word instance, Word epoch, Event event) {
    auto& frame = frames_[instance];
    if (!frame.sample) { frame.sample = sample; frame.epoch = epoch; }
    require(*frame.sample == sample && frame.epoch == epoch, "unflushed hart sample or mixed epoch");
    if constexpr (std::is_same_v<Event, RequestSample>) {
      require(event.index < 2 && !frame.requests[event.index], "duplicate request sample");
      frame.requests[event.index] = event;
    } else if constexpr (std::is_same_v<Event, ArithmeticSample>) {
      require(event.index < 2 && !frame.arithmetic[event.index], "duplicate arithmetic sample");
      frame.arithmetic[event.index] = event;
    } else {
      auto& field = std::get<std::optional<Event>>(frame.events);
      require(!field, "duplicate hart callback");
      field = event;
    }
  }
  void flush(observation::Collector& collector) override;

 private:
  using Events = std::tuple<std::optional<ScalarCycle>, std::optional<HeaderSample>, std::optional<BoundarySample>, std::optional<ResponseSample>, std::optional<PhysicalSample>, std::optional<FpSample>>;
  struct Frame {
    std::optional<Word> sample; Word epoch = 0; Events events;
    std::array<std::optional<RequestSample>, 2> requests;
    std::array<std::optional<ArithmeticSample>, 2> arithmetic;
  };
  struct Physical { bool valid = false; Word address = 0; bool split = false; Word second = 0; };
  struct MemoryOwner { Id id; RequestSample request; Physical physical; };
  struct ArithmeticOwner { Id id; Word rd; };
  struct Hart {
    Word epoch = 0, xlen = 0, flen = 0, vector = 0, next = 0, cycles = 0;
    Word hpm_overflows = 0, hpm_previous_overflow = 0;
    std::optional<Id> retained, exception, vector_retained;
    Physical hit, split_physical;
    std::optional<RequestSample> split_request;
    std::deque<MemoryOwner> memory;
    std::array<std::deque<ArithmeticOwner>, 2> arithmetic;
    std::array<std::optional<Id>, 32> fp_owners;
    std::deque<ArithmeticOwner> fp_integer;
  };
  std::map<Word, Frame> frames_;
  std::map<Word, Hart> harts_;
  static void require(bool condition, const char* message);
  static Word mask(Word width);
  static Word atomic(Word xlen, const RequestSample& request, Word value);
  static void memory(observation::Collector& collector, Word instance, Id id, Word xlen,
                     const RequestSample& request, Word value, bool fault,
                     const Physical& physical, Word fault_address);
  void resolve(observation::Collector& collector, Word instance, Hart& hart, const Frame& frame);
};
}

extern "C" {
void rhodium_rv5stage_scalar_cycle(std::int64_t instance, std::int64_t xlen, std::int64_t flen, std::int64_t vector, std::int64_t wb_retained_memory_accepted, std::int64_t wb_reservation_wait_accept, std::int64_t wb_vector_admission, std::int64_t wb_vector_return, std::int64_t wb_commit_valid, std::int64_t wb_memory_pending, std::int64_t wb_reservation_wait_pending, std::int64_t wb_pending_exception_valid, std::int64_t wb_current_exception_wait, std::int64_t wb_memory_accepted, std::int64_t wb_attempt, std::int64_t wb_data_fault, std::int64_t wb_split_accepted, std::int64_t wb_split_done, std::int64_t wb_commit_has_multiply, std::int64_t wb_commit_has_divide, std::int64_t response_fire, std::int64_t write_valid, std::int64_t fault_address, std::int64_t commit_rd, std::int64_t commit_value, std::int64_t write_rd, std::int64_t write_data, std::int64_t offer_request_valid) noexcept;
void rhodium_rv5stage_header(std::int64_t instance, std::int64_t pc, std::int64_t instruction, std::int64_t exception, std::int64_t cause) noexcept;
void rhodium_rv5stage_boundary(std::int64_t instance, std::int64_t csr_interrupt_take, std::int64_t csr_synchronous_trap, std::int64_t csr_commit, std::int64_t csr_retired, std::int64_t privilege, std::int64_t virtualized, std::int64_t cause, std::int64_t epc, std::int64_t tval, std::int64_t target_pc, std::int64_t target_privilege, std::int64_t target_virtualized, std::int64_t guest_valid, std::int64_t htval, std::int64_t htinst, std::int64_t next_pc, std::int64_t next_privilege, std::int64_t next_virtualized, std::int64_t interrupts, std::int64_t time, std::int64_t interrupt_boundary, std::int64_t hpm_enabled, std::int64_t hpm_counter, std::int64_t hpm_overflow) noexcept;
void rhodium_rv5stage_request(std::int64_t instance, std::int64_t index, std::int64_t address, std::int64_t access, std::int64_t width, std::int64_t atomic, std::int64_t data, std::int64_t writeback, std::int64_t integer, std::int64_t fp, std::int64_t rd) noexcept;
void rhodium_rv5stage_response(std::int64_t instance, std::int64_t data, std::int64_t fault, std::int64_t writeback, std::int64_t fp, std::int64_t split_data, std::int64_t split_fault, std::int64_t split_fault_address) noexcept;
void rhodium_rv5stage_physical(std::int64_t instance, std::int64_t valid, std::int64_t address, std::int64_t pipeline_valid, std::int64_t pipeline_address, std::int64_t fragment_valid, std::int64_t fragment_address, std::int64_t fragment_virtual) noexcept;
void rhodium_rv5stage_arithmetic(std::int64_t instance, std::int64_t index, std::int64_t accepted, std::int64_t issue_rd, std::int64_t completed, std::int64_t rd, std::int64_t data) noexcept;
void rhodium_rv5stage_fp(std::int64_t instance, std::int64_t issued, std::int64_t completed, std::int64_t issue_fp, std::int64_t issue_integer, std::int64_t issue_rd, std::int64_t complete_fp, std::int64_t complete_integer, std::int64_t rd, std::int64_t integer_value, std::int64_t fp_value, std::int64_t flags_valid, std::int64_t exception_flags, std::int64_t fp_load_valid, std::int64_t fp_load_rd, std::int64_t fp_load_data) noexcept;
}
