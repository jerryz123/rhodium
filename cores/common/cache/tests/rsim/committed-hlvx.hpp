// Exercises aligned committed HLVX permissions at both cores' MMU boundaries.
// SPDX-License-Identifier: Apache-2.0
#pragma once

template <typename Probe> void check_committed_hlvx(Probe probe) {
  constexpr unsigned half = 1, word = 2;
  probe(0x2500, word, false); // Readable, non-executable.
  probe(0x2800, word, false); // Executable, unreadable.
  probe(0x2900, half, true);  // Two readable and executable bytes.
  probe(0x2900, word, false); // Last two bytes fall outside that region.
}
