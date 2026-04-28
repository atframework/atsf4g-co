#include "load_reporter.pseudo.h"

namespace atorbit {
namespace dsa {
namespace agent {

load_reporter::load_reporter(shared::runtime::resource_ledger* ledger) : ledger_(ledger) {}

agent_load_snapshot_t load_reporter::build_agent_load_snapshot() const {
  agent_load_snapshot_t snapshot;
  if (nullptr == ledger_) {
    return snapshot;
  }

  auto ledger_snapshot = ledger_->build_snapshot();
  snapshot.cpu_used = ledger_snapshot.cpu_used;
  snapshot.cpu_capacity = ledger_snapshot.cpu_capacity;
  snapshot.memory_used_mb = ledger_snapshot.memory_used_mb;
  snapshot.memory_capacity_mb = ledger_snapshot.memory_capacity_mb;
  snapshot.cpu_available = ledger_snapshot.cpu_available;
  snapshot.memory_available_mb = ledger_snapshot.memory_available_mb;
  snapshot.running_ds_count = ledger_snapshot.running_ds_count;
  return snapshot;
}

unsigned long long load_reporter::select_oom_victim_if_needed(double pod_memory_usage_mb,
                                                              double memory_kill_threshold_mb) const {
  if (nullptr == ledger_ || pod_memory_usage_mb <= memory_kill_threshold_mb) {
    return 0;
  }

  auto candidate = ledger_->select_oom_candidate();
  return candidate.found ? candidate.ds_id : 0;
}

}  // namespace agent
}  // namespace dsa
}  // namespace atorbit
