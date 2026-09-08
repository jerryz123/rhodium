// Streams settled SimpleSoC event cycles using the descriptor generated alongside RTL.
#include "rheg_perfetto.h"
#include "soc_events.h"
#include <cstdio>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {
std::ofstream output;
std::unique_ptr<rheg::PerfettoWriter> writer;
template <typename F> int checked(F action) {
  try { action(); return 0; }
  catch (const std::exception& error) {
    std::fprintf(stderr, "RHEG: %s\n", error.what());
    return 1;
  }
}
}

extern "C" int rheg_sim_open(const char* path) {
  return checked([&] {
    if (writer) throw std::runtime_error("trace already open");
    output.open(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open trace output");
    auto& graph = rheg::graph();
    graph.bind_manifest(rheg_generated::manifest());
    graph.bind_timing({rheg_generated::clock_frequency_hz, 0});
    const auto header = graph.begin_stream();
    const std::string_view name(path);
    const auto compression = name.size() >= 3 && name.substr(name.size() - 3) == ".gz"
        ? rheg::PerfettoCompression::Gzip : rheg::PerfettoCompression::None;
    writer = std::make_unique<rheg::PerfettoWriter>(output, header.manifest(), *header.timing(), compression);
  });
}

extern "C" int rheg_sim_cycle(unsigned long long cycle) {
  return checked([&] {
    if (!writer) throw std::runtime_error("trace is not open");
    writer->write(rheg::graph().finish_cycle(cycle));
  });
}

extern "C" int rheg_sim_close() {
  return checked([&] {
    if (!writer) throw std::runtime_error("trace is not open");
    rheg::graph().end_stream();
    writer->finish();
    writer.reset();
    output.close();
    if (!output) throw std::runtime_error("trace close failed");
  });
}
