// Compares directly streamed Perfetto prefixes with a replayable snapshot fixture.
#include "rheg_perfetto.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  if (argc != 3) throw std::runtime_error("expected snapshot and Perfetto output paths");
  rheg::Graph graph;
  graph.bind_manifest({R"({"format":"rhodium-event-graph","version":1,"top":"StreamTest","sites":[{"id":"top/join","label":"join","payload_width":0,"fields":[]},{"id":"top/right","label":"right","payload_width":0,"fields":[]},{"id":"top/left","label":"left","payload_width":0,"fields":[]},{"id":"top/source","label":"source","payload_width":172,"fields":[{"name":"pc","width":64,"offset":108,"encoding":"hex"},{"name":"instruction","width":32,"offset":76,"encoding":"hex"},{"name":"fault","width":1,"offset":75,"encoding":"bool"},{"name":"count","width":5,"offset":70,"encoding":"unsigned"},{"name":"delta","width":5,"offset":65,"encoding":"signed"},{"name":"wide","width":65,"offset":0,"encoding":"unsigned"}]}],"dependencies":[{"parent":"top/source","child":"top/left"},{"parent":"top/source","child":"top/right"},{"parent":"top/left","child":"top/join"},{"parent":"top/right","child":"top/join"}]})",
                       {0, 0, 0, 172}, {{3, 2}, {3, 1}, {2, 0}, {1, 0}},
                       {{}, {}, {}, {{"pc",64,108,"hex"}, {"instruction",32,76,"hex"},
                                     {"fault",1,75,"bool"}, {"count",5,70,"unsigned"},
                                     {"delta",5,65,"signed"}, {"wide",65,0,"unsigned"}}}});
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
  // Independent bitwise packing exercises every alignment, including a 65-bit value.
  std::vector<std::uint32_t> words(6);
  auto put = [&](unsigned offset, unsigned width, std::uint64_t value) {
    for (unsigned bit = 0; bit < width; ++bit)
      if ((value >> bit) & 1) words[(offset + bit) / 32] |= 1U << ((offset + bit) % 32);
  };
  put(108,64,0xfedcba9876543210ULL); put(76,32,0x89abcdef);
  put(75,1,1); put(70,5,19); put(65,5,25); put(64,1,1); put(0,64,UINT64_MAX);
  for (unsigned i = words.size(); i-- > 0;) graph.record_payload({3, sequence}, i, words[i]);
  graph.record_node({3, sequence}, 0, 172);
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
