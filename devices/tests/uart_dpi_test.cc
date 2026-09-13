// Exercises PTY creation, raw I/O, reset buffering, and model isolation.
// SPDX-License-Identifier: Apache-2.0
#include "uart_dpi.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <string>

namespace {

int open_slave(int model_id) {
  const char* path = uart_pty_path(model_id);
  assert(path != nullptr);
  const int descriptor = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
  assert(descriptor >= 0);
  return descriptor;
}

void check_raw_mode(int descriptor) {
  termios attributes{};
  assert(tcgetattr(descriptor, &attributes) == 0);
  assert((attributes.c_lflag & (ECHO | ICANON)) == 0);
  assert((attributes.c_oflag & OPOST) == 0);
}

void write_byte(int descriptor, std::uint8_t value) {
  const char byte = static_cast<char>(value);
  ssize_t count;
  do {
    count = write(descriptor, &byte, 1);
  } while (count < 0 && errno == EINTR);
  assert(count == 1);
}

std::uint8_t read_byte(int descriptor) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    char byte = 0;
    const ssize_t count = read(descriptor, &byte, 1);
    if (count == 1) {
      return static_cast<std::uint8_t>(byte);
    }
    assert(count < 0);
    assert(errno == EAGAIN || errno == EWOULDBLOCK || errno == EIO);
    usleep(1000);
  }
  assert(false);
  return 0;
}

void check_no_byte(int descriptor) {
  char byte = 0;
  const ssize_t count = read(descriptor, &byte, 1);
  assert(count < 0);
  assert(errno == EAGAIN || errno == EWOULDBLOCK || errno == EIO);
}

int tick(int model_id,
         bool reset,
         bool uart_to_pty_valid,
         std::uint8_t uart_to_pty_byte,
         bool framing_error,
         bool pty_to_uart_ready,
         unsigned char* pty_to_uart_valid,
         char* pty_to_uart_byte) {
  return uart_pty_tick(model_id,
                       reset ? 1 : 0,
                       uart_to_pty_valid ? 1 : 0,
                       static_cast<char>(uart_to_pty_byte),
                       framing_error ? 1 : 0,
                       pty_to_uart_ready ? 1 : 0,
                       pty_to_uart_valid,
                       pty_to_uart_byte);
}

}  // namespace

int main() {
  constexpr int kFirstModel = 7001;
  constexpr int kSecondModel = 7002;
  const std::string first_path = uart_pty_path(kFirstModel);
  const std::string second_path = uart_pty_path(kSecondModel);
  assert(first_path != second_path);

  const int first_slave = open_slave(kFirstModel);
  const int second_slave = open_slave(kSecondModel);
  check_raw_mode(first_slave);
  check_raw_mode(second_slave);

  unsigned char valid = 0;
  char value = 0;
  write_byte(first_slave, 0xa5);
  assert(tick(kFirstModel, true, true, 0x44, false, true, &valid, &value) == 0);
  assert(valid == 0);
  check_no_byte(first_slave);
  assert(tick(kFirstModel, false, false, 0, false, true, &valid, &value) == 0);
  assert(valid == 1 && static_cast<std::uint8_t>(value) == 0xa5);

  write_byte(second_slave, 0x3c);
  assert(tick(kFirstModel, false, false, 0, false, true, &valid, &value) == 0);
  assert(valid == 0);
  assert(tick(kSecondModel, false, false, 0, false, true, &valid, &value) == 0);
  assert(valid == 1 && static_cast<std::uint8_t>(value) == 0x3c);

  assert(tick(kFirstModel, false, true, 0xa6, false, false, &valid, &value) == 0);
  assert(read_byte(first_slave) == 0xa6);
  assert(tick(kFirstModel, false, false, 0, false, true, &valid, &value) == 0);
  assert(valid == 0);
  check_no_byte(first_slave);

  assert(tick(kFirstModel, false, true, 0x55, true, false, &valid, &value) == 0);
  assert(read_byte(first_slave) == 0x55);
  assert(uart_pty_framing_errors(kFirstModel) == 1);
  assert(uart_pty_framing_errors(kSecondModel) == 0);
  assert(first_path == uart_pty_path(kFirstModel));

  assert(uart_pty_tick(kFirstModel,
                       0,
                       0,
                       0,
                       0,
                       0,
                       nullptr,
                       &value) == 1);
  close(first_slave);
  close(second_slave);
}
