// Converts a saved Rhodium event snapshot to native Perfetto on standard output.
// SPDX-License-Identifier: Apache-2.0
#include "rheg_perfetto.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv) {
  bool gzip = false;
  const char* path = nullptr;
  const char* tracks = nullptr;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--gzip" && !gzip) gzip = true;
    else if (arg == "--tracks" && !tracks && i + 1 < argc) tracks = argv[++i];
    else if (arg.substr(0, 2) != "--" && !path) path = argv[i];
    else {
      std::cerr << "usage: rheg-perfetto [--gzip] [--tracks TRACKS.json] TRACE.json > TRACE.pftrace[.gz]\n";
      return 2;
    }
  }
  if (!path) {
    std::cerr << "usage: rheg-perfetto [--gzip] [--tracks TRACKS.json] TRACE.json > TRACE.pftrace[.gz]\n";
    return 2;
  }
  try {
    rheg::PerfettoTrackGroups groups;
    if (tracks) {
      std::ifstream config(tracks);
      if (!config) throw std::runtime_error("cannot open track group configuration");
      groups = rheg::read_perfetto_track_groups(config);
    }
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open input trace");
    auto snapshot = rheg::read_event_trace(input);
    rheg::write_perfetto(std::cout, snapshot, gzip ? rheg::PerfettoCompression::Gzip : rheg::PerfettoCompression::None, groups);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "rheg-perfetto: " << error.what() << '\n';
    return 1;
  }
}
