// Supplies an explicit synchronous executor for single-hart Highway library consumers, without OS threads.
// SPDX-License-Identifier: Apache-2.0
#ifndef RHODIUM_HIGHWAY_SERIAL_POOL_H_
#define RHODIUM_HIGHWAY_SERIAL_POOL_H_
#include <cstddef>
#include <cstdint>
#include "hwy/base.h"
// The library accepts a concrete hwy::ThreadPool; provide only its sequential
// contract in this bare-metal compilation instead of importing the hosted pool.
#define HIGHWAY_HWY_CONTRIB_THREAD_POOL_THREAD_POOL_H_
namespace hwy {
class ThreadPool {
 public:
  explicit ThreadPool(size_t threads) { HWY_ASSERT(threads == 0); }
  size_t NumWorkers() const { return 1; }
  template <class Closure>
  void Run(uint64_t begin, uint64_t end, const Closure& closure) {
    for (uint64_t task = begin; task < end; ++task) closure(task, size_t{0});
  }
};
}  // namespace hwy
#endif
