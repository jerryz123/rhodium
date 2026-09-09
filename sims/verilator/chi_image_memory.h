// Adapts CHI-owned byte storage to the simulation loader without coupling CHI to FESVR.
#pragma once

#include "chi_memory.h"
#include "image_memory.h"

namespace rhodium::simulation {

inline fesvr::ImageMemoryMap chi_image_memory_map() {
  fesvr::ImageMemoryMap map;
  for (const auto& entry : chi::freeze_memory_registrations()) {
    auto& memory = *entry.storage;
    map.add({entry.owner, entry.base, memory.size(), memory.size(), 0,
           [&memory](auto offset, auto bytes) { memory.read(offset, bytes); },
           [&memory](auto offset, auto bytes) { memory.write(offset, bytes); },
           [&memory](auto offset, auto length) { memory.zero(offset, length); }});
  }
  return map;
}

}  // namespace rhodium::simulation
