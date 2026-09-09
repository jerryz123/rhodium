// Loads images through registered native memory or exact target transactions, then uses coherent HTIF.
#include "direct_mem_htif.h"

#include <stdexcept>
#include <algorithm>
#include <cstdio>
#include <limits>
#include <sstream>

namespace rhodium::fesvr {
namespace {

constexpr std::size_t kMaxBytes = sizeof(std::uint64_t);

std::uint64_t load_little_endian(const void* source, std::size_t length) {
  const auto* bytes = static_cast<const std::uint8_t*>(source);
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < length; ++i)
    value |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
  return value;
}

void store_little_endian(std::uint64_t word, void* destination, std::size_t length) {
  auto* bytes = static_cast<std::uint8_t*>(destination);
  for (std::size_t i = 0; i < length; ++i)
    bytes[i] = static_cast<std::uint8_t>(word >> (8 * i));
}

void require_transfer(addr_t address, std::size_t length) {
  if (length == 0 || length > kMaxBytes ||
      address > std::numeric_limits<addr_t>::max() - (length - 1))
    throw std::runtime_error("direct-memory HTIF requires a non-wrapping one-to-eight-byte chunk");
}

void require_range(addr_t address, std::size_t length) {
  if (length != 0 && address > std::numeric_limits<addr_t>::max() - (length - 1))
    throw std::runtime_error("direct-memory HTIF range wraps the address space");
}

}  // namespace

DirectMemoryHtif::DirectMemoryHtif(int argc, char** argv, int expected_xlen,
                                 std::uint64_t boot_address_register, ImageMemoryMap image_memories)
    : htif_t(argc, argv), target_xlen_(expected_xlen),
      boot_address_register_(boot_address_register), image_memories_(std::move(image_memories)) {
  if (expected_xlen != 32 && expected_xlen != 64) {
    throw std::invalid_argument("direct-memory HTIF target XLEN must be 32 or 64");
  }
  if (boot_address_register % kMaxBytes != 0) {
    throw std::invalid_argument("boot-address register must be eight-byte aligned");
  }
  set_expected_xlen(expected_xlen);
  image_memories_.freeze();
  target_context_ = context_t::current();
  host_context_.init(host_thread_main, this);
}

void DirectMemoryHtif::host_thread_main(void* argument) {
  auto* transport = static_cast<DirectMemoryHtif*>(argument);
  try {
    transport->run();
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FESVR transport failed: %s\n", error.what());
    transport->failed_ = true;
  }
  while (true) {
    transport->switch_to_target();
  }
}

void DirectMemoryHtif::tick(bool request_ready,
                            bool response_valid,
                            std::uint64_t response_data,
                            std::uint8_t response_status) {
  if (request_pending_) {
    if (!request_exposed_) {
      request_exposed_ = true;
    } else if (!request_accepted_ && request_ready) {
      request_accepted_ = true;
    }

    if (request_accepted_ && response_valid) {
      response_data_ = response_data;
      response_status_ = response_status;
      request_pending_ = false;
      request_exposed_ = false;
      request_accepted_ = false;
    }
  }

  host_context_.switch_to();
}

bool DirectMemoryHtif::request_valid() const {
  return request_pending_ && request_exposed_ && !request_accepted_;
}

const DirectMemoryRequest& DirectMemoryHtif::request() const {
  return request_;
}

bool DirectMemoryHtif::response_ready() const {
  return request_pending_ && request_accepted_;
}

std::uint32_t DirectMemoryHtif::exit_word() {
  if (failed_) return 3;
  return done() ? (static_cast<std::uint32_t>(exit_code()) << 1) | 1 : 0;
}

void DirectMemoryHtif::reset() {
  // FESVR invokes this startup callback after all blocking image writes finish.
  const auto entry = get_entry_point();
  if (entry == 0 || (target_xlen_ == 32 && entry > UINT32_MAX))
    throw std::runtime_error("boot entry must be nonzero and fit target XLEN");
  loading_ = false;
  // The register is always 64 bits, including on RV32. Wait for final completion.
  transact(true, boot_address_register_, entry, kMaxBytes);
}

void DirectMemoryHtif::read_chunk(addr_t address,
                                  std::size_t length,
                                  void* destination) {
  require_range(address, length);
  auto bytes = std::span(static_cast<std::uint8_t*>(destination), length);
  while (!bytes.empty()) {
    auto segment = loading_ ? image_memories_.segment(address, bytes.size())
                            : ImageMemoryMap::Segment{nullptr, 0, bytes.size()};
    if (segment.region) segment.region->read(segment.offset, bytes.first(segment.length));
    else {
      segment.length = std::min(segment.length, kMaxBytes);
      store_little_endian(transact(false, address, 0, segment.length), bytes.data(), segment.length);
    }
    bytes = bytes.subspan(segment.length);
    if (!bytes.empty()) address += segment.length;
  }
}

void DirectMemoryHtif::write_chunk(addr_t address,
                                   std::size_t length,
                                   const void* source) {
  require_loading_write(address, length);
  auto bytes = std::span(static_cast<const std::uint8_t*>(source), length);
  while (!bytes.empty()) {
    auto segment = loading_ ? image_memories_.segment(address, bytes.size())
                            : ImageMemoryMap::Segment{nullptr, 0, bytes.size()};
    if (segment.region) segment.region->write(segment.offset, bytes.first(segment.length));
    else {
      segment.length = std::min(segment.length, kMaxBytes);
      transact(true, address, load_little_endian(bytes.data(), segment.length), segment.length);
    }
    bytes = bytes.subspan(segment.length);
    if (!bytes.empty()) address += segment.length;
  }
}

void DirectMemoryHtif::clear_chunk(addr_t address, std::size_t length) {
  require_loading_write(address, length);
  while (length != 0) {
    auto segment = loading_ ? image_memories_.segment(address, length)
                            : ImageMemoryMap::Segment{nullptr, 0, length};
    if (segment.region) segment.region->zero(segment.offset, segment.length);
    else {
      segment.length = std::min(segment.length, kMaxBytes);
      transact(true, address, 0, segment.length);
    }
    length -= segment.length;
    if (length != 0) address += segment.length;
  }
}

std::size_t DirectMemoryHtif::chunk_align() {
  // Prevent memif_t from widening reads or synthesizing read-modify-write.
  return 1;
}

std::size_t DirectMemoryHtif::chunk_max_size() {
  return loading_ && !image_memories_.empty() ? 64 * 1024 : kMaxBytes;
}

void DirectMemoryHtif::require_loading_write(addr_t address, std::size_t length) const {
  require_range(address, length);
  if (loading_ && length != 0 && address <= boot_address_register_ + kMaxBytes - 1 &&
      address + length - 1 >= boot_address_register_)
    throw std::runtime_error("loading write overlaps the reserved boot-address register");
}

void DirectMemoryHtif::idle() {
  switch_to_target();
}

std::uint64_t DirectMemoryHtif::transact(bool write,
                                         addr_t address,
                                         std::uint64_t data,
                                         std::size_t length) {
  require_transfer(address, length);
  // Compare inclusive ends after require_transfer checks wrapping; the aligned
  // eight-byte register also fits at the very top of the address space.
  if (loading_ && write && address <= boot_address_register_ + kMaxBytes - 1 &&
      address + length - 1 >= boot_address_register_)
    throw std::runtime_error("loading write overlaps the reserved boot-address register");
  if (request_pending_) {
    throw std::logic_error("direct-memory HTIF permits only one outstanding request");
  }

  request_ = DirectMemoryRequest{
      .write = write,
      .address = address,
      .data = data,
      .length = static_cast<std::uint8_t>(length),
  };
  request_pending_ = true;
  request_exposed_ = false;
  request_accepted_ = false;

  while (request_pending_) {
    switch_to_target();
  }
  if (response_status_ != 0) {
    std::ostringstream message;
    message << (write ? "write" : "read") << " at 0x" << std::hex << address
            << std::dec << " (" << length << " bytes), target status "
            << static_cast<unsigned>(response_status_);
    throw std::runtime_error(message.str());
  }
  return response_data_;
}

void DirectMemoryHtif::switch_to_target() {
  target_context_->switch_to();
}

}  // namespace rhodium::fesvr
