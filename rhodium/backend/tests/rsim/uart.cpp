// SPDX-License-Identifier: Apache-2.0
#include "devices/uart/dpi/uart_dpi.h"
#ifdef RSIM_REFERENCE
#include "VRsimUartPair.h"
#include "VRsimUartPair__Dpi.h"
using Model = VRsimUartPair;
#else
#include "RsimUartPair.hpp"
using Model = rsim_pRsimUartPair::Model;
#endif
#include <array>
#include <cstdint>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <string>

extern "C" int uart_test_connect(int);
extern "C" int uart_test_write(int, int);
extern "C" int uart_test_read(int);

static void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

struct Channel {
  int id, clocks_per_bit;
  std::deque<int> serial_expected, host_expected;
  int countdown = 0, bit = -1, byte = 0;
  unsigned serial_count = 0, host_count = 0;

  void observe(bool line, bool reset) {
    if (reset) {
      countdown = 0;
      require(line, "serial output active during reset");
      return;
    }
    if (countdown == 0) {
      if (!line) {
        require(!serial_expected.empty(), "unexpected serial frame on model " + std::to_string(id));
        bit = -1; byte = 0; countdown = clocks_per_bit / 2;
      }
      return;
    }
    if (--countdown != 0) return;
    if (bit == -1) require(!line, "bad start bit");
    else if (bit < 8) byte |= int(line) << bit;
    else {
      require(line, "bad stop bit");
      require(byte == serial_expected.front(), "serial byte order/value mismatch on model " + std::to_string(id) + ": got " + std::to_string(byte) + " expected " + std::to_string(serial_expected.front()));
      serial_expected.pop_front(); ++serial_count;
      return;
    }
    ++bit; countdown = clocks_per_bit;
  }

  void poll() {
    for (;;) {
      const int value = uart_test_read(id);
      if (value == -1) return;
      require(value >= 0, "PTY read failed");
      require(!host_expected.empty(), "unexpected PTY byte on model " + std::to_string(id));
      require(value == host_expected.front(), "PTY byte order/value mismatch");
      host_expected.pop_front(); ++host_count;
    }
  }
};

int main() {
  try {
    Model dut;
    std::array<Channel, 2> channels{{{7, 16, {}, {}}, {8, 32, {}, {}}}};
    bool reset = true, connected = false;
    std::array<bool, 2> tx{{true, true}};
    unsigned cycles = 0;
    const auto cycle = [&]() {
      require(++cycles < 30000, "UART simulation timed out");
#ifdef RSIM_REFERENCE
      dut.clock = 0;
      dut.reset = reset;
      dut.first_tx = tx[0]; dut.second_tx = tx[1];
      dut.eval(); dut.eval();
      dut.clock = 1; dut.eval();
      const std::array<bool, 2> rx{{bool(dut.first_rx), bool(dut.second_rx)}};
#else
      dut.inputs.preset = reset;
      dut.inputs.pfirst_utx = tx[0]; dut.inputs.psecond_utx = tx[1];
      dut.eval(); dut.eval(); dut.tick();
      const std::array<bool, 2> rx{{bool(dut.outputs().pfirst_urx), bool(dut.outputs().psecond_urx)}};
#endif
      for (unsigned i = 0; i < channels.size(); ++i) {
        channels[i].observe(rx[i], reset);
        if (connected) channels[i].poll();
      }
    };
    const auto advance = [&](int count) { for (int i = 0; i < count; ++i) cycle(); };
    const auto write = [&](unsigned side, int value) {
      require(uart_test_write(channels[side].id, value) == 0, "PTY write failed");
      channels[side].serial_expected.push_back(value);
    };
    const auto drain = [&]() {
      for (int remaining = 12000; remaining > 0; --remaining) {
        bool pending = false;
        for (const auto& channel : channels) pending |= !channel.serial_expected.empty() || !channel.host_expected.empty();
        if (!pending) { advance(80); return; }
        cycle();
      }
      throw std::runtime_error("UART lost queued bytes or stalled under backpressure");
    };
    const auto send = [&](unsigned side, int value, bool stop) {
      auto& channel = channels[side];
      channel.host_expected.push_back(value);
      tx[side] = false; advance(channel.clocks_per_bit);
      for (int bit = 0; bit < 8; ++bit) {
        tx[side] = ((value >> bit) & 1) != 0;
        advance(channel.clocks_per_bit);
      }
      tx[side] = stop; advance(channel.clocks_per_bit);
      tx[side] = true; advance(channel.clocks_per_bit);
    };

    advance(4);
    for (const auto& channel : channels) require(uart_test_connect(channel.id) == 0, "could not open slave PTY");
    connected = true;
    // Queued host data survives reset, and transmitter busy must backpressure
    // the host across the registered foreign-call result boundary.
    for (int value : {0xa5, 0x00, 0xff, 0x3c}) write(0, value);
    for (int value : {0x5a, 0xff, 0x00}) write(1, value);
    advance(8);
    reset = false;
    send(0, 0xa6, true);
    send(1, 0x71, true);
    drain();
    send(0, 0x55, false);
    send(1, 0x93, false);
    send(0, 0x00, true);
    send(1, 0xff, true);
    drain();
    for (const auto& channel : channels) require(uart_pty_framing_errors(channel.id) == 1, "framing error count differs");

    reset = true;
    advance(4);
    write(0, 0x42); write(0, 0x81); write(1, 0x24); write(1, 0x18);
    advance(8);
    reset = false;
    drain();
    // Extra idle cycles detect duplicate serial frames and PTY bytes.
    advance(400);
    for (const auto& channel : channels) {
      require(uart_pty_framing_errors(channel.id) == 1, "reset changed host error history");
      std::cout << channel.id << ' ' << channel.serial_count << ' ' << channel.host_count << '\n';
    }
    std::cout << "UART rsim integration passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
