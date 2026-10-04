/* Exposes the optional Perfetto projection of live event batches and saved snapshots. */
// SPDX-License-Identifier: Apache-2.0
#ifndef RHEG_PERFETTO_H
#define RHEG_PERFETTO_H
#include "rheg.h"
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace rheg {
/* Selects raw protobuf output or one incrementally encoded gzip stream. */
enum class PerfettoCompression { None, Gzip };
/* Groups exact transfer or residency IDs on one exclusive display track without
   merging graph identities. Stall companions follow their transfer automatically. */
struct PerfettoTrackGroup {
  std::string label;
  std::vector<std::string> sites;
};
using PerfettoTrackGroups = std::vector<PerfettoTrackGroup>;
/* Reads version-1 rheg-perfetto-tracks JSON; the writer checks manifest membership. */
PerfettoTrackGroups read_perfetto_track_groups(std::istream& input);
/* Encodes one epoch on a caller-owned stream that must outlive the writer.
   Live writes accept typed deltas without serializing or reparsing occurrence JSON. */
class PerfettoWriter {
public:
  /* Validates metadata and emits epoch/track descriptors before occurrence batches. */
  PerfettoWriter(std::ostream& output, const Manifest& manifest, TraceTiming timing,
                 PerfettoCompression compression = PerfettoCompression::None,
                 const PerfettoTrackGroups& track_groups = {});
  /* Releases resources without implicitly finishing output or swallowing finish errors. */
  ~PerfettoWriter();
  /* Prevents two encoder objects from owning the same stream and epoch state. */
  PerfettoWriter(const PerfettoWriter&) = delete;
  /* Prevents replacement of an encoder's stream and in-progress interval state. */
  PerfettoWriter& operator=(const PerfettoWriter&) = delete;
  /* Accepts strictly increasing settled watermarks; malformed batches do not advance
     state, while an I/O failure makes the writer unusable and may leave partial bytes. */
  void write(const CycleBatch& batch);
  /* Closes pending stalls, finishes gzip framing, and flushes idempotently; subsequent
     writes fail. Unreleased residency owners remain open. Call explicitly to observe errors. */
  void finish();
private:
  /* Hides JSON, disassembly, compression, and epoch bookkeeping from library callers. */
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
/* Parses and validates a timed version-1 rhodium-event-trace snapshot.
   JSON parsing belongs to this optional exporter, not the stdlib-only DPI collector. */
Snapshot read_event_trace(std::istream& input);
/* Replays a timed snapshot through the identical live writer, including empty traces. */
void write_perfetto(std::ostream& output, const Snapshot& snapshot,
                    PerfettoCompression compression = PerfettoCompression::None,
                    const PerfettoTrackGroups& track_groups = {});
}
#endif
