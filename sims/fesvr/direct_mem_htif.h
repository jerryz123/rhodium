// Declares exact-width, one-outstanding FESVR transactions and target error reporting.
#pragma once

#include <cstddef>
#include <cstdint>

#include <fesvr/context.h>
#include <fesvr/htif.h>

namespace rhodium::fesvr {

struct DirectMemoryRequest {
  bool write;
  std::uint64_t address;
  std::uint64_t data;
  std::uint8_t length;
};

class DirectMemoryHtif : public htif_t {
 public:
  DirectMemoryHtif(int argc, char** argv, int expected_xlen);
  ~DirectMemoryHtif() override = default;

  void tick(bool request_ready,
            bool response_valid,
            std::uint64_t response_data,
            std::uint8_t response_status,
            bool start_ready);

  bool request_valid() const;
  const DirectMemoryRequest& request() const;
  bool response_ready() const;

  bool start_valid() const;
  std::uint64_t start_entry() const;

  std::uint32_t exit_word();

 protected:
  void reset() override;
  void read_chunk(addr_t address, std::size_t length, void* destination) override;
  void write_chunk(addr_t address, std::size_t length, const void* source) override;
  void clear_chunk(addr_t address, std::size_t length) override;
  std::size_t chunk_align() override;
  std::size_t chunk_max_size() override;
  void idle() override;

 private:
  static void host_thread_main(void* argument);

  std::uint64_t transact(bool write, addr_t address, std::uint64_t data, std::size_t length);
  void switch_to_target();

  context_t host_context_;
  context_t* target_context_ = nullptr;

  DirectMemoryRequest request_ = {};
  bool request_pending_ = false;
  bool request_exposed_ = false;
  bool request_accepted_ = false;
  std::uint64_t response_data_ = 0;
  std::uint8_t response_status_ = 0;
  bool failed_ = false;

  bool start_pending_ = false;
  bool start_exposed_ = false;
  std::uint64_t start_entry_ = 0;
};

}  // namespace rhodium::fesvr
