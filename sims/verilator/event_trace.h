// Declares native event-export lifecycle operations selected by the simulation runtime.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
namespace rhodium::simulation {
int open_event_trace(const char* path) noexcept;
int export_event_cycle(std::uint64_t cycle) noexcept;
int close_event_trace() noexcept;
}
