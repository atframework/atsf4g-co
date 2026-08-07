// Copyright 2026 atframework

#pragma once

#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace atframework {
namespace testing {
namespace detail {

// Move every queued event whose deliver_at_generation is due into the returned batch, preserving
// insertion order. Shared by the mock engines' deliver_pending paths. Two subtleties:
// - Insertion order is not due order when rules carry different delays, so callers must not stop at
//   the first not-due entry (a queued-early slow event must not block a queued-later fast one).
// - The batch is drained before delivery because delivering may synchronously resume coroutines that
//   re-enter the engine and push_back into the same queue (deque push_back invalidates iterators).
template <class TEVENT>
std::vector<TEVENT> drain_due_events(std::deque<TEVENT> &queue, uint64_t current_generation) {
  std::vector<TEVENT> due;
  for (auto iter = queue.begin(); iter != queue.end();) {
    if (iter->deliver_at_generation > current_generation) {
      ++iter;
      continue;
    }
    due.push_back(std::move(*iter));
    iter = queue.erase(iter);
  }
  return due;
}

}  // namespace detail
}  // namespace testing
}  // namespace atframework
