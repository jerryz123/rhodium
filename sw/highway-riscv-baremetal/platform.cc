// Provides counter-based Highway platform timing without hosted calibration or OS services.
// SPDX-License-Identifier: Apache-2.0
#include "hwy/timer.h"
namespace hwy { namespace platform {
double InvariantTicksPerSecond() { return 1.0; }
double Now() {
  uint64_t cycles;
  asm volatile("rdcycle %0" : "=r"(cycles));
  return static_cast<double>(cycles);
}
bool HaveTimerStop(char* cpu) { cpu[0] = 0; return true; }
bool GetCpuString(char* cpu) { cpu[0] = 0; return false; }
uint64_t TimerResolution() { return 1; }
}}  // namespace hwy::platform
