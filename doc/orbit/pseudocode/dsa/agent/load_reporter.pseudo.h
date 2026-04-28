#pragma once

#include "../../shared/runtime/resource_ledger.pseudo.h"

// Phase 2
// 目标: 固化 per-DS 负载聚合、Pod 快照构建和 OOM 候选选择。
// 未来真实落点: src/dsa/agent/load_reporter.cpp

namespace atorbit {
namespace dsa {
namespace agent {

struct agent_load_snapshot_t {
  double cpu_used = 0;
  double cpu_capacity = 0;
  double memory_used_mb = 0;
  double memory_capacity_mb = 0;
  double cpu_available = 0;
  double memory_available_mb = 0;
  unsigned long long running_ds_count = 0;
};

class load_reporter {
public:
  explicit load_reporter(shared::runtime::resource_ledger* ledger);

  agent_load_snapshot_t build_agent_load_snapshot() const;
  unsigned long long select_oom_victim_if_needed(double pod_memory_usage_mb, double memory_kill_threshold_mb) const;

private:
  shared::runtime::resource_ledger* ledger_ = nullptr;
};

}  // namespace agent
}  // namespace dsa
}  // namespace atorbit
