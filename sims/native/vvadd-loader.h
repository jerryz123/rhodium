// Loads shared RV64 workloads coherently and verifies concurrent hart progress and results.
#ifndef RHODIUM_VVADD_LOADER_H
#define RHODIUM_VVADD_LOADER_H
#include <array>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "chase-workload.h"
#include "workload-host.h"

struct VvaddLoader {
  rds_workload_host host_bridge{};
  int tick_current(const uint64_t *in, size_t ni, uint64_t *out, size_t no) {
    return rds_workload_host_tick(&host_bridge, [](void *p, const uint64_t *a, size_t n, uint64_t *b, size_t m) { return static_cast<VvaddLoader *>(p)->tick(a,n,b,m); }, this, in, ni, out, no);
  }

  static constexpr uint64_t memory_base = 0x80000000, stride = 0x4000,
                            length = 64;
  struct Write {
    uint64_t address;
    uint32_t data;
  };
  struct Hart {
    uint64_t progress = 0, checksum = 0, retired = 0, start = 0, finish = 0;
    bool heartbeat = false;
  };
  enum Stage {
    Load,
    Drain,
    Start,
    Ready,
    Release,
    ReleaseDrain,
    Running,
    Metadata,
    Results,
    Done
  };
  unsigned harts = 8, rounds = 64;
  bool chasing = false;
  ChaseWorkload chase;
  const char *workload() const { return chasing ? "bank-striped-pointer-chase" : "vvadd"; }
  std::vector<Write> writes;
  std::array<Hart, 8> hart{};
  Stage stage = Load;
  size_t write_index = 0;
  unsigned reader = 0, field = 0, element = 0, half = 0, ready_mask = 0,
           done_mask = 0;
  uint64_t low = 0, ticks = 0, deadline = 0, stop = 0, polls = 0,
           trace = 14695981039346656037ULL;
  bool request = false, start_request = false, waiting = false,
       measuring = false, finished_measurement = false;
  std::string error;

  uint64_t base(unsigned h) const { return memory_base + stride * h; }
  uint64_t expected(unsigned h, unsigned i) const {
    if (chasing) return chase.data[h][ChaseWorkload::sample(i)];
    return uint64_t(h) * 8192 + 3 * i + 1;
  }
  void configure(const char *path, unsigned count, unsigned repetitions) {
    const char *selected = std::getenv("RDS_WORKLOAD");
    if (selected && std::string(selected) != "vvadd" && std::string(selected) != "chase")
      throw std::runtime_error("RDS_WORKLOAD must be vvadd or chase");
    chasing = selected && std::string(selected) == "chase";
    if ((count != 1 && count != 2 && count != 4 && count != 8) ||
        repetitions < (chasing ? 2u : 32u) || repetitions > (chasing ? 256u : 65536u) ||
        (chasing && count != 8))
      throw std::runtime_error(
          "vvadd requires 1/2/4/8 harts and 32..65536 rounds; chase requires eight harts and 2..256 rounds");
    harts = count;
    rounds = repetitions;
    if (chasing) chase.initialize(harts, rounds);
    std::ifstream f(path, std::ios::binary);
    if (!f)
      throw std::runtime_error("cannot open target binary");
    std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(f), {}};
    if (bytes.empty() || bytes.size() > 512 || bytes.size() % 4)
      throw std::runtime_error("invalid target size");
    writes.clear();
    for (size_t i = 0; i < bytes.size(); i += 4)
      writes.push_back({memory_base + i, uint32_t(bytes[i]) |
                                             (uint32_t(bytes[i + 1]) << 8) |
                                             (uint32_t(bytes[i + 2]) << 16) |
                                             (uint32_t(bytes[i + 3]) << 24)});
    for (unsigned i = 0; i < 6; ++i)
      writes.push_back({memory_base + 0x200 + i * 4, i == 0   ? uint32_t(chasing ? ChaseWorkload::nodes : length)
                                                     : i == 2 ? rounds
                                                              : 0});
    for (unsigned h = 0; h < harts; ++h)
      for (unsigned offset : {0u, 4u, 16u, 20u})
        writes.push_back({base(h) + 0x300 + offset, 0});
    reset();
  }
  void reset() {
    hart = {};
    stage = Load;
    write_index = 0;
    reader = field = element = half = ready_mask = done_mask = 0;
    low = ticks = deadline = stop = polls = 0;
    trace = 14695981039346656037ULL;
    request = start_request = waiting = measuring = finished_measurement =
        false;
    error.clear();
  }
  void fail(const std::string &why) {
    if (error.empty())
      error = why;
  }
  uint64_t read_address() const {
    uint64_t address = base(reader);
    if (stage == Ready)
      return address + 0x300;
    if (stage == Running)
      return address + 0x300 + (field ? 16 : 8);
    if (stage == Metadata)
      return address + 0x300 + (field ? 24 + (field - 1) * 8 : 8) + half * 4;
    if (chasing) return ChaseWorkload::address(reader, ChaseWorkload::sample(element)) + half * 4;
    return address + 0x880 + element * 8 + half * 4;
  }
  void response(uint32_t word) {
    ++polls;
    if (stage == Ready) {
      if (word == 1)
        ready_mask |= 1u << reader;
      reader = (reader + 1) % harts;
      if (ready_mask == (1u << harts) - 1) {
        stage = Release;
        deadline = ticks + 16384;
        reader = 0;
      }
      return;
    }
    if (stage == Running) {
      if (!field) {
        hart[reader].progress = word;
        hart[reader].heartbeat |= word > 0 && word < rounds;
        field = 1;
      } else {
        if (word == 1)
          done_mask |= 1u << reader;
        else if (word > 1)
          fail("invalid hart completion");
        field = 0;
        reader = (reader + 1) % harts;
      }
      if (done_mask == (1u << harts) - 1) {
        stop = ticks;
        measuring = false;
        finished_measurement = true;
        stage = Metadata;
        reader = field = half = 0;
      }
      return;
    }
    if (!half) {
      low = word;
      half = 1;
      return;
    }
    uint64_t value = low | (uint64_t(word) << 32);
    half = 0;
    if (stage == Metadata) {
      auto &h = hart[reader];
      if (field == 0)
        h.progress = value;
      if (field == 1)
        h.checksum = value;
      if (field == 2)
        h.retired = value;
      if (field == 3)
        h.start = value;
      if (field == 4)
        h.finish = value;
      if (++field == 5) {
        uint64_t sum = 0;
        for (unsigned i = 0; i < length; ++i)
          sum += expected(reader, i);
        if (chasing) sum = chase.checksum[reader];
        if (h.checksum != sum || h.progress != rounds ||
            h.retired < uint64_t(rounds) * (chasing ? ChaseWorkload::nodes * 22 : length * 10) || h.start < deadline ||
            h.finish <= h.start)
          fail("hart " + std::to_string(reader) +
               " progress/checksum/retirement/timing mismatch: progress=" + std::to_string(h.progress) +
               " checksum=" + std::to_string(h.checksum) + " expected=" + std::to_string(sum) +
               " retired=" + std::to_string(h.retired) + " start=" + std::to_string(h.start) +
               " deadline=" + std::to_string(deadline) + " finish=" + std::to_string(h.finish));
        field = 0;
        if (++reader == harts) {
          stage = Results;
          reader = element = 0;
        }
      }
    } else {
      if (value != expected(reader, element))
        fail("hart " + std::to_string(reader) + " workload result mismatch at " +
             std::to_string(element));
      if (++element == length) {
        element = 0;
        if (++reader == harts) {
          uint64_t latest_start = 0, earliest_finish = UINT64_MAX;
          for (unsigned h = 0; h < harts; ++h) {
            if (hart[h].start > latest_start)
              latest_start = hart[h].start;
            if (hart[h].finish < earliest_finish)
              earliest_finish = hart[h].finish;
          }
          if (latest_start >= earliest_finish)
            fail("harts did not execute concurrently");
          stage = Done;
        }
      }
    }
  }
  int tick(const uint64_t *in, size_t ni, uint64_t *out, size_t no) {
    if (ni != 5 || no != 8)
      return -1;
    for (size_t i = 0; i < no; ++i)
      out[i] = 0;
    if (in[0]) {
      reset();
      return 0;
    }
    ++ticks;
    Stage prior = stage;
    if (request && in[1]) {
      if (stage == Load) {
        if (++write_index == writes.size())
          stage = Drain;
      } else if (stage == Release)
        stage = ReleaseDrain;
      else
        waiting = true;
    }
    if (prior == Drain && in[1])
      stage = Start;
    if (prior == ReleaseDrain && in[1])
      stage = Running;
    if (stage == Start && start_request && in[4])
      stage = Ready;
    if (waiting && in[2]) {
      waiting = false;
      response(uint32_t(in[3]));
    }
    if (deadline && ticks == deadline)
      measuring = true;
    out[2] = memory_base + 0x200;
    out[4] = 1;
    if (stage == Load) {
      out[0] = out[1] = 1;
      out[2] = writes[write_index].address;
      out[3] = writes[write_index].data;
    } else if (stage == Start) {
      out[5] = 1;
      out[6] = memory_base;
    } else if (stage == Release) {
      out[0] = out[1] = 1;
      out[2] = memory_base + 0x210;
      out[3] = deadline;
    } else if ((stage == Ready || stage == Running || stage == Metadata ||
                stage == Results) &&
               !waiting) {
      out[0] = 1;
      out[2] = read_address();
    } else if (stage == Done)
      out[7] = error.empty() ? 1 : 2;
    request = out[0];
    start_request = out[5];
    for (size_t i = 0; i < ni + no; ++i) {
      uint64_t x = i < ni ? in[i] : out[i - ni];
      for (unsigned b = 0; b < 8; ++b) {
        trace ^= (x >> (8 * b)) & 255;
        trace *= 1099511628211ULL;
      }
    }
    return 0;
  }
};
#endif
