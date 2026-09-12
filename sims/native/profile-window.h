// Enables Linux perf counters only during the benchmark's existing measured
// interval.
#ifndef RHODIUM_PROFILE_WINDOW_H
#define RHODIUM_PROFILE_WINDOW_H
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <unistd.h>
struct ProfileWindow {
  int fd = -1;
  ProfileWindow() {
    if (const char *value = std::getenv("RDS_PERF_CONTROL_FD"))
      fd = std::stoi(value);
  }
  void enable(bool active) const {
    if (fd < 0)
      return;
    const char *command = active ? "enable\n" : "disable\n";
    size_t size = active ? 7 : 8;
    if (write(fd, command, size) != static_cast<ssize_t>(size))
      throw std::runtime_error("cannot control perf measurement window");
  }
};
#endif
