// Compares directly streamed Perfetto prefixes with a replayable snapshot fixture.
#include "rheg_perfetto.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  if (argc != 3) throw std::runtime_error("expected snapshot and Perfetto output paths");
  rheg::Graph graph;
  graph.bind_manifest({R"({"format":"rhodium-event-graph","version":1,"top":"StreamTest","sites":[{"id":"top/join","label":"join","payload_width":0},{"id":"top/right","label":"right","payload_width":0},{"id":"top/left","label":"left","payload_width":0},{"id":"top/source","label":"source","payload_width":8}],"dependencies":[{"parent":"top/source","child":"top/left"},{"parent":"top/source","child":"top/right"},{"parent":"top/left","child":"top/join"},{"parent":"top/right","child":"top/join"}]})",
                       {0, 0, 0, 8}, {{3, 2}, {3, 1}, {2, 0}, {1, 0}}});
  graph.bind_timing({100000000, 9});
  const auto header = graph.begin_stream();
  std::ofstream output(argv[2], std::ios::binary);
  rheg::PerfettoWriter writer(output, header.manifest(), *header.timing());
  auto finish = [&](std::uint64_t cycle) {
    writer.write(graph.finish_cycle(cycle));
    std::filesystem::copy_file(argv[2], std::string(argv[2]) + ".prefix" + std::to_string(cycle),
                               std::filesystem::copy_options::overwrite_existing);
  };
  graph.reset(true);
  constexpr std::uint64_t sequence = 9007199254740993ULL;
  graph.record_payload({3, sequence}, 0, 42);
  graph.record_node({3, sequence}, 0, 8);
  finish(0);
  graph.record_edge({3, sequence}, {2, sequence});
  graph.record_node({2, sequence}, 1, 0);
  finish(1);
  // Reverse site order and callback order force topological same-cycle sorting.
  graph.record_edge({1, sequence}, {0, sequence});
  graph.record_edge({2, sequence}, {0, sequence});
  graph.record_edge({3, sequence}, {1, sequence});
  graph.record_node({0, sequence}, 2, 0);
  graph.record_node({1, sequence}, 2, 0);
  finish(2);
  graph.end_stream();
  std::ofstream snapshot(argv[1]);
  snapshot << graph.snapshot().json();
  snapshot.close();
  if (!snapshot) throw std::runtime_error("snapshot write failed");
}
