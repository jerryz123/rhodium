// Converts a saved Rhodium event snapshot to native Perfetto on standard output.
#include "rheg_perfetto.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: rheg-perfetto TRACE.json > TRACE.pftrace\n";
    return 2;
  }
  try {
    std::ifstream input(argv[1]);
    if (!input) throw std::runtime_error("cannot open input trace");
    auto snapshot = rheg::read_event_trace(input);
    rheg::write_perfetto(std::cout, snapshot);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "rheg-perfetto: " << error.what() << '\n';
    return 1;
  }
}
