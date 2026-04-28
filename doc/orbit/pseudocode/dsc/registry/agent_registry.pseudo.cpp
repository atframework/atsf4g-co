#include "agent_registry.pseudo.h"

namespace atorbit {
namespace dsc {
namespace registry {

result_code_t agent_registry::upsert_registered_agent(const agent_registration_t& registration) {
  if (0 == registration.agent_id || nullptr == registration.region) {
    return -1;
  }

  auto* agent = find_agent(registration.agent_id);
  if (nullptr == agent) {
    agent = allocate_agent(registration.agent_id);
  }

  if (nullptr == agent) {
    return -2;
  }

  agent->agent_id = registration.agent_id;
  agent->region = registration.region;
  agent->last_load = registration.load;
  agent->occupied = true;
  return 0;
}

result_code_t agent_registry::refresh_agent_heartbeat(agent_id_t agent_id, long long now_ms, const load_snapshot_t& load) {
  auto* agent = find_agent(agent_id);
  if (nullptr == agent || now_ms <= 0) {
    return -1;
  }

  agent->last_heartbeat_ms = now_ms;
  agent->last_load.cpu_used = load.cpu_used;
  agent->last_load.memory_used_mb = load.memory_used_mb;
  agent->last_load.running_ds_count = load.running_ds_count;
  return 0;
}

result_code_t agent_registry::update_agent_load(agent_id_t agent_id, const load_snapshot_t& load) {
  auto* agent = find_agent(agent_id);
  if (nullptr == agent) {
    return -1;
  }

  agent->last_load.cpu_used = load.cpu_used;
  agent->last_load.memory_used_mb = load.memory_used_mb;
  agent->last_load.cpu_available = load.cpu_available;
  agent->last_load.memory_available_mb = load.memory_available_mb;
  agent->last_load.running_ds_count = load.running_ds_count;
  return 0;
}

unsigned long long agent_registry::list_agents(agent_record_t output_records[], unsigned long long capacity) const {
  if (nullptr == output_records || 0 == capacity) {
    return 0;
  }

  unsigned long long output_count = 0;
  for (const auto& agent : agents_) {
    if (!agent.occupied) {
      continue;
    }

    output_records[output_count++] = agent;
    if (output_count >= capacity) {
      break;
    }
  }

  return output_count;
}

bool agent_registry::has_agent(agent_id_t agent_id) const {
  return nullptr != find_agent(agent_id);
}

unsigned long long agent_registry::get_agent_count() const {
  unsigned long long count = 0;
  for (const auto& agent : agents_) {
    if (agent.occupied) {
      ++count;
    }
  }

  return count;
}

long long agent_registry::get_last_heartbeat_ms(agent_id_t agent_id) const {
  const auto* agent = find_agent(agent_id);
  if (nullptr == agent) {
    return 0;
  }

  return agent->last_heartbeat_ms;
}

double agent_registry::get_cpu_used(agent_id_t agent_id) const {
  const auto* agent = find_agent(agent_id);
  if (nullptr == agent) {
    return 0;
  }

  return agent->last_load.cpu_used;
}

agent_record_t* agent_registry::find_agent(agent_id_t agent_id) {
  for (auto& agent : agents_) {
    if (agent.occupied && agent.agent_id == agent_id) {
      return &agent;
    }
  }

  return nullptr;
}

const agent_record_t* agent_registry::find_agent(agent_id_t agent_id) const {
  for (const auto& agent : agents_) {
    if (agent.occupied && agent.agent_id == agent_id) {
      return &agent;
    }
  }

  return nullptr;
}

agent_record_t* agent_registry::allocate_agent(agent_id_t agent_id) {
  for (auto& agent : agents_) {
    if (!agent.occupied) {
      agent.agent_id = agent_id;
      agent.occupied = true;
      return &agent;
    }
  }

  return nullptr;
}

}  // namespace registry
}  // namespace dsc
}  // namespace atorbit