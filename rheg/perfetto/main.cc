// Converts a saved Rhodium event snapshot to native Perfetto on standard output.
#include "rheg_perfetto.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv) {
  const bool gzip = argc == 3 && std::string_view(argv[1]) == "--gzip";
  if ((!gzip && argc != 2) || (argc == 2 && std::string_view(argv[1]).substr(0, 2) == "--")) {
    std::cerr << "usage: rheg-perfetto [--gzip] TRACE.json > TRACE.pftrace[.gz]\n";
    return 2;
  }
  try {
    std::ifstream input(argv[gzip ? 2 : 1]);
    if (!input) throw std::runtime_error("cannot open input trace");
    auto snapshot = rheg::read_event_trace(input);
    rheg::write_perfetto(std::cout, snapshot, gzip ? rheg::PerfettoCompression::Gzip : rheg::PerfettoCompression::None);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "rheg-perfetto: " << error.what() << '\n';
    return 1;
  }
}
