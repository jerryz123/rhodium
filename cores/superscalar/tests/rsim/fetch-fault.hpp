// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>

constexpr unsigned fetch_kind(unsigned cause) {
  switch (cause) {
  case 0: return 1;
  case 1: return 2;
  case 12: return 3;
  case 20: return 4;
  default: return 7;
  }
}

template <class Fault>
void set_fetch_guest(Fault &fault, bool valid, std::uint64_t address, unsigned access) {
  if constexpr (requires { fault.pguest; })
    fault.pguest = {valid, address, std::uint8_t(access)};
}

template <class Fault>
bool fetch_guest_is(const Fault &fault, bool valid, std::uint64_t address, unsigned access) {
  if constexpr (requires { fault.pguest; })
    return fault.pguest.pguest_uvirtual_uaddress == valid &&
           fault.pguest.pguest_uphysical_uaddress == address && fault.pguest.paccess == access;
  else
    return !valid && !address && !access;
}

template <class Fault>
bool same_fetch_guest(const Fault &a, const Fault &b) {
  if constexpr (requires { a.pguest; })
    return fetch_guest_is(a, b.pguest.pguest_uvirtual_uaddress,
                          b.pguest.pguest_uphysical_uaddress, b.pguest.paccess);
  else
    return true;
}
