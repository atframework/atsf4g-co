#pragma once

// Phase 2
// 目标: 固化 DSA 向 DSC 的注册、心跳、负载、DS 启停与 reconcile 上报流程。
// 未来真实落点: src/dsa/agent/controller_reporter.cpp

namespace atorbit {
namespace dsa {
namespace agent {

struct agent_register_report_t {
  unsigned long long agent_id = 0;
  const char* region = nullptr;
  double cpu_capacity = 0;
  double memory_capacity_mb = 0;
};

struct ds_started_report_t {
  unsigned long long ds_id = 0;
  int process_id = 0;
  const char* local_endpoint = nullptr;
};

struct ds_exit_report_t {
  unsigned long long ds_id = 0;
  int exit_reason = 0;
  int exit_code = 0;
  const char* user_data = nullptr;
};

struct load_report_t {
  double cpu_used = 0;
  double memory_used_mb = 0;
  double cpu_available = 0;
  double memory_available_mb = 0;
  unsigned long long running_ds_count = 0;
};

class controller_reporter {
public:
  int register_agent(const agent_register_report_t& report);
  int heartbeat_agent(unsigned long long agent_id, long long now_ms);
  int report_agent_load(const load_report_t& report);
  int notify_ds_started(const ds_started_report_t& report);
  int notify_ds_exit(const ds_exit_report_t& report);
  int reconcile_agent_state(unsigned long long agent_id, const load_report_t& report);
  int retry_failed_reports();

private:
  int enqueue_retry(const char* rpc_name, unsigned long long entity_id);

private:
  struct retry_record_t {
    const char* rpc_name = nullptr;
    unsigned long long entity_id = 0;
    unsigned long long retry_times = 0;
  };

  retry_record_t retry_queue_[64];
  unsigned long long retry_count_ = 0;
};

}  // namespace agent
}  // namespace dsa
}  // namespace atorbit
