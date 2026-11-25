// SPDX-License-Identifier: Apache-2.0
#include "ports.hpp"
#include <iostream>

int main() {
  int reset, offer, data, consume, ready, valid, head;
  unsigned edges = 0;
  while (std::cin >> reset >> offer >> data >> consume >> ready >> valid >> head) {
    ports::reset = reset;
    ports::offer = offer;
    ports::data = data;
    ports::consume = consume;
    dut.eval();
    // Compare pre-edge pins. -1 denotes an uncared rspecsim observation.
    if ((ready >= 0 && ports::ready != ready) || (valid >= 0 && ports::valid != valid) ||
        (head >= 0 && ports::head != head)) {
      std::cerr << "QueuePair edge " << edges << " ready/valid/head: expected "
                << ready << '/' << valid << '/' << head << ", saw "
                << +ports::ready << '/' << +ports::valid << '/' << +ports::head << '\n';
      return 1;
    }
    dut.tick();
    ++edges;
  }
  return std::cin.eof() && edges == 8 ? 0 : 1;
}
