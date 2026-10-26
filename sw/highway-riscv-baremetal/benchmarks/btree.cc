// Runs Highway B-tree construction, bounded hit/miss/boundary queries, full traversal, and mutation.
// SPDX-License-Identifier: Apache-2.0
#include "benchmark.h"
#include "hwy/contrib/btree/btree-inl.h"
#include <vector>
template <class Key, class Value> void BenchTree() {
  for (size_t n : {size_t{16}, size_t{257}}) {
    constexpr size_t kQueries = 16;
    std::vector<Key> keys(n), queries(kQueries);
    std::vector<Value> values(n), output(kQueries);
    std::unique_ptr<bool[]> found(new bool[kQueries]);
    HighwaySetPhase(HighwayPhase::Inputs);
    for (size_t i = 0; i < n; ++i) {
      keys[i] = static_cast<Key>((n - i) * 10);
      values[i] = static_cast<Value>(i * 100);
    }
    // Include both endpoint hits, misses outside the range, and interior
    // hit/miss pairs without scaling the query batch with the tree size.
    queries[0] = 0;
    queries[1] = keys.back();
    queries[2] = keys.front();
    queries[3] = keys.front() + 1;
    for (size_t i = 4; i < kQueries; ++i)
      queries[i] = keys[(i * 37) % n] + static_cast<Key>(i % 2);
    HighwaySetPhase(HighwayPhase::Setup);
    Measure("btree", n, [&] {
      auto set = hn::BTreeSet<Key>::Build(keys.data(), n);
      auto map = hn::BTreeMap<Key, Value>::Build(keys.data(), values.data(), n);
      uint64_t hits = 0;
      for (Key q : queries) {
        hits += set.contains(q);
        hits += (set.lower_bound(q) != set.end()) + (set.upper_bound(q) != set.end());
        hits += (map.find(q) != map.end());
      }
      map.LookupBatch(queries.data(), kQueries, output.data(), found.get());
      std::vector<typename decltype(set)::const_iterator> lower(kQueries);
      set.LowerBoundBatch(queries.data(), kQueries, lower.data());
      for (Key key : set) hits += static_cast<uint64_t>(key);
      for (size_t i = 0; i < 8; ++i) {
        set.insert(static_cast<Key>(n * 10 + i)); set.erase(keys[i]);
        map.insert({static_cast<Key>(n * 10 + i), static_cast<Value>(i)}); map.erase(keys[i]);
      }
      Consume(hits); Consume(output.data()); Consume(lower.data());
    });
  }
}
void RunBenchmarks() { BenchTree<uint32_t, uint64_t>(); BenchTree<uint64_t, double>(); }
