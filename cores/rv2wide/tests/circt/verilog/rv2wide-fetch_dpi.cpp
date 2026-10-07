// Binds and validates compiler-instrumented RV2Wide fetch occurrences during the functional oracle.
// SPDX-License-Identifier: Apache-2.0
#include "../../../../../rheg/runtime/rheg.h"
#include "rv2wide-fetch_manifest.h"

// Installs the exact fixture schema before the first reset and transfer callbacks.
extern "C" void rv2wide_fetch_trace_bind() {
  rheg::graph().bind_manifest(rheg_generated::manifest());
}

// Checks completed occurrence identities, payloads, and lineage against that schema.
extern "C" void rv2wide_fetch_trace_finish() {
  (void)rheg::graph().snapshot();
}
