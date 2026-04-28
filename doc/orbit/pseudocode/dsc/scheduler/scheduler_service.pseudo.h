#pragma once

#include "../registry/agent_registry.pseudo.h"

// Phase 3.8 / 3.9
// 目标: 固化 LaunchDedicatedServer 的 agent 选择、容量过滤与 tie-break 规则。
// 未来真实落点: src/dsc/scheduler/scheduler_service.cpp

namespace atorbit {
namespace dsc {
namespace scheduler {

struct launch_request_t {
  unsigned long long owner_unique_id = 0;
  const char* target_region = nullptr;
  double expected_cpu = 0;
  double expected_memory_mb = 0;
};

struct agent_candidate_t {
  bool found = false;
  unsigned long long agent_id = 0;
  const char* region = nullptr;
  double score = 0;
};

class scheduler_service {
public:
  explicit scheduler_service(const registry::agent_registry* agent_registry);

  agent_candidate_t select_agent_for_launch(const launch_request_t& request) const;

private:
  bool validate_request(const launch_request_t& request) const;
  bool is_agent_eligible(const registry::agent_record_t& agent, const launch_request_t& request) const;
  double compute_score(const registry::agent_record_t& agent) const;

private:
  const registry::agent_registry* agent_registry_ = nullptr;
};

}  // namespace scheduler
}  // namespace dsc
}  // namespace atorbit