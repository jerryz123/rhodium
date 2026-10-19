// Provides RV5Stage memory-context encodings on the shared rsim scoreboard driver.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../tests/rsim/driver.hpp"

inline std::uint16_t memory_integer(unsigned rd) { return 0x80 | (rd & 31); }
inline std::uint16_t memory_fp(unsigned rd, unsigned precision) {
  return 0x100 | ((rd & 31) << 2) | (precision & 3);
}
inline std::uint16_t memory_vector(unsigned slot) { return 0x180 | (slot & 7); }
inline unsigned memory_rd(unsigned wb) {
  return (wb >> 7) == 2 ? (wb >> 2) & 31 : wb & 31;
}

// Compare logical fields rather than C++ padding in typed memory transactions.
template <class A, class B> bool same_memory_context(const A &a, const B &b) {
  return a.pwriteback == b.pwriteback && a.porigin == b.porigin;
}
template <class A, class B> bool same_memory_request(const A &a, const B &b) {
  return a.pbyte_umask == b.pbyte_umask && a.paddress == b.paddress &&
         a.paccess == b.paccess && a.patomic == b.patomic &&
         a.pwidth == b.pwidth && a.punsigned == b.punsigned &&
         a.pdata == b.pdata && a.plocality == b.plocality &&
         same_memory_context(a.pcontext, b.pcontext);
}
template <class A, class B> bool same_memory_response(const A &a, const B &b) {
  return a.paccess_ufault == b.paccess_ufault && a.pdata == b.pdata &&
         same_memory_context(a.pcontext, b.pcontext);
}
