// Provides test-only access to the production UART model's slave PTY.
// SPDX-License-Identifier: Apache-2.0
#include "../../../devices/dpi/uart_dpi.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <unordered_map>

namespace {

std::unordered_map<int, int> slave_descriptors;

int descriptor(int model_id) {
  const auto found = slave_descriptors.find(model_id);
  return found == slave_descriptors.end() ? -1 : found->second;
}

}  // namespace

extern "C" int uart_test_connect(int model_id) {
  if (descriptor(model_id) >= 0) {
    return 0;
  }
  const char* path = uart_pty_path(model_id);
  if (path == nullptr) {
    return 1;
  }
  const int opened = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (opened < 0) {
    return 1;
  }
  slave_descriptors.emplace(model_id, opened);
  return 0;
}

extern "C" int uart_test_write(int model_id, int value) {
  const int opened = descriptor(model_id);
  if (opened < 0 || value < 0 || value > 0xff) {
    return 1;
  }
  const char byte = static_cast<char>(value);
  ssize_t count;
  do {
    count = write(opened, &byte, 1);
  } while (count < 0 && errno == EINTR);
  return count == 1 ? 0 : 1;
}

extern "C" int uart_test_read(int model_id) {
  const int opened = descriptor(model_id);
  if (opened < 0) {
    return -2;
  }
  char byte = 0;
  ssize_t count;
  do {
    count = read(opened, &byte, 1);
  } while (count < 0 && errno == EINTR);
  if (count == 1) {
    return static_cast<int>(static_cast<std::uint8_t>(byte));
  }
  if (count < 0 &&
      (errno == EAGAIN || errno == EWOULDBLOCK || errno == EIO)) {
    return -1;
  }
  return -2;
}
