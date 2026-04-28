#pragma once

// Phase 3.2 / 3.3 / 3.4
// 目标: 固化 DSC 侧对 DSA 注册、心跳和负载快照的状态落点。
// 未来真实落点: src/dsc/registry/agent_registry.cpp

namespace atorbit {
namespace dsc {
namespace registry {

using result_code_t = int;
using agent_id_t = unsigned long long;

struct load_snapshot_t {
  double cpu_capacity = 0;
  double memory_capacity_mb = 0;
  double cpu_used = 0;
  double memory_used_mb = 0;
  double cpu_available = 0;
  double memory_available_mb = 0;
  unsigned long long running_ds_count = 0;
  unsigned long long current_ds_count = 0;
};

struct agent_registration_t {
  agent_id_t agent_id = 0;
  const char* region = nullptr;
  load_snapshot_t load;
};

struct agent_record_t {
  agent_id_t agent_id = 0;
  const char* region = nullptr;
  load_snapshot_t last_load;
  long long last_heartbeat_ms = 0;
  bool occupied = false;
};

class agent_registry {
public:
  result_code_t upsert_registered_agent(const agent_registration_t& registration);
  result_code_t refresh_agent_heartbeat(agent_id_t agent_id, long long now_ms, const load_snapshot_t& load);
  result_code_t update_agent_load(agent_id_t agent_id, const load_snapshot_t& load);
  unsigned long long list_agents(agent_record_t output_records[], unsigned long long capacity) const;

  bool has_agent(agent_id_t agent_id) const;
  unsigned long long get_agent_count() const;
  long long get_last_heartbeat_ms(agent_id_t agent_id) const;
  double get_cpu_used(agent_id_t agent_id) const;

private:
  agent_record_t* find_agent(agent_id_t agent_id);
  const agent_record_t* find_agent(agent_id_t agent_id) const;
  agent_record_t* allocate_agent(agent_id_t agent_id);

private:
  agent_record_t agents_[64];
};

}  // namespace registry
}  // namespace dsc
}  // namespace atorbit