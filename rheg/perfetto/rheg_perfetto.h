// Exposes the rheg C++ Perfetto encoder for live batches and saved traces.
// SPDX-License-Identifier: Apache-2.0
#ifndef RHEG_PERFETTO_H
#define RHEG_PERFETTO_H
#include "rheg.h"
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace rheg {
enum class PerfettoCompression { None, Gzip };
// Presentation only: exact transfer or residency IDs share one exclusive track.
// Stall companions follow their transfer automatically. Unlisted sites stay separate.
struct PerfettoTrackGroup {
  std::string label;
  std::vector<std::string> sites;
};
using PerfettoTrackGroups = std::vector<PerfettoTrackGroup>;
// Read version-1 rheg-perfetto-tracks JSON. Site membership is checked by the writer.
PerfettoTrackGroups read_perfetto_track_groups(std::istream& input);
// One epoch and one output stream per writer. The caller owns the output stream
// and must keep it alive. No JSON serialization is involved in write().
class PerfettoWriter {
public:
  PerfettoWriter(std::ostream& output, const Manifest& manifest, TraceTiming timing,
                 PerfettoCompression compression = PerfettoCompression::None,
                 const PerfettoTrackGroups& track_groups = {});
  ~PerfettoWriter();
  PerfettoWriter(const PerfettoWriter&) = delete;
  PerfettoWriter& operator=(const PerfettoWriter&) = delete;
  void write(const CycleBatch& batch);
  // Close pending stall slices, finish gzip framing, and flush output.
  // Idempotent; rejects subsequent writes.
  // Call explicitly to observe errors; destruction only releases resources.
  void finish();
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
// Read and validate a complete version-1 rhodium-event-trace JSON snapshot.
// This parser is owned by the optional exporter, not by the DPI collector.
Snapshot read_event_trace(std::istream& input);
// Uses the identical writer/ordering as streaming, including empty traces.
void write_perfetto(std::ostream& output, const Snapshot& snapshot,
                    PerfettoCompression compression = PerfettoCompression::None,
                    const PerfettoTrackGroups& track_groups = {});
}
#endif
