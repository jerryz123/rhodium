// Connects simulator scheduling, HTIF mailbox policy, and host writes to Sail checking.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include <span>
namespace rhodium::cosim {
void simulation_host_write(std::uint64_t address, std::span<const std::uint8_t> data);
void simulation_htif_mailboxes(std::uint64_t tohost, std::uint64_t fromhost);
int open_simulation(std::int64_t corrupt_order) noexcept;
int begin_sample(std::uint64_t sample) noexcept;
int end_sample() noexcept;
// Freeze the observation prefix and report -1/error, 0/pending, or 1/drained.
int drain_simulation() noexcept;
int finish_simulation() noexcept;
}
