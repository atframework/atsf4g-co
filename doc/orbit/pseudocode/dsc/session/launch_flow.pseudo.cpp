#include "launch_flow.pseudo.h"

namespace atorbit {
namespace dsc {
namespace session {

launch_flow::launch_flow(start_ds_sender* start_ds_sender_handle) : start_ds_sender_handle_(start_ds_sender_handle) {}

result_code_t launch_flow::reserve_and_dispatch(const launch_dispatch_request_t& request) {
  if (!validate_request(request)) {
    return -1;
  }

  auto* entry = allocate_inflight_entry(request.agent_id);
  if (nullptr == entry) {
    return -2;
  }

  entry->occupied = true;
  entry->owner_unique_id = request.owner_unique_id;
  entry->agent_id = request.agent_id;
  entry->request_id = request.request_id;
  entry->expire_at_ms = request.now_ms + inflight_timeout_ms_;

  auto send_result = start_ds_sender_handle_->send_start_ds(request);
  if (send_result != 0) {
    rollback_inflight(request.agent_id, request.request_id);
    return -3;
  }

  return 0;
}

result_code_t launch_flow::complete_inflight(unsigned long long agent_id, unsigned long long request_id) {
  for (auto& entry : inflight_entries_) {
    if (entry.occupied && entry.agent_id == agent_id && entry.request_id == request_id) {
      entry = {};
      return 0;
    }
  }

  return -1;
}

unsigned long long launch_flow::prune_expired_inflight(long long now_ms) {
  if (now_ms <= 0) {
    return 0;
  }

  unsigned long long pruned_count = 0;
  for (auto& entry : inflight_entries_) {
    if (!entry.occupied || entry.expire_at_ms > now_ms) {
      continue;
    }

    entry = {};
    ++pruned_count;
  }

  return pruned_count;
}

unsigned long long launch_flow::inflight_count(unsigned long long agent_id) const {
  unsigned long long count = 0;
  for (const auto& entry : inflight_entries_) {
    if (entry.occupied && entry.agent_id == agent_id) {
      ++count;
    }
  }

  return count;
}

bool launch_flow::validate_request(const launch_dispatch_request_t& request) const {
  if (nullptr == start_ds_sender_handle_ || 0 == request.owner_unique_id || 0 == request.agent_id || 0 == request.request_id ||
      request.expected_cpu <= 0 || request.expected_memory_mb <= 0 || request.now_ms <= 0) {
    return false;
  }

  return true;
}

launch_flow::inflight_entry_t* launch_flow::allocate_inflight_entry(unsigned long long agent_id) {
  if (inflight_count(agent_id) >= max_inflight_per_agent_) {
    return nullptr;
  }

  for (auto& entry : inflight_entries_) {
    if (!entry.occupied) {
      return &entry;
    }
  }

  return nullptr;
}

void launch_flow::rollback_inflight(unsigned long long agent_id, unsigned long long request_id) {
  for (auto& entry : inflight_entries_) {
    if (entry.occupied && entry.agent_id == agent_id && entry.request_id == request_id) {
      entry = {};
      return;
    }
  }
}

}  // namespace session
}  // namespace dsc
}  // namespace atorbit