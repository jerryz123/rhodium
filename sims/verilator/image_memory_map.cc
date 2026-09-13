// Collects reset-initialized native RAM models for the selected simulator build.
// SPDX-License-Identifier: Apache-2.0
#include "image_memory.h"
#ifdef RHODIUM_CHI_MEMORY
#include "chi_image_memory.h"
#endif

namespace rhodium::simulation {
fesvr::ImageMemoryMap make_image_memory_map() {
#ifdef RHODIUM_CHI_MEMORY
  return chi_image_memory_map();
#else
  return {};
#endif
}
}  // namespace rhodium::simulation
