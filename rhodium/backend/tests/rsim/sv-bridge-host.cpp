// SPDX-License-Identifier: Apache-2.0
// This same callback is called by generated rsim C++ and by direct SV DPI.
#include <svdpi.h>
#include <vpi_user.h>
// The runner includes this build's DPI declarations in the native source wrapper.
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>

extern "C" void rsim_bridge_observe(svBit reset, char raw_step, char raw_count) {
  const auto step = static_cast<unsigned char>(raw_step);
  const auto count = static_cast<unsigned char>(raw_count);
  const auto scope = svGetScope();
  const auto* name = scope ? svGetNameFromScope(scope) : nullptr;
  if (!name || std::string(name).empty()) throw std::runtime_error("missing callback scope");
  s_vpi_vlog_info info{};
  if (!vpi_get_vlog_info(&info)) throw std::runtime_error("missing VPI arguments");
  bool token = false;
  for (int index = 0; index < info.argc; ++index)
    token |= std::string(info.argv[index]) == "+bridge-token=42";
  if (!token) throw std::runtime_error("missing bridge token");

  struct Observation { unsigned calls = 0; unsigned expected = 0; };
  static std::map<std::string, Observation> observations;
  auto& observation = observations[name];
  // State before the first reset is not a portable backend guarantee.
  if (observation.calls && count != observation.expected)
    throw std::runtime_error("callback did not sample old counter state");
  observation.expected = reset ? 0 : (count + step) % 256;
  std::printf("CALL %s %u %u %u %u\n", name, observation.calls++, reset, step,
              reset ? 0 : count);
}
