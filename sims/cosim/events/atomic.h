// Normalizes an observed shared-cache AMO's written bytes without executing a reference instruction.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "record.h"
#include <algorithm>
#include <stdexcept>

namespace rhodium::cosim::observation {
inline Word atomic_written_value(Word operation, Word old, Word operand, unsigned width) {
  if (width != 32 && width != 64) throw std::runtime_error("invalid observed AMO width");
  const Word mask = width == 64 ? UINT64_MAX : UINT32_MAX;
  const Word left = old & mask, right = operand & mask, sign = Word{1} << (width-1);
  const bool less = (left ^ sign) < (right ^ sign);
  Word result;
  switch (operation) { // RiscvAtomicOperation's transport encoding.
    case 0: result = right; break;
    case 1: result = left + right; break;
    case 2: result = left ^ right; break;
    case 3: result = left & right; break;
    case 4: result = left | right; break;
    case 5: result = less ? left : right; break;
    case 6: result = less ? right : left; break;
    case 7: result = std::min(left,right); break;
    case 8: result = std::max(left,right); break;
    default: throw std::runtime_error("invalid observed AMO operation");
  }
  return result & mask;
}
}
