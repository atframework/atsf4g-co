#include "scheduler_service.pseudo.h"

namespace atorbit {
namespace dsc {
namespace scheduler {

namespace {

static bool same_region(const char* left, const char* right) {
  return left == right;
}

}  // namespace

scheduler_service::scheduler_service(const registry::agent_registry* agent_registry) : agent_registry_(agent_registry) {}

agent_candidate_t scheduler_service::select_agent_for_launch(const launch_request_t& request) const {
  agent_candidate_t selected;
  if (!validate_request(request)) {
    return selected;
  }

  registry::agent_record_t agent_records[64] = {};
  auto agent_count = agent_registry_->list_agents(agent_records, 64);
  for (unsigned long long index = 0; index < agent_count; ++index) {
    const auto& agent = agent_records[index];
    if (!is_agent_eligible(agent, request)) {
      continue;
    }

    auto score = compute_score(agent);
    if (!selected.found || score > selected.score || (score == selected.score && agent.agent_id < selected.agent_id)) {
      selected.found = true;
      selected.agent_id = agent.agent_id;
      selected.region = agent.region;
      selected.score = score;
    }
  }

  return selected;
}

bool scheduler_service::validate_request(const launch_request_t& request) const {
  if (nullptr == agent_registry_ || 0 == request.owner_unique_id || nullptr == request.target_region ||
      request.expected_cpu <= 0 || request.expected_memory_mb <= 0) {
    return false;
  }

  return true;
}

bool scheduler_service::is_agent_eligible(const registry::agent_record_t& agent, const launch_request_t& request) const {
  if (!agent.occupied || nullptr == agent.region || !same_region(agent.region, request.target_region)) {
    return false;
  }

  if (agent.last_load.cpu_available < request.expected_cpu ||
      agent.last_load.memory_available_mb < request.expected_memory_mb) {
    return false;
  }

  return true;
}

double scheduler_service::compute_score(const registry::agent_record_t& agent) const {
  return agent.last_load.cpu_available * 1000.0 + agent.last_load.memory_available_mb;
}

}  // namespace scheduler
}  // namespace dsc
}  // namespace atorbit