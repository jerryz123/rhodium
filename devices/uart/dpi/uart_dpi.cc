// Bridges each UART DPI model to a nonblocking raw pseudo-terminal.
// SPDX-License-Identifier: Apache-2.0
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include "uart_dpi.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

namespace {

constexpr std::size_t kIoChunkBytes = 256;

struct UartModel {
  int model_id;
  int master_fd = -1;
  int configuration_fd = -1;
  std::string slave_path;
  std::deque<std::uint8_t> pty_to_uart;
  std::deque<std::uint8_t> uart_to_pty;
  std::uint32_t framing_errors = 0;

  explicit UartModel(int id) : model_id(id) {}

  ~UartModel() {
    if (configuration_fd >= 0) {
      close(configuration_fd);
    }
    if (master_fd >= 0) {
      close(master_fd);
    }
  }

  int open_pty() {
    if (master_fd >= 0) {
      return 0;
    }

    const int opened_master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (opened_master < 0 || grantpt(opened_master) != 0 ||
        unlockpt(opened_master) != 0) {
      if (opened_master >= 0) {
        close(opened_master);
      }
      return 2;
    }

    const char* opened_slave_path = ptsname(opened_master);
    if (opened_slave_path == nullptr) {
      close(opened_master);
      return 2;
    }

    const int slave_fd = open(opened_slave_path, O_RDWR | O_NOCTTY);
    if (slave_fd < 0) {
      close(opened_master);
      return 2;
    }
    termios attributes{};
    if (tcgetattr(slave_fd, &attributes) != 0) {
      close(slave_fd);
      close(opened_master);
      return 3;
    }
    attributes.c_iflag &=
        ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL |
          IXON | IXOFF | IXANY);
    attributes.c_oflag &= ~OPOST;
    attributes.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    attributes.c_cflag &= ~(CSIZE | PARENB);
    attributes.c_cflag |= CS8;
    attributes.c_cc[VMIN] = 1;
    attributes.c_cc[VTIME] = 0;
    if (tcsetattr(slave_fd, TCSANOW, &attributes) != 0) {
      close(slave_fd);
      close(opened_master);
      return 3;
    }
    master_fd = opened_master;
    configuration_fd = slave_fd;
    slave_path = opened_slave_path;
    std::fprintf(stderr,
                 "UART DPI model %d PTY: %s\n",
                 model_id,
                 slave_path.c_str());
    return 0;
  }

  int read_pty() {
    std::array<char, kIoChunkBytes> bytes{};
    while (true) {
      const ssize_t count = read(master_fd, bytes.data(), bytes.size());
      if (count > 0) {
        for (ssize_t index = 0; index < count; ++index) {
          pty_to_uart.push_back(
              static_cast<std::uint8_t>(bytes[static_cast<std::size_t>(index)]));
        }
        continue;
      }
      if (count == 0) {
        return 0;
      }
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EIO) {
        return 0;
      }
      return 4;
    }
  }

  int write_pty() {
    std::array<char, kIoChunkBytes> bytes{};
    while (!uart_to_pty.empty()) {
      const std::size_t chunk = std::min(bytes.size(), uart_to_pty.size());
      for (std::size_t index = 0; index < chunk; ++index) {
        bytes[index] = static_cast<char>(uart_to_pty[index]);
      }
      const ssize_t count = write(master_fd, bytes.data(), chunk);
      if (count > 0) {
        for (ssize_t index = 0; index < count; ++index) {
          uart_to_pty.pop_front();
        }
        continue;
      }
      if (count == 0) {
        return 0;
      }
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EIO) {
        return 0;
      }
      return 4;
    }
    return 0;
  }
};

std::unordered_map<std::int32_t, std::unique_ptr<UartModel>> models;

UartModel& model(int model_id) {
  const auto key = static_cast<std::int32_t>(model_id);
  auto found = models.find(key);
  if (found == models.end()) {
    found = models.emplace(key, std::make_unique<UartModel>(model_id)).first;
  }
  return *found->second;
}

}  // namespace

char uart_pty_tick(int model_id,
                   unsigned char reset,
                   unsigned char uart_to_pty_valid,
                   char uart_to_pty_byte,
                   unsigned char uart_to_pty_framing_error,
                   unsigned char pty_to_uart_ready,
                   unsigned char* pty_to_uart_valid,
                   char* pty_to_uart_byte) {
  if (pty_to_uart_valid == nullptr || pty_to_uart_byte == nullptr) {
    return 1;
  }
  *pty_to_uart_valid = 0;
  *pty_to_uart_byte = 0;

  auto& uart = model(model_id);
  int status = uart.open_pty();
  if (status != 0) {
    return static_cast<char>(status);
  }
  status = uart.read_pty();
  if (status != 0) {
    return static_cast<char>(status);
  }

  if (reset == 0 && uart_to_pty_valid != 0) {
    uart.uart_to_pty.push_back(
        static_cast<std::uint8_t>(uart_to_pty_byte));
    if (uart_to_pty_framing_error != 0) {
      ++uart.framing_errors;
      std::fprintf(stderr,
                   "UART DPI model %d framing error for byte 0x%02x\n",
                   model_id,
                   static_cast<unsigned char>(uart_to_pty_byte));
    }
  }
  status = uart.write_pty();
  if (status != 0) {
    return static_cast<char>(status);
  }

  if (reset == 0 && pty_to_uart_ready != 0 && !uart.pty_to_uart.empty()) {
    *pty_to_uart_valid = 1;
    *pty_to_uart_byte = static_cast<char>(uart.pty_to_uart.front());
    uart.pty_to_uart.pop_front();
  }
  return 0;
}

const char* uart_pty_path(int model_id) {
  auto& uart = model(model_id);
  if (uart.open_pty() != 0) {
    return nullptr;
  }
  return uart.slave_path.c_str();
}

int uart_pty_framing_errors(int model_id) {
  return static_cast<int>(model(model_id).framing_errors);
}
